#!/bin/bash
# Run the GTP benchmark ports in gtp-benchmark-monnom with nombench and check
# that every configuration printed the expected result.
#
# usage: ./run-gtp <test|all> [-r RUNS] [-w WARMUPS] [--chart] [--lattice]
#                  [--compare | --dry-run | --clean] [-- NOM_FLAGS...]
#
#   <test>          a benchmark folder in gtp-benchmark-monnom (e.g. sieve);
#                   `all` runs every benchmark in turn
#   -r RUNS         measured runs per configuration, passed to nombench
#                   (default 1)
#   -w WARMUPS      untimed runs of the program before the measured one, in
#                   the same nom process, passed to nom as -w<WARMUPS>
#                   (default 0); the timeout covers them too
#   --chart         also draw the performance chart with newplot.py, into
#                   charts/gtp/<test>/<test>-<nom flags>.png (<test>-base.png
#                   without flags); only when every configuration passed
#   --lattice       also draw the performance lattice, into
#                   lattice/gtp/<test>/<test>-<nom flags>-lattice.png and .html;
#                   failed configurations are marked on it. With --compare
#                   each mode gets its own, all relative to the fully untyped
#                   configuration of the base mode
#   --compare       collect performance data: nombench generates the
#                   configurations once, then each one runs with nom in two
#                   modes (no flags, --trans --sync; async --trans is off), RUNS times per
#                   mode. Once a benchmark finishes, the running times go to
#                   charts/gtp/raw/<test>/ and make_tables.py composes them
#                   into charts/gtp/tables/<test>.md
#                   and .csv, one row per configuration with the average
#                   time of its runs in both modes,
#                   and charts/gtp/SUMMARY.md, one row per benchmark. With
#                   --chart each mode is also charted, as <test>-base and
#                   <test>-trans-sync
#   --dry-run       only generate the configurations with nombench, do not
#                   run them
#   --clean         delete the generated files (.BM_* configurations, *.mnil,
#                   *.manifest, llvmir*.lli) and exit
#   -- NOM_FLAGS... flags passed to nom for every run, after nombench's
#                   default -o3 (e.g. -- --trans --sync)
#
# For each benchmark the old .BM_* configurations are deleted, nombench
# --byfile generates and runs the new ones, and the result lines of every
# out<N>.txt (all but the timing line) are compared with expected.txt, and
# collect.rkt gathers them into the benchmark's results.csv (what
# scripts/python/newplot.py charts).
# Exits non-zero if any configuration is missing a result or printed a wrong one.
#
# --compare runs are pinned to RUNGTP_CPUS (default 0,1: two P-cores, the
# second for the async IMT compiler) because timings are noisy on this hybrid
# CPU otherwise, and a run is stopped after RUNGTP_TIMEOUT seconds (default 300).
# setpath.sh (and the setvars.sh it sources) overwrite common names such as
# usage, so the functions here are prefixed
RUNGTP_SELF="$(realpath "${BASH_SOURCE[0]}")"
RUNGTP_DIR="$(dirname "$RUNGTP_SELF")"
TESTS_DIR="$RUNGTP_DIR/gtp-benchmark-monnom"
# what make_tables.py reads and writes
PERF_DIR="$RUNGTP_DIR/charts/gtp"
CPUS="${RUNGTP_CPUS:-0,1}"
TIMEOUT="${RUNGTP_TIMEOUT:-300}"

# the benchmark folders (those with a .mnp project file), one per line
rungtp_list_tests() {
  local d
  for d in "$TESTS_DIR"/*/; do
    compgen -G "$d*.mnp" > /dev/null && basename "$d"
  done
}

# prints the names given as arguments in columns, filled top to bottom
rungtp_print_matrix() {
  local names=("$@") n=$# width=0 name
  for name in "${names[@]}"; do
    [ ${#name} -gt $width ] && width=${#name}
  done
  width=$((width + 2))
  local cols=$(( (${COLUMNS:-80} - 2) / width ))
  [ $cols -lt 1 ] && cols=1
  local rows=$(( (n + cols - 1) / cols )) r c i
  for ((r = 0; r < rows; r++)); do
    printf '  '
    for ((c = 0; c < cols; c++)); do
      i=$((c * rows + r))
      [ $i -lt $n ] && printf "%-${width}s" "${names[$i]}"
    done
    printf '\n'
  done
}

rungtp_usage() {
  sed -n '5,39p' "$RUNGTP_SELF" | sed 's/^# \{0,1\}//'
  if [ -d "$TESTS_DIR" ]; then
    echo "available tests:"
    mapfile -t RUNGTP_NAMES < <(rungtp_list_tests)
    rungtp_print_matrix "${RUNGTP_NAMES[@]}"
  fi
  exit 1
}

[ $# -eq 0 ] && rungtp_usage

TEST="$1"
shift
case "$TEST" in
  -h|--help) rungtp_usage ;;
  -*) echo "ERROR: the first argument must be a test name or all"; rungtp_usage ;;
esac
RUNS=1
RUNS_GIVEN=0
WARMUPS=0
WARMUPS_GIVEN=0
DRYRUN=0
CLEAN=0
PLOT=0
LATTICE=0
COMPARE=0
FORWARD=()
while [ $# -gt 0 ]; do
  case "$1" in
    -r) if [ -z "$2" ] || ! [[ "$2" =~ ^[0-9]+$ ]] || [ "$2" -lt 1 ]; then
          echo "ERROR: -r needs a positive number"; rungtp_usage
        fi
        RUNS="$2"; RUNS_GIVEN=1; shift 2 ;;
    -w) if [ -z "$2" ] || ! [[ "$2" =~ ^[0-9]+$ ]]; then
          echo "ERROR: -w needs a number"; rungtp_usage
        fi
        WARMUPS="$2"; WARMUPS_GIVEN=1; shift 2 ;;
    --chart) PLOT=1; shift ;;
    --lattice) LATTICE=1; shift ;;
    --compare) COMPARE=1; shift ;;
    --dry-run) DRYRUN=1; shift ;;
    --clean) CLEAN=1; shift ;;
    --) shift; FORWARD=("$@"); break ;;
    *) echo "ERROR: unexpected argument $1"; rungtp_usage ;;
  esac
done
if [ $DRYRUN -eq 1 ] && [ $CLEAN -eq 1 ]; then
  echo "ERROR: --dry-run and --clean cannot be combined"; rungtp_usage
fi
if { [ $DRYRUN -eq 1 ] || [ $CLEAN -eq 1 ]; } &&
   { [ $RUNS_GIVEN -eq 1 ] || [ $WARMUPS_GIVEN -eq 1 ] || [ $PLOT -eq 1 ] ||
     [ $LATTICE -eq 1 ] ||
     [ $COMPARE -eq 1 ] || [ ${#FORWARD[@]} -gt 0 ]; }; then
  echo "ERROR: -r, -w, --chart, --lattice, --compare and nom flags only apply" \
       "when the configurations are run"
  rungtp_usage
fi

# nom's warmup flag, for every run; not part of NOMFLAGS, so the chart and
# lattice names stay the same
WARMUP=()
[ "$WARMUPS" -gt 0 ] && WARMUP=("-w$WARMUPS")

if [ $CLEAN -eq 0 ]; then
  # setpath.sh puts nom, nomc and nombench on PATH
  . "$RUNGTP_DIR/setpath.sh" > /dev/null 2>&1
  for tool in nom nomc nombench; do
    if ! command -v "$tool" > /dev/null 2>&1; then
      echo "ERROR: $tool not found (build the runtime and run ./build.sh first)"
      exit 1
    fi
  done

  # --compare pins every run to $CPUS; if this shell may not use them (e.g.
  # a cpuset left behind by bench-cpus.sh) every run would fail, so stop now
  if [ $COMPARE -eq 1 ] && ! taskset -c "$CPUS" true 2> /dev/null; then
    echo "ERROR: cannot pin to CPUs $CPUS: this shell may only use CPUs" \
         "$(sed -n 's/^Cpus_allowed_list:\s*//p' /proc/self/status)"
    echo "  set RUNGTP_CPUS to CPUs it may use, or give the CPUs back with" \
         "./bench-cpus.sh --restore (see ./bench-cpus.sh --status)"
    exit 1
  fi

  # the ports are a git submodule; fetch it if it has not been checked out
  if [ ! -d "$TESTS_DIR" ] || [ -z "$(ls -A "$TESTS_DIR" 2> /dev/null)" ]; then
    echo "== gtp-benchmark-monnom missing, updating the submodule"
    if ! git -C "$RUNGTP_DIR" submodule update --init gtp-benchmark-monnom; then
      echo "ERROR: could not check out the gtp-benchmark-monnom submodule"
      exit 1
    fi
  fi
fi

if [ "$TEST" = all ]; then
  mapfile -t TESTS < <(rungtp_list_tests)
else
  if ! compgen -G "$TESTS_DIR/$TEST/*.mnp" > /dev/null; then
    echo "ERROR: no benchmark named $TEST in $TESTS_DIR"
    rungtp_usage
  fi
  TESTS=("$TEST")
fi

if [ $CLEAN -eq 1 ]; then
  for B in "${TESTS[@]}"; do
    D="$TESTS_DIR/$B"
    echo "== removing generated files in $D"
    rm -rf "$D"/.BM_*
    rm -f "$D"/*.mnil "$D"/*.manifest "$D"/llvmir*.lli
  done
  exit 0
fi

# the chart file name for benchmark $1: the benchmark followed by the nom
# flags of the current run (NOMFLAGS) without their leading dashes, e.g.
# sieve-trans-sync.png
rungtp_chart_name() {
  local NAME="$1" A
  [ ${#NOMFLAGS[@]} -eq 0 ] && NAME+="-base"
  for A in "${NOMFLAGS[@]}"; do
    A="${A#-}"; A="${A#-}"
    NAME+="-${A//[^A-Za-z0-9._=+]/_}"
  done
  echo "$NAME.png"
}

# checks run $2 of configuration folder $1 against $3 (expected.txt) and
# prints its status; $4 is nom's exit status, if known
rungtp_check_run() {
  local OUT="$1/out$2.txt"
  if [ "${4:-0}" -eq 124 ]; then
    echo "FAIL (timed out after ${TIMEOUT}s)"
  elif [ "${4:-0}" -ne 0 ]; then
    echo "FAIL (exit status $4)"
  elif [ ! -s "$OUT" ]; then
    echo "FAIL (no output)"
  elif ! diff -q <(tail -n +3 "$OUT") "$3" > /dev/null; then
    echo "FAIL (wrong result)"
  else
    echo "OK ($(head -1 "$OUT"))"
  fi
}

# passes nombench's output through as it comes, minus its noise; each
# "Run R/N: .BM_X(i/n)" line gets the status of that run once nom has
# finished it, i.e. when the next line arrives
rungtp_follow_nombench() {
  local D="$1" LINE PCONF="" PRUN="" OPEN=0 SHOWN=0 CONFS
  while IFS= read -r LINE; do
    [[ "$LINE" == *'DONE!' || "$LINE" == '   at '* ]] && continue
    if [[ "$LINE" =~ ^Run\ ([0-9]+)(/[0-9]+)?:\ (\.BM_[^\(]*)\( ]]; then
      # generation is over once the first run starts
      if [ $SHOWN -eq 0 ]; then
        CONFS=("$D"/.BM_*/)
        rungtp_announce "${#CONFS[@]} configurations, run by nombench"
        SHOWN=1
      fi
      if [ -n "$PCONF" ]; then
        [ $OPEN -eq 0 ] && printf '  ->'
        echo " $(rungtp_check_run "$PCONF" "$PRUN" "$D/expected.txt")"
      fi
      PCONF="$D/${BASH_REMATCH[3]}"; PRUN="${BASH_REMATCH[1]}"
      printf '%s ...' "$LINE"
      OPEN=1
      continue
    fi
    # nombench prints Done! once the last run has finished
    if [ "$LINE" = 'Done!' ] && [ -n "$PCONF" ]; then
      [ $OPEN -eq 0 ] && printf '  ->'
      echo " $(rungtp_check_run "$PCONF" "$PRUN" "$D/expected.txt")"
      PCONF=""; OPEN=0
    fi
    # other output (e.g. a crash message from nom) goes on its own line
    [ $OPEN -eq 1 ] && { echo; OPEN=0; }
    echo "$LINE"
  done
  if [ -n "$PCONF" ]; then
    [ $OPEN -eq 0 ] && printf '  ->'
    echo " $(rungtp_check_run "$PCONF" "$PRUN" "$D/expected.txt")"
  fi
}

# prints the --compare header (if any) and the nom command line of the
# current run
rungtp_announce() {
  [ -n "$HEADER" ] && echo "$HEADER"
  local FLAGS=("${WARMUP[@]}" "${NOMFLAGS[@]}")
  echo "== $LABEL: nom -o3${FLAGS[*]:+ ${FLAGS[*]}} $P ($1)"
}

# runs every generated configuration of $D with nom directly, the way
# nombench does (-o3, then the flags, stdout to out<R>.txt), printing each
# run's status as it finishes. Unlike nombench this sees nom's exit status:
# failed configurations go in NOMFAILED, and each run is added to
# $RAW_TMP/<test>/<configuration>.p<R>.csv as
#   configuration,mode,pass,exit,ok,seconds
# the format make_tables.py reads from raw/<test>/
rungtp_run_configs() {
  local D="$1" B="$2" CONFS=("$1"/.BM_*/) R N CONF ERR RC NAME CHECK OK SECS
  rungtp_announce "${#CONFS[@]} configurations, pinned to CPUs $CPUS"
  rm -f "$D"/.BM_*/out*.txt
  ERR="$(mktemp)"
  for ((R = 1; R <= RUNS; R++)); do
    N=0
    for CONF in "${CONFS[@]}"; do
      CONF="${CONF%/}"
      N=$((N + 1))
      printf 'Run %s/%s: %s(%s/%s) ...' "$R" "$RUNS" "$(basename "$CONF")" \
        "$N" "${#CONFS[@]}"
      ( cd "$CONF" && timeout "$TIMEOUT" taskset -c "$CPUS" \
          nom -p "$CONF" -o3 "${WARMUP[@]}" "${NOMFLAGS[@]}" "$P" > "out$R.txt" 2> "$ERR" )
      RC=$?
      # anything nom printed to stderr (e.g. a crash) goes on its own lines
      if [ -s "$ERR" ]; then
        echo; cat "$ERR"; printf '  ->'
      fi
      CHECK="$(rungtp_check_run "$CONF" "$R" "$D/expected.txt" "$RC")"
      echo " $CHECK"
      NAME="$(basename "$CONF")"
      NAME="${NAME#.BM_}"
      OK=0 SECS=""
      if [[ "$CHECK" == OK* ]]; then
        OK=1
        SECS="$(head -1 "$CONF/out$R.txt" | cut -d' ' -f1)"
      else
        NOMFAILED[$NAME]=1
      fi
      echo "$NAME,$VARIANT,$R,$RC,$OK,$SECS" >> "$RAW_TMP/$B/$NAME.p$R.csv"
    done
  done
  rm -f "$ERR"
}

# runs one benchmark with the nom flags NOMFLAGS and checks its results:
# with GENERATE=1 nombench generates the configurations, and runs them too
# unless this is --compare, whose runs all go through rungtp_run_configs; sets
# RESULT to a one-line summary
rungtp_run_test() {
  local B="$1"
  local D="$TESTS_DIR/$B"
  local MNPS=("$D"/*.mnp)
  local P
  P="$(basename "${MNPS[0]}" .mnp)"
  if [ ! -f "$D/expected.txt" ]; then
    RESULT="$LABEL: ERROR: $D/expected.txt is missing"
    echo "$RESULT"
    return 1
  fi
  local STATUS=0
  NOMFAILED=()
  if [ $GENERATE -eq 1 ]; then
    # without -r nombench only generates the configurations
    local ARGS=(--path "$D" --project "$P" --byfile)
    if [ $DRYRUN -eq 0 ] && [ $COMPARE -eq 0 ]; then
      ARGS+=(-r "$RUNS")
      # --forward must come last: nombench skips the argument following it
      local FLAGS=("${WARMUP[@]}" "${NOMFLAGS[@]}")
      [ ${#FLAGS[@]} -gt 0 ] && ARGS+=(--forward ${#FLAGS[@]} "${FLAGS[@]}")
    fi
    echo "== $LABEL: nombench ${ARGS[*]}"
    rm -rf "$D"/.BM_*
    nombench "${ARGS[@]}" 2>&1 | rungtp_follow_nombench "$D"
    STATUS=${PIPESTATUS[0]}
  fi
  if [ $COMPARE -eq 1 ] && compgen -G "$D/.BM_*/" > /dev/null; then
    rungtp_run_configs "$D" "$B"
  fi

  local CONFS=("$D"/.BM_*/)
  if [ ! -d "${CONFS[0]}" ]; then
    RESULT="$LABEL: ERROR: nombench generated no configurations (exit $STATUS)"
    echo "$RESULT"
    return 1
  fi
  if [ $DRYRUN -eq 1 ]; then
    RESULT="$LABEL: generated ${#CONFS[@]} configurations (not run)"
    echo "$RESULT"
    return 0
  fi

  local ok=0 bad=() CONF R good NAME
  for CONF in "${CONFS[@]}"; do
    good=1
    for R in $(seq 1 "$RUNS"); do
      if [ ! -f "$CONF/out$R.txt" ] ||
         ! diff -q <(tail -n +3 "$CONF/out$R.txt") "$D/expected.txt" > /dev/null; then
        good=0
      fi
    done
    NAME="$(basename "$CONF")"
    [ -n "${NOMFAILED[${NAME#.BM_}]}" ] && good=0
    if [ $good -eq 1 ]; then ok=$((ok + 1)); else bad+=("$NAME"); fi
  done
  RESULT="$LABEL: $ok/${#CONFS[@]} configurations print the expected result ($RUNS run(s) each)"
  echo "$RESULT"

  # collect.sh only accepts folders under experiments/, so call collect.rkt
  # directly; it gathers every .BM_*/out*.txt into results.csv
  echo "== $LABEL: collecting results into $D/results.csv"
  if ! racket -e "(require (file \"$RUNGTP_DIR/collect.rkt\")) (run \"$D\")" ||
     [ ! -s "$D/results.csv" ]; then
    RESULT+=", collecting results.csv failed"
    echo "$LABEL: ERROR: collecting results.csv failed"
    return 1
  fi

  local OUTDIR="$RUNGTP_DIR/charts/gtp/$B" LATTICE_OK=1
  # before the failure check: a long run's lattice is worth having even if
  # a few configurations failed
  if [ $LATTICE -eq 1 ]; then
    NAME="$(rungtp_chart_name "$B")"
    NAME="${NAME%.png}-lattice.png"
    mkdir -p "$RUNGTP_DIR/lattice/gtp/$B"
    echo "== $LABEL: drawing the lattice into $RUNGTP_DIR/lattice/gtp/$B/$NAME"
    # base runs first; the other modes are measured against its time
    local BASELINE=()
    [ "$VARIANT" = base ] && BASELINE=(--save-baseline "$RAW_TMP/$B.baseline")
    [ -n "$VARIANT" ] && [ "$VARIANT" != base ] &&
      BASELINE=(--baseline "$RAW_TMP/$B.baseline")
    if python3 "$RUNGTP_DIR/scripts/python/lattice.py" \
         "$RUNGTP_DIR/lattice/gtp/$B/$NAME" "$D" \
         "${BASELINE[@]}" --failed "${bad[@]#.BM_}"; then
      RESULT+=", lattice/gtp/$B/$NAME"
    else
      RESULT+=", drawing the lattice failed"
      echo "$LABEL: ERROR: drawing the lattice failed"
      LATTICE_OK=0
    fi
  fi

  if [ ${#bad[@]} -gt 0 ]; then
    echo "$LABEL: failing configurations:"
    printf '  %s\n' "${bad[@]:0:20}"
    [ ${#bad[@]} -gt 20 ] && echo "  ... and $(( ${#bad[@]} - 20 )) more"
    # newplot.py refuses results that differ between configurations
    [ $PLOT -eq 1 ] && echo "$LABEL: no chart, since not every configuration passed"
    return 1
  fi

  if [ $PLOT -eq 1 ]; then
    NAME="$(rungtp_chart_name "$B")"
    mkdir -p "$OUTDIR"
    echo "== $LABEL: charting into $OUTDIR/$NAME"
    # run from OUTDIR so the chart title stays the bare name
    if ! ( cd "$OUTDIR" &&
           python3 "$RUNGTP_DIR/scripts/python/newplot.py" "$NAME" "$D" monnom ); then
      RESULT+=", charting failed"
      echo "$LABEL: ERROR: charting failed"
      return 1
    fi
    RESULT+=", chart charts/gtp/$B/$NAME"
  fi
  [ $LATTICE_OK -eq 1 ]
}

# runtime flags of each --compare variant
rungtp_variant_flags() {
  case "$1" in
    base) FLAGS=() ;;
    trans) FLAGS=(--trans) ;;
    trans-sync) FLAGS=(--trans --sync) ;;
  esac
}

declare -A NOMFAILED
# --compare runs can take hours: the times stay here until a benchmark is
# done, so losing charts/ meanwhile is harmless, and stopping a run leaves
# the benchmark's previous measurement in place
RAW_TMP="$(mktemp -d)"
trap 'rm -rf "$RAW_TMP"' EXIT
SUMMARY=()
FAILED=0
for B in "${TESTS[@]}"; do
  if [ $COMPARE -eq 0 ]; then
    RESULT="" LABEL="$B" HEADER="" GENERATE=1 NOMFLAGS=("${FORWARD[@]}") VARIANT=""
    if ! rungtp_run_test "$B"; then FAILED=1; fi
    SUMMARY+=("$RESULT")
    continue
  fi
  mkdir -p "$RAW_TMP/$B"
  # the async --trans mode is commented out for now, as in the runtime
  # for VARIANT in base trans trans-sync; do
  for VARIANT in base trans-sync; do
    rungtp_variant_flags "$VARIANT"
    NOMFLAGS=("${FLAGS[@]}" "${FORWARD[@]}")
    RESULT="" LABEL="$B [$VARIANT]"
    HEADER="==== $B $VARIANT${NOMFLAGS[*]:+: ${NOMFLAGS[*]}}"
    # the first variant generates the configurations, the others reuse them
    GENERATE=0
    [ "$VARIANT" = base ] && GENERATE=1
    if ! rungtp_run_test "$B"; then
      FAILED=1
      SUMMARY+=("$RESULT")
      # without generated configurations the other variants cannot run
      compgen -G "$TESTS_DIR/$B/.BM_*/" > /dev/null || break
    else
      SUMMARY+=("$RESULT")
    fi
  done
  # a new measurement replaces this benchmark's old one
  rm -rf "$PERF_DIR/raw/$B"
  mkdir -p "$PERF_DIR/raw"
  mv "$RAW_TMP/$B" "$PERF_DIR/raw/$B"
  # rebuilt after each benchmark, so a stopped `all` keeps what it finished
  echo "== $B: composing the running times into $PERF_DIR/tables/$B.md"
  if python3 "$RUNGTP_DIR/scripts/python/make_tables.py" "$PERF_DIR" --mean; then
    SUMMARY+=("$B: table charts/gtp/tables/$B.md, all benchmarks in charts/gtp/SUMMARY.md")
  else
    echo "$B: ERROR: make_tables.py failed"
    SUMMARY+=("$B: ERROR: make_tables.py failed")
    FAILED=1
  fi
done
if [ ${#SUMMARY[@]} -gt 1 ]; then
  echo "== summary"
  printf '%s\n' "${SUMMARY[@]}"
fi
exit $FAILED
