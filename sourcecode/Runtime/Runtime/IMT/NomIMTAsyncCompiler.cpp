#include "NomIMTAsyncCompiler.h"
#include "NomIMTUtils.h"
#include "NomJIT.h"
#include "RTConfig.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar.h"
#include <condition_variable>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>

using namespace llvm;
namespace Nom {
namespace Runtime {
namespace IMTUtils {
namespace {
struct IMTCompileJob {
  std::string name;
  SmallVector<char, 0> bitcode;         // input: the entry module
  std::unique_ptr<MemoryBuffer> object; // output: the compiled object
  std::string error;                    // output: set if compiling failed

  NomIMTGraph *imtGraph;
  int imtIndex;
  const NomInterfaceCallTag *callTag;
  void *callTagFunAddr;
  NomIMTNode *imtNode; // the node the new entry extends
  void **imtArray;
};

// Optimizes and code-generates IMT entries on a worker thread. The main
// thread still builds each entry's IR, since that needs the shared
// LLVMCONTEXT, but hands it over as bitcode; the worker parses it into a
// private context, so the two threads never share LLVM state.
class IMTAsyncCompiler {
private:
  std::mutex mutex; // guards queued, finished and stopping
  std::condition_variable workAvailable;
  std::deque<std::unique_ptr<IMTCompileJob>> queued;
  std::deque<std::unique_ptr<IMTCompileJob>> finished;
  bool stopping = false;

  // main thread only: slots with a job queued, compiling or finished but
  // not yet installed
  std::set<std::pair<const NomIMTGraph *, int>> pendingSlots;

  // declared last so everything the worker uses exists before it starts
  std::thread worker;

  void Run();
  static void Optimize(Module &mod);
  static void Compile(IMTCompileJob &job, TargetMachine &tm);

  static IMTAsyncCompiler *instance;

public:
  IMTAsyncCompiler() : worker(&IMTAsyncCompiler::Run, this) {}

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
  // TargetMachines are not thread-safe, so the worker owns its own. It is
  // configured like the one LLJIT builds for the main JIT.
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
  }
}

// The core of NomJIT::optimizeModule. Its debug and profiling
// instrumentation is left out; UseAsyncIMTCompile turns this path off
// when any of it is enabled.
void IMTAsyncCompiler::Optimize(Module &mod) {
  PassManagerBuilder pmb;
  pmb.OptLevel = NomOptLevel;
  pmb.Inliner = createFunctionInliningPass();
  legacy::FunctionPassManager fpm(&mod);
  legacy::PassManager mpm;
  pmb.populateFunctionPassManager(fpm);
  pmb.populateModulePassManager(mpm);
  fpm.add(createTailCallEliminationPass());
  if (RTConfig_AdditionalOptPasses) {
    PassManagerBuilder pmb2;
    pmb2.OptLevel = 2;
    pmb2.populateFunctionPassManager(fpm);
    pmb2.populateModulePassManager(mpm);
    fpm.add(createTailCallEliminationPass());
  }

  fpm.doInitialization();
  for (auto &fun : mod) {
    fpm.run(fun);
  }
  fpm.doFinalization();
  mpm.run(mod);
}

void IMTAsyncCompiler::Compile(IMTCompileJob &job, TargetMachine &tm) {
  LLVMContext context;
  auto mod = parseBitcodeFile(
      MemoryBufferRef(StringRef(job.bitcode.data(), job.bitcode.size()),
                      job.name),
      context);
  if (!mod) {
    job.error = toString(mod.takeError());
    return;
  }
  if (NomOptLevel > 0) {
    Optimize(**mod);
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
  std::deque<std::unique_ptr<IMTCompileJob>> done;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (finished.empty()) {
      return;
    }
    done.swap(finished);
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
    // Only one job per slot is in flight, and the bridge leaves a pending
    // slot alone, so the slot still holds the entry this job extends.
    job->imtArray[job->imtIndex] = newIMTEntry;
  }
}
} // namespace

bool UseAsyncIMTCompile() {
  return RTConfig_AsyncIMTCompile && !NomVerbose && NomDebugPrintLevel < 0 &&
         NomStatsLevel <= 1 && NomFunctionTimingLevel <= 2;
}

void CompileIMTEntryFunctionAsync(NomRecord *nomRecord, NomIMTGraph *imtGraph,
                                  int imtIndex,
                                  const NomInterfaceCallTag *callTag,
                                  void *callTagFunAddr, NomIMTNode *imtNode,
                                  void **imtArray) {
  auto mod = BuildIMTEntryModule(nomRecord, imtIndex, callTag, callTagFunAddr,
                                 imtNode);

  auto job = std::make_unique<IMTCompileJob>();
  job->name = mod->getModuleIdentifier();
  raw_svector_ostream bitcodeStream(job->bitcode);
  WriteBitcodeToFile(*mod, bitcodeStream);
  job->imtGraph = imtGraph;
  job->imtIndex = imtIndex;
  job->callTag = callTag;
  job->callTagFunAddr = callTagFunAddr;
  job->imtNode = imtNode;
  job->imtArray = imtArray;

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
