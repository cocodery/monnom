#!/bin/bash
# Collects the performance data of the selected benchmarks: every
# configuration in three modes (base, --trans, --trans --sync), RUNS measured
# runs per mode after WARMUPS warmups, with the chart and lattice of each mode
# and the tables of charts/{benchmark,gtp}/.
#
# usage: ./run-selected.sh [-r RUNS] [-w WARMUPS]      (defaults: -r 5 -w 1)
#
#   experiments/monnom (run-benchmark.sh):
#     deltablue float havlak lambdas richards sieve splay
#   gtp-benchmark-monnom (run-gtp.sh):
#     fsm fsmoo morsecode quadT suffixtree synth
#
# Run it as yourself: it starts itself again through bench-cpus.sh (which
# asks for your sudo password once), so every benchmark runs on the fixed
# 2400 MHz CPUs 0,1 and the settings are put back at the end, also on Ctrl-C.
# The whole output also goes to logs/selected-<date>.log. A failing benchmark
# does not stop the others; the script exits non-zero if any failed. It takes
# days, so start it inside tmux.
SELF="$(realpath "${BASH_SOURCE[0]}")"
DIR="$(dirname "$SELF")"
BENCHMARKS=(splay)
GTP=(fsm fsmoo morsecode quadT suffixtree synth)

RUNS=3
WARMUPS=1
INSIDE=0
LOG=""
while [ $# -gt 0 ]; do
  case "$1" in
    -r) [[ "$2" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: -r needs a positive number"; exit 1; }
        RUNS="$2"; shift 2 ;;
    -w) [[ "$2" =~ ^[0-9]+$ ]] || { echo "ERROR: -w needs a number"; exit 1; }
        WARMUPS="$2"; shift 2 ;;
    # set when bench-cpus.sh starts this script again
    --inside) INSIDE=1; LOG="$2"; shift 2 ;;
    -h|--help) sed -n '2,20p' "$SELF" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "ERROR: unexpected argument $1"; exit 1 ;;
  esac
done

if [ $INSIDE -eq 0 ]; then
  if [ "$EUID" -eq 0 ]; then
    echo "ERROR: run this as yourself, without sudo"; exit 1
  fi
  mkdir -p "$DIR/logs"
  LOG="$DIR/logs/selected-$(date +%Y%m%d-%H%M%S).log"
  echo "== log: $LOG"
  "$DIR/bench-cpus.sh" "$SELF" -r "$RUNS" -w "$WARMUPS" --inside "$LOG" 2>&1 |
    tee "$LOG"
  exit "${PIPESTATUS[0]}"
fi

cd "$DIR" || exit 1
OPTS=(-r "$RUNS" -w "$WARMUPS" --compare --chart --lattice)
STATUS=()
FAILED=0
START=$(date +%s)

# runs one benchmark with script $1 and records how it went
selected_run() {
  local script="$1" test="$2" t0=$SECONDS rc
  echo "######## $test ($script ${OPTS[*]}), started $(date '+%F %T')"
  "./$script" "$test" "${OPTS[@]}"
  rc=$?
  [ $rc -ne 0 ] && FAILED=1
  STATUS+=("$(printf '%-12s %-17s exit %d, %d min' "$test" "$script" $rc \
               $(( (SECONDS - t0) / 60 )))")
}

echo "== $RUNS run(s) and $WARMUPS warmup(s) per configuration and mode;" \
     "pinned to CPUs ${RUNBENCH_CPUS:-0,1}"
for t in "${BENCHMARKS[@]}"; do selected_run run-benchmark.sh "$t"; done
# for t in "${GTP[@]}"; do selected_run run-gtp.sh "$t"; done

echo "######## done after $(( ($(date +%s) - START) / 3600 )) h, $(date '+%F %T')"
printf '  %s\n' "${STATUS[@]}"
echo "  tables: charts/benchmark/SUMMARY.md, charts/gtp/SUMMARY.md"
[ $FAILED -eq 0 ] || echo "  some benchmarks failed; see FAIL lines in $LOG"
exit $FAILED
