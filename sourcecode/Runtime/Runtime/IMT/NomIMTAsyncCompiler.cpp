#include "NomIMTAsyncCompiler.h"
#include "NomIMTUtils.h"
#include "NomJIT.h"
#include "RTConfig.h"
#include "gcinclude_config.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>

// Boehm declares its thread registration API only under GC_THREADS,
// libgc is built with thread support and exports these.
extern "C" {
void GC_allow_register_threads(void);
int GC_register_my_thread(const struct GC_stack_base *);
int GC_unregister_my_thread(void);
}

using namespace llvm;
namespace Nom {
namespace Runtime {
namespace IMTUtils {
namespace {
struct IMTCompileJob {
  // input: what the entry is built from
  NomRecord *nomRecord;
  int imtIndex;
  const NomInterfaceCallTag *callTag;
  void *imtEntry; // only for debug log
  void *callTagFunAddr;
  IMTCallTagSet parentCallTags; // a copy of the extended node's call-tags

  std::string name;                     // output: the entry's name
  std::unique_ptr<MemoryBuffer> object; // output: the compiled object
  std::string error;                    // output: set if compiling failed

  // main thread only: where the entry is installed
  NomIMTGraph *imtGraph;
  NomIMTNode *imtNode; // the node the new entry extends
  void **imtArray;

  IMTCompileJob(NomRecord *nomRecord, int imtIndex, void *imtEntry,
                const NomInterfaceCallTag *callTag, void *callTagFunAddr,
                IMTCallTagSet parentCallTags, NomIMTGraph *imtGraph,
                NomIMTNode *imtNode, void **imtArray)
      : nomRecord(nomRecord), imtIndex(imtIndex), callTag(callTag),
        imtEntry(imtEntry), callTagFunAddr(callTagFunAddr),
        parentCallTags(std::move(parentCallTags)), imtGraph(imtGraph),
        imtNode(imtNode), imtArray(imtArray) {}
};

// Builds, optimizes and code-generates IMT entries on a worker thread
// Building the IR needs the shared LLVMCONTEXT and the runtime's type data
// so the worker does it under NomJIT's context lock
// It then hands the IR over as bitcode to a private context and optimizes and
// compiles it without the lock.
class IMTAsyncCompiler {
private:
  std::mutex mutex; // guards queued, finished and stopping
  std::condition_variable workAvailable;
  std::deque<std::unique_ptr<IMTCompileJob>> queued;
  std::deque<std::unique_ptr<IMTCompileJob>> finished;
  bool stopping = false;
  // !finished.empty(), written under mutex but read without it, so the
  // bridge can skip the lock when nothing is ready to install
  std::atomic<bool> hasFinished{false};

  // main thread only: slots with a job queued, compiling or finished but
  // not yet installed
  std::set<std::pair<const NomIMTGraph *, int>> pendingSlots;

  // declared last so everything the worker uses exists before it starts
  std::thread worker;

  void Run();
  static void Compile(IMTCompileJob &job, TargetMachine &tm);
  static SmallVector<char, 0> BuildBitcode(IMTCompileJob &job);

  static IMTAsyncCompiler *instance;

public:
  IMTAsyncCompiler() {
    // building IR can allocate from the GC heap
    // that has to be enabled from the main thread
    GC_allow_register_threads();
    worker = std::thread(&IMTAsyncCompiler::Run, this);
  }

  // Never destroyed. Joining the worker from a static destructor is
  // unsafe: function-local statics inside LLVM that the worker first
  // touched (AutoUpgrade's Regex objects, for one) are destroyed before
  // it, while the worker may still be using them. ShutdownIMTCompiler
  // stops the worker before exit instead.
  static IMTAsyncCompiler &Instance() {
    if (instance == nullptr) {
      instance = new IMTAsyncCompiler();
    }
    return *instance;
  }
  static IMTAsyncCompiler *InstanceIfStarted() { return instance; }

  // Drops queued jobs, waits for the one being compiled, and stops the
  // worker. Safe to call more than once.
  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
    }
    workAvailable.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
  }

  void Submit(std::unique_ptr<IMTCompileJob> job);
  void InstallFinished();
  bool IsPending(const NomIMTGraph *imtGraph, int imtIndex) const {
    return pendingSlots.count({imtGraph, imtIndex}) > 0;
  }
};

IMTAsyncCompiler *IMTAsyncCompiler::instance = nullptr;

void IMTAsyncCompiler::Run() {
  struct GC_stack_base stackBase;
  GC_get_stack_base(&stackBase);
  if (GC_register_my_thread(&stackBase) != GC_SUCCESS) {
    std::cout << "Could not register the IMT compiler thread with the GC"
              << std::endl;
    std::abort();
  }

  // TargetMachines are not thread-safe so the worker owns its own
  // It is configured like the one LLJIT builds for the main JIT.
  std::unique_ptr<TargetMachine> tm;
  std::string tmError;
  auto jtmb = orc::JITTargetMachineBuilder::detectHost();
  if (!jtmb) {
    tmError = toString(jtmb.takeError());
  } else if (auto tmOrErr = jtmb->createTargetMachine()) {
    tm = std::move(*tmOrErr);
  } else {
    tmError = toString(tmOrErr.takeError());
  }

  while (true) {
    std::unique_ptr<IMTCompileJob> job;
    {
      std::unique_lock<std::mutex> lock(mutex);
      workAvailable.wait(lock, [this] { return stopping || !queued.empty(); });
      if (stopping) {
        GC_unregister_my_thread();
        return;
      }
      job = std::move(queued.front());
      queued.pop_front();
    }
    if (tm) {
      Compile(*job, *tm);
    } else {
      job->error = tmError;
    }
    std::lock_guard<std::mutex> lock(mutex);
    finished.push_back(std::move(job));
    hasFinished.store(true, std::memory_order_release);
  }
}

// Builds the entry's IR in the shared context and serializes it
// Holds the context lock throughout including while the module is destroyed
SmallVector<char, 0> IMTAsyncCompiler::BuildBitcode(IMTCompileJob &job) {
  SmallVector<char, 0> bitcode;
  auto lock = NomJIT::Instance().getContextLock();
  try {
    auto mod = BuildIMTEntryModule(job.nomRecord, job.imtIndex, job.callTag,
                                   job.callTagFunAddr, job.parentCallTags);
    job.name = mod->getModuleIdentifier();
    raw_svector_ostream bitcodeStream(bitcode);
    WriteBitcodeToFile(*mod, bitcodeStream);
  } catch (...) {
    // BuildIMTEntryModule has printed what went wrong
    job.error = "building the IR failed";
  }
  return bitcode;
}

void IMTAsyncCompiler::Compile(IMTCompileJob &job, TargetMachine &tm) {
  auto bitcode = BuildBitcode(job);
  if (!job.error.empty()) {
    return;
  }
  LLVMContext context;
  auto mod = parseBitcodeFile(
      MemoryBufferRef(StringRef(bitcode.data(), bitcode.size()), job.name),
      context);
  if (!mod) {
    job.error = toString(mod.takeError());
    return;
  }
  if (NomOptLevel > 0) {
    OptimizeIMTEntryModule(**mod);
  }
  auto object = orc::SimpleCompiler(tm)(**mod);
  if (!object) {
    job.error = toString(object.takeError());
    return;
  }
  job.object = std::move(*object);
}

void IMTAsyncCompiler::Submit(std::unique_ptr<IMTCompileJob> job) {
  pendingSlots.emplace(job->imtGraph, job->imtIndex);
  {
    std::lock_guard<std::mutex> lock(mutex);
    queued.push_back(std::move(job));
  }
  workAvailable.notify_one();
}

void IMTAsyncCompiler::InstallFinished() {
  // Called on every fallback, so the common case (nothing finished) takes
  // no lock. A stale false only delays installing to the next fallback.
  if (!hasFinished.load(std::memory_order_acquire)) {
    return;
  }
  std::deque<std::unique_ptr<IMTCompileJob>> done;
  {
    std::lock_guard<std::mutex> lock(mutex);
    done.swap(finished);
    hasFinished.store(false, std::memory_order_relaxed);
  }

  auto &jit = NomJIT::Instance();
  for (auto &job : done) {
    pendingSlots.erase({job->imtGraph, job->imtIndex});
    if (!job->error.empty()) {
      std::cout << "Could not compile IMT entry " << job->name << ": "
                << job->error << std::endl;
      throw new std::exception();
    }
    if (auto err = jit.addObjectFile(std::move(job->object))) {
      logAllUnhandledErrors(std::move(err), errs(),
                            "Could not add IMT entry " + job->name + ": ");
      throw new std::exception();
    }
    auto sym = jit.lookup(job->name);
    if (!sym) {
      logAllUnhandledErrors(sym.takeError(), errs(),
                            "Could not find IMT entry " + job->name + ": ");
      throw new std::exception();
    }
    auto newIMTEntry = reinterpret_cast<void *>(sym->getAddress());

    auto newIMTNode = NomIMTNode::CreateTransitionNode(
        job->imtNode, job->callTag, job->callTagFunAddr, newIMTEntry);
    job->imtGraph->GenerateTransitionRelation(job->imtIndex, newIMTNode);

    // std::cout << "Assign new entry " << newIMTEntry << " to slot "
    //           << job->imtIndex << " over oringal entry " << job->imtEntry
    //           << std::endl;
    job->imtArray[job->imtIndex] = newIMTEntry;
  }
}
} // namespace

bool UseAsyncIMTCompile() {
  return RTConfig_AsyncIMTCompile && !NomVerbose && NomDebugPrintLevel < 0 &&
         NomStatsLevel <= 1 && NomFunctionTimingLevel <= 2;
}

void CompileIMTEntryFunctionAsync(NomRecord *nomRecord, NomIMTGraph *imtGraph,
                                  int imtIndex, void *imtEntry,
                                  const NomInterfaceCallTag *callTag,
                                  void *callTagFunAddr, NomIMTNode *imtNode,
                                  void **imtArray) {
  // the parent's call-tags were cached when their entries were built
  CacheRecordMethodAddr(nomRecord, callTag);
  auto job = std::make_unique<IMTCompileJob>(
      nomRecord, imtIndex, imtEntry, callTag, callTagFunAddr,
      imtNode->GetCallTags(), imtGraph, imtNode, imtArray);
  IMTAsyncCompiler::Instance().Submit(std::move(job));
}

void InstallFinishedIMTEntries() {
  IMTAsyncCompiler::Instance().InstallFinished();
}

void ShutdownIMTCompiler() {
  if (auto compiler = IMTAsyncCompiler::InstanceIfStarted()) {
    compiler->Shutdown();
  }
}

bool IsIMTEntryPending(const NomIMTGraph *imtGraph, int imtIndex) {
  return IMTAsyncCompiler::Instance().IsPending(imtGraph, imtIndex);
}
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom
