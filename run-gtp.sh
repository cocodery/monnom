#!/bin/bash
# Run the GTP benchmark ports in gtp-benchmark-monnom with nombench and check
# that every configuration printed the expected result.
#
# usage: ./run-gtp <test|all> [-r RUNS] [-p] [--compare | --dry-run | --clean]
#                  [-- NOM_FLAGS...]
#
#   <test>          a benchmark folder in gtp-benchmark-monnom (e.g. sieve);
#                   `all` runs every benchmark in turn
#   -r RUNS         measured runs per configuration, passed to nombench
#                   (default 1)
#   -p              also chart the running times, into
#                   charts/gtp/<test>/<test>-<nom flags>.png (<test>-base.png
#                   without flags); only when every configuration passed
#   --compare       run each benchmark three times: no flags, --trans,
#                   --trans --sync (charted as <test>-base, <test>-trans,
#                   <test>-trans-sync); nombench only generates the
#                   configurations for the first, the others rerun them with
#                   nom directly
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
# setpath.sh (and the setvars.sh it sources) overwrite common names such as
# usage, so the functions here are prefixed
RUNGTP_SELF="$(realpath "${BASH_SOURCE[0]}")"
RUNGTP_DIR="$(dirname "$RUNGTP_SELF")"
TESTS_DIR="$RUNGTP_DIR/gtp-benchmark-monnom"

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
  sed -n '5,26p' "$RUNGTP_SELF" | sed 's/^# \{0,1\}//'
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
DRYRUN=0
CLEAN=0
PLOT=0
COMPARE=0
FORWARD=()
while [ $# -gt 0 ]; do
  case "$1" in
    -r) if [ -z "$2" ] || ! [[ "$2" =~ ^[0-9]+$ ]] || [ "$2" -lt 1 ]; then
          echo "ERROR: -r needs a positive number"; rungtp_usage
        fi
        RUNS="$2"; RUNS_GIVEN=1; shift 2 ;;
    -p) PLOT=1; shift ;;
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
   { [ $RUNS_GIVEN -eq 1 ] || [ $PLOT -eq 1 ] || [ $COMPARE -eq 1 ] ||
     [ ${#FORWARD[@]} -gt 0 ]; }; then
  echo "ERROR: -r, -p, --compare and nom flags only apply when the" \
       "configurations are run"
  rungtp_usage
fi

if [ $CLEAN -eq 0 ]; then
  # setpath.sh puts nom, nomc and nombench on PATH
  . "$RUNGTP_DIR/setpath.sh" > /dev/null 2>&1
  for tool in nom nomc nombench; do
    if ! command -v "$tool" > /dev/null 2>&1; then
      echo "ERROR: $tool not found (build the runtime and run ./build.sh first)"
      exit 1
    fi
  done

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
# prints its status
rungtp_check_run() {
  local OUT="$1/out$2.txt"
  if [ ! -s "$OUT" ]; then
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
  echo "== $LABEL: nom -o3${NOMFLAGS[*]:+ ${NOMFLAGS[*]}} $P ($1)"
}

# reruns every generated configuration of $D with nom directly, the way
# nombench does (-o3, then the flags, stdout to out<R>.txt), printing each
# run's status as it finishes
rungtp_rerun_configs() {
  local D="$1" CONFS=("$1"/.BM_*/) R N CONF ERR
  rungtp_announce "${#CONFS[@]} configurations, generator skipped"
  rm -f "$D"/.BM_*/out*.txt
  ERR="$(mktemp)"
  for ((R = 1; R <= RUNS; R++)); do
    N=0
    for CONF in "${CONFS[@]}"; do
      CONF="${CONF%/}"
      N=$((N + 1))
      printf 'Run %s/%s: %s(%s/%s) ...' "$R" "$RUNS" "$(basename "$CONF")" \
        "$N" "${#CONFS[@]}"
      ( cd "$CONF" && nom -p "$CONF" -o3 "${NOMFLAGS[@]}" "$P" \
          > "out$R.txt" 2> "$ERR" )
      # anything nom printed to stderr (e.g. a crash) goes on its own lines
      if [ -s "$ERR" ]; then
        echo; cat "$ERR"; printf '  ->'
      fi
      echo " $(rungtp_check_run "$CONF" "$R" "$D/expected.txt")"
    done
  done
  rm -f "$ERR"
}

# runs one benchmark with the nom flags NOMFLAGS and checks its results:
# with GENERATE=1 nombench generates and runs the configurations, otherwise
# the existing ones are rerun; sets RESULT to a one-line summary
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
  if [ $GENERATE -eq 1 ]; then
    # without -r nombench only generates the configurations
    local ARGS=(--path "$D" --project "$P" --byfile)
    [ $DRYRUN -eq 0 ] && ARGS+=(-r "$RUNS")
    # --forward must come last: nombench skips the argument following it
    [ ${#NOMFLAGS[@]} -gt 0 ] &&
      ARGS+=(--forward ${#NOMFLAGS[@]} "${NOMFLAGS[@]}")
    echo "== $LABEL: nombench ${ARGS[*]}"
    rm -rf "$D"/.BM_*
    nombench "${ARGS[@]}" 2>&1 | rungtp_follow_nombench "$D"
    STATUS=${PIPESTATUS[0]}
  elif compgen -G "$D/.BM_*/" > /dev/null; then
    rungtp_rerun_configs "$D"
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

  local ok=0 bad=() CONF R good
  for CONF in "${CONFS[@]}"; do
    good=1
    for R in $(seq 1 "$RUNS"); do
      if [ ! -f "$CONF/out$R.txt" ] ||
         ! diff -q <(tail -n +3 "$CONF/out$R.txt") "$D/expected.txt" > /dev/null; then
        good=0
      fi
    done
    if [ $good -eq 1 ]; then ok=$((ok + 1)); else bad+=("$(basename "$CONF")"); fi
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

  if [ ${#bad[@]} -gt 0 ]; then
    echo "$LABEL: failing configurations:"
    printf '  %s\n' "${bad[@]:0:20}"
    [ ${#bad[@]} -gt 20 ] && echo "  ... and $(( ${#bad[@]} - 20 )) more"
    # newplot.py refuses results that differ between configurations
    [ $PLOT -eq 1 ] && echo "$LABEL: no chart, since not every configuration passed"
    return 1
  fi

  if [ $PLOT -eq 1 ]; then
    local OUTDIR="$RUNGTP_DIR/charts/gtp/$B" NAME
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
  return 0
}

# runtime flags of each --compare variant
rungtp_variant_flags() {
  case "$1" in
    base) FLAGS=() ;;
    trans) FLAGS=(--trans) ;;
    trans-sync) FLAGS=(--trans --sync) ;;
  esac
}

SUMMARY=()
FAILED=0
for B in "${TESTS[@]}"; do
  if [ $COMPARE -eq 0 ]; then
    RESULT="" LABEL="$B" HEADER="" GENERATE=1 NOMFLAGS=("${FORWARD[@]}")
    if ! rungtp_run_test "$B"; then FAILED=1; fi
    SUMMARY+=("$RESULT")
    continue
  fi
  for VARIANT in base trans trans-sync; do
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
done
if [ ${#SUMMARY[@]} -gt 1 ]; then
  echo "== summary"
  printf '%s\n' "${SUMMARY[@]}"
fi
exit $FAILED
