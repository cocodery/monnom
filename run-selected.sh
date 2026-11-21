#!/bin/bash
# Collects the performance data of the chosen benchmarks: every
# configuration in two modes (base, --trans --sync; async --trans is off), RUNS measured
# runs per mode after WARMUPS warmups, with the chart and lattice of each mode
# and the tables of charts/{benchmark,gtp}/.
#
# usage: ./run-selected.sh [BENCHMARKS=a,b,...] [GTP=c,d,...] [--all]
#                          [-r RUNS] [-w WARMUPS]      (defaults: -r 3 -w 1)
#
#   BENCHMARKS=...  tests in experiments/monnom, run with run-benchmark.sh
#   GTP=...         tests in gtp-benchmark-monnom, run with run-gtp.sh
#                   (names separated by commas, semicolons or spaces, e.g.
#                   BENCHMARKS=splay,deltablue GTP="fsm;synth")
#   --all           every test of both; cannot be combined with the lists
#   -h, --help      this text and the available tests
#
# Run it as yourself: it starts itself again through bench-cpus.sh (which
# asks for your sudo password once), so every benchmark runs on the fixed
# 2400 MHz CPUs 0,1 and the settings are put back at the end, also on Ctrl-C.
# The whole output also goes to logs/selected-<date>.log. A failing benchmark
# does not stop the others; the script exits non-zero if any failed. It takes
# days, so start it inside tmux.
SELF="$(realpath "${BASH_SOURCE[0]}")"
DIR="$(dirname "$SELF")"

# the tests each script can run, as its own listing finds them
selected_list_benchmarks() {
  local d
  for d in "$DIR"/experiments/monnom/*/; do
    [ -f "$d/plotconfig.json" ] && compgen -G "$d*.mnp" > /dev/null &&
      basename "$d"
  done
}
selected_list_gtp() {
  local d
  for d in "$DIR"/gtp-benchmark-monnom/*/; do
    compgen -G "$d*.mnp" > /dev/null && basename "$d"
  done
}

selected_usage() {
  sed -n '2,22p' "$SELF" | sed 's/^# \{0,1\}//'
  echo "available BENCHMARKS: $(selected_list_benchmarks | paste -sd' ')"
  echo "available GTP:        $(selected_list_gtp | paste -sd' ')"
  exit "${1:-0}"
}

BENCHMARKS=()
GTP=()
ALL=0
RUNS=3
WARMUPS=1
INSIDE=0
LOG=""
while [ $# -gt 0 ]; do
  case "$1" in
    BENCHMARKS=*) read -ra BENCHMARKS <<< "${1#BENCHMARKS=}"; shift ;;
    GTP=*) read -ra GTP <<< "${1#GTP=}"; shift ;;
    --all) ALL=1; shift ;;
    -r) [[ "$2" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: -r needs a positive number"; exit 1; }
        RUNS="$2"; shift 2 ;;
    -w) [[ "$2" =~ ^[0-9]+$ ]] || { echo "ERROR: -w needs a number"; exit 1; }
        WARMUPS="$2"; shift 2 ;;
    # set when bench-cpus.sh starts this script again
    --inside) INSIDE=1; LOG="$2"; shift 2 ;;
    -h|--help) selected_usage 0 ;;
    *) echo "ERROR: unexpected argument $1"; selected_usage 1 ;;
  esac
done
# commas and semicolons separate names too
read -ra BENCHMARKS <<< "${BENCHMARKS[*]//[,;]/ }"
read -ra GTP <<< "${GTP[*]//[,;]/ }"

if [ $ALL -eq 1 ]; then
  if [ ${#BENCHMARKS[@]} -gt 0 ] || [ ${#GTP[@]} -gt 0 ]; then
    echo "ERROR: --all cannot be combined with BENCHMARKS= or GTP="; exit 1
  fi
  mapfile -t BENCHMARKS < <(selected_list_benchmarks)
  mapfile -t GTP < <(selected_list_gtp)
fi
if [ ${#BENCHMARKS[@]} -eq 0 ] && [ ${#GTP[@]} -eq 0 ]; then
  echo "ERROR: choose tests with BENCHMARKS=..., GTP=... or --all"
  selected_usage 1
fi
# checked before asking for the sudo password, not hours into the run
BAD=()
for t in "${BENCHMARKS[@]}"; do
  selected_list_benchmarks | grep -qx "$t" || BAD+=("BENCHMARKS: $t")
done
for t in "${GTP[@]}"; do
  selected_list_gtp | grep -qx "$t" || BAD+=("GTP: $t")
done
if [ ${#BAD[@]} -gt 0 ]; then
  printf 'ERROR: no such test, %s\n' "${BAD[@]}"
  selected_usage 1
fi

if [ $INSIDE -eq 0 ]; then
  if [ "$EUID" -eq 0 ]; then
    echo "ERROR: run this as yourself, without sudo"; exit 1
  fi
  mkdir -p "$DIR/logs"
  LOG="$DIR/logs/selected-$(date +%Y%m%d-%H%M%S).log"
  echo "== log: $LOG"
  "$DIR/bench-cpus.sh" "$SELF" "BENCHMARKS=${BENCHMARKS[*]}" "GTP=${GTP[*]}" \
    -r "$RUNS" -w "$WARMUPS" --inside "$LOG" 2>&1 | tee "$LOG"
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
echo "== BENCHMARKS: ${BENCHMARKS[*]:-none}; GTP: ${GTP[*]:-none}"
for t in "${BENCHMARKS[@]}"; do selected_run run-benchmark.sh "$t"; done
for t in "${GTP[@]}"; do selected_run run-gtp.sh "$t"; done

echo "######## done after $(( ($(date +%s) - START) / 3600 )) h, $(date '+%F %T')"
printf '  %s\n' "${STATUS[@]}"
echo "  tables: charts/benchmark/SUMMARY.md, charts/gtp/SUMMARY.md"
[ $FAILED -eq 0 ] || echo "  some benchmarks failed; see FAIL lines in $LOG"
exit $FAILED
