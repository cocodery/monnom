#!/bin/bash
# Run one MonNom benchmark from experiments/monnom and chart its results.
#
# usage: ./run-benchmark.sh <test> [name|CONFIG] [-o DIR] [-r RUNS]
#                           [--plot-only | --no-gen | --compare]
#                           [-- RUNTIME_FLAGS...]
#
#   <test>          benchmark folder in experiments/monnom (sort, sieve, float)
#   [name]          output chart name, .png optional (default <test>)
#   [CONFIG]        a generated configuration (e.g. IISSSCC): run only
#                   <test>/.BM_<CONFIG> and print its output, no chart
#   -o DIR          directory to write the chart to (default $MONNOMBASE)
#   -r RUNS         measured runs per configuration (default 1)
#   --plot-only     skip running, re-chart the existing results.csv
#   --no-gen        skip the benchmark generator, rerun the existing
#                   .BM_* configurations with nom directly
#   --compare       run three times: no flags, --trans, --trans --sync,
#                   writing <name>-base, <name>-trans, <name>-trans-sync
#                   (the generator only runs for the first)
#   -- FLAGS...     extra flags passed to the nom runtime, after the default
#                   -o3 so they override it (e.g. --trans, -o1, -w2)
#
# The chart is written to <DIR>/<name>.png, as make_charts_png.sh
# does (kaleido needs Chrome; install it once with plotly_get_chrome).
SOURCE="$( realpath "${BASH_SOURCE[0]}" )"
DIRNAME="$( dirname "$SOURCE" )"

usage() {
  sed -n '4,21p' "$SOURCE" | sed 's/^# \{0,1\}//'
  echo "available tests:"
  for d in "$DIRNAME"/experiments/monnom/*/; do
    [ -f "$d/plotconfig.json" ] && echo "  $(basename "$d")"
  done
  exit 1
}

# runtime flags of each --compare variant
COMPARE_VARIANTS=(base trans trans-sync)
variant_flags() {
  case "$1" in
    base) FLAGS=() ;;
    trans) FLAGS=(--trans) ;;
    trans-sync) FLAGS=(--trans --sync) ;;
  esac
}

TEST=""
RUNS=1
OUTNAME=""
OUTDIR=""
PLOTONLY=0
NOGEN=0
COMPARE=0
FORWARD=()
while [ $# -gt 0 ]; do
  case "$1" in
    -r) RUNS="$2"; shift 2 ;;
    -o) OUTDIR="$2"; shift 2 ;;
    --plot-only) PLOTONLY=1; shift ;;
    --no-gen) NOGEN=1; shift ;;
    --compare) COMPARE=1; shift ;;
    -h|--help) usage ;;
    --) shift; FORWARD=("$@"); break ;;
    -*) echo "ERROR: unknown option $1"; usage ;;
    *) if [ -z "$TEST" ]; then TEST="$1"
       elif [ -z "$OUTNAME" ]; then OUTNAME="$1"
       else echo "ERROR: unexpected argument $1"; usage
       fi
       shift ;;
  esac
done
[ -z "$TEST" ] && usage

# a second argument naming a generated configuration selects single mode;
# an all-capitals name that matches none is most likely a typo of one
CONFIG=""
if [ -n "$OUTNAME" ] && \
   [ -d "$DIRNAME/experiments/monnom/$TEST/.BM_$OUTNAME" ]; then
  CONFIG="$OUTNAME"
elif [[ "$OUTNAME" =~ ^[A-Z]+$ ]]; then
  echo "ERROR: $TEST has no configuration .BM_$OUTNAME" \
       "(run ./run-benchmark.sh $TEST once to generate them)"
  exit 1
fi
if [ -n "$CONFIG" ] && { [ $PLOTONLY -eq 1 ] || [ -n "$OUTDIR" ]; }; then
  echo "ERROR: -o and --plot-only make no sense for a single configuration"
  exit 1
fi

OUTNAME="${OUTNAME:-$TEST}"
OUTNAME="${OUTNAME%.png}.png"
# resolve -o against the caller's cwd before setpath.sh/cd change things
if [ -n "$OUTDIR" ]; then
  mkdir -p "$OUTDIR" || { echo "ERROR: cannot create $OUTDIR"; exit 1; }
  OUTDIR="$( realpath "$OUTDIR" )"
fi

. "$DIRNAME/setpath.sh" > /dev/null
BENCHDIR="$MONNOMBASE/experiments/monnom/$TEST"
if [ ! -f "$BENCHDIR/plotconfig.json" ]; then
  echo "ERROR: $BENCHDIR has no plotconfig.json (not a chartable benchmark)"
  usage
fi
MNPS=( "$BENCHDIR"/*.mnp )
if [ ${#MNPS[@]} -ne 1 ] || [ ! -f "${MNPS[0]}" ]; then
  echo "ERROR: expected exactly one .mnp project file in $BENCHDIR"
  exit 1
fi
PROJECT="$( basename "${MNPS[0]}" .mnp )"

# the nom command line nombench runs in each configuration
# (BenchmarkGenerator.cs: default -o3, forwarded flags after it)
NOMARGS=( -o3 )

if [ -n "$CONFIG" ]; then
  # runs the configuration RUNS times with the given runtime flags,
  # printing nom's output instead of writing out*.txt, so the results of
  # the last full run stay intact
  run_config() {
    local RUN STATUS=0 RC
    echo "== nom ${NOMARGS[*]}${*:+ $*} $PROJECT (configuration $CONFIG)"
    for (( RUN = 1; RUN <= RUNS; RUN++ )); do
      [ "$RUNS" -gt 1 ] && echo "Run $RUN/$RUNS:"
      ( cd "$BENCHDIR/.BM_$CONFIG" && \
        nom -p "$BENCHDIR/.BM_$CONFIG" "${NOMARGS[@]}" "$@" "$PROJECT" )
      RC=$?
      [ $RC -ne 0 ] && { echo "ERROR: nom exited with status $RC"; STATUS=1; }
    done
    return $STATUS
  }
  if [ $COMPARE -eq 0 ]; then
    run_config "${FORWARD[@]}"
    exit
  fi
  FAILED=()
  for VARIANT in "${COMPARE_VARIANTS[@]}"; do
    variant_flags "$VARIANT"
    ALL=( "${FLAGS[@]}" "${FORWARD[@]}" )
    echo "==== $VARIANT${ALL[*]:+: ${ALL[*]}}"
    run_config "${ALL[@]}" || FAILED+=("$VARIANT")
  done
  if [ ${#FAILED[@]} -gt 0 ]; then
    echo "ERROR: failed variants: ${FAILED[*]}"
    exit 1
  fi
  exit 0
fi

if [ $COMPARE -eq 1 ]; then
  # every run overwrites results.csv, so each variant is run and charted
  # by its own invocation before the next one starts
  # the first variant generates the .BM_* configurations, the others
  # reuse them (--no-gen)
  if [ $PLOTONLY -eq 1 ] || [ $NOGEN -eq 1 ]; then
    echo "ERROR: --compare cannot be combined with --plot-only or --no-gen"
    exit 1
  fi
  COMMON=(-r "$RUNS")
  [ -n "$OUTDIR" ] && COMMON+=(-o "$OUTDIR")
  BASE="${OUTNAME%.png}"
  FAILED=()
  for VARIANT in "${COMPARE_VARIANTS[@]}"; do
    variant_flags "$VARIANT"
    GEN=(--no-gen)
    [ "$VARIANT" = base ] && GEN=()
    ALL=( "${FLAGS[@]}" "${FORWARD[@]}" )
    # the run prints this header right before its nom command, i.e. after
    # nombench has generated the configurations for base
    if ! RUNBENCH_HEADER="==== $VARIANT${ALL[*]:+: ${ALL[*]}}" \
         "$SOURCE" "$TEST" "$BASE-$VARIANT" "${COMMON[@]}" "${GEN[@]}" \
         -- "${FLAGS[@]}" "${FORWARD[@]}"; then
      FAILED+=("$VARIANT")
      # without generated configurations the other variants cannot run
      if [ "$VARIANT" = base ] && \
         ! compgen -G "$BENCHDIR/.BM_*/" > /dev/null
      then
        break
      fi
    fi
  done
  if [ ${#FAILED[@]} -gt 0 ]; then
    echo "ERROR: failed variants: ${FAILED[*]}"
    exit 1
  fi
  exit 0
fi

# per-benchmark generator flags, as in scripts/shell/run-<test>.sh
EXTRA=()
case "$TEST" in
  sort) EXTRA=(--byfile) ;;
esac

if [ $PLOTONLY -eq 0 ]; then
  # nombench never deletes old out*.txt, and collect.rkt reads them all, so
  # a previous run with more RUNS would leak stale timings into the chart
  rm -f "$BENCHDIR"/.BM_*/out*.txt
  NOMCMD="nom ${NOMARGS[*]}${FORWARD[*]:+ ${FORWARD[*]}} $PROJECT"
  announce_nom() {
    [ -n "$RUNBENCH_HEADER" ] && echo "$RUNBENCH_HEADER"
    echo "== $NOMCMD ($1)"
  }
  if [ $NOGEN -eq 1 ]; then
    CONFIGS=( "$BENCHDIR"/.BM_*/ )
    if [ ! -d "${CONFIGS[0]}" ]; then
      echo "ERROR: no generated configurations in $BENCHDIR (drop --no-gen)"
      exit 1
    fi
    announce_nom "${#CONFIGS[@]} configurations, generator skipped"
    for (( RUN = 1; RUN <= RUNS; RUN++ )); do
      N=0
      for CONF in "${CONFIGS[@]}"; do
        CONF="${CONF%/}"
        N=$(( N + 1 ))
        echo "Run $RUN/$RUNS: $(basename "$CONF")($N/${#CONFIGS[@]})"
        ( cd "$CONF" && nom -p "$CONF" "${NOMARGS[@]}" "${FORWARD[@]}" \
            "$PROJECT" > "out$RUN.txt" )
      done
    done
  else
    ARGS=(--path "$BENCHDIR" --project "$PROJECT" -r "$RUNS" "${EXTRA[@]}")
    # --forward must come last: nombench skips the argument following it
    [ ${#FORWARD[@]} -gt 0 ] && ARGS+=(--forward ${#FORWARD[@]} "${FORWARD[@]}")
    echo "== nombench ${ARGS[*]}"
    # nombench generates the configurations, then prints "Run ..." before
    # each nom call; announce the nom command between the two phases
    nombench "${ARGS[@]}" | {
      SHOWN=0
      while IFS= read -r LINE; do
        if [ $SHOWN -eq 0 ] && [[ "$LINE" == Run\ * ]]; then
          CONFIGS=( "$BENCHDIR"/.BM_*/ )
          announce_nom "${#CONFIGS[@]} configurations, run by nombench"
          SHOWN=1
        fi
        printf '%s\n' "$LINE"
      done
    }
    [ "${PIPESTATUS[0]}" -eq 0 ] || { echo "ERROR: nombench failed"; exit 1; }
  fi
  echo "== collecting results"
  "$MONNOMBASE/collect.sh" "monnom/$TEST" > /dev/null \
    || { echo "ERROR: collect failed"; exit 1; }
fi

if [ ! -s "$BENCHDIR/results.csv" ]; then
  echo "ERROR: no results in $BENCHDIR/results.csv"
  exit 1
fi
# a run that crashed or printed an error leaves an empty out file
EMPTY=$( find "$BENCHDIR" -path '*/.BM_*/out*.txt' -empty | wc -l )
if [ "$EMPTY" -gt 0 ]; then
  echo "ERROR: $EMPTY run(s) produced no output:"
  find "$BENCHDIR" -path '*/.BM_*/out*.txt' -empty | head
  exit 1
fi

# newplot.py also checks that every configuration produced the same result
echo "== charting $PROJECT"
OUTDIR="${OUTDIR:-$MONNOMBASE}"
# run from OUTDIR so the chart title stays the bare name
cd "$OUTDIR"
python3 "$MONNOMBASE/scripts/python/newplot.py" "$OUTNAME" \
  "$BENCHDIR" monnom \
  || { echo "ERROR: charting failed"; exit 1; }
echo "wrote $OUTDIR/$OUTNAME"
