#!/bin/bash
# Compile and run the functional tests in experiments/monnom/function and
# check that each one passes.
#
# usage: ./run-function.sh [TEST...] [--compare] [-- NOM_FLAGS...]
#
#   TEST...         test folders in experiments/monnom/function
#                   (default: all of them)
#   --compare       run each test three times: no flags, --trans,
#                   --trans --sync; all three outputs must be identical
#   -- FLAGS...     extra flags passed to nom, after the default -o3 so
#                   they override it (e.g. --trans, -o1)
#
# A test passes when nomc compiles it, nom exits with status 0 within
# RUNFN_TIMEOUT seconds (default 300), and the output has no "FAIL",
# "ERROR" or "Aborting", and no "<name>: COMPARE got|expected" line whose
# two sides differ.
# setpath.sh (and the setvars.sh it sources) overwrite common names such as
# SOURCE, SCRIPT and usage, so everything here is prefixed
RUNFN_SELF="$( realpath "${BASH_SOURCE[0]}" )"
RUNFN_DIR="$( dirname "$RUNFN_SELF" )"
FUNCDIR="$RUNFN_DIR/experiments/monnom/function"
TIMEOUT="${RUNFN_TIMEOUT:-300}"

runfn_usage() {
  sed -n '5,12p' "$RUNFN_SELF" | sed 's/^# \{0,1\}//'
  echo "available tests:"
  for d in "$FUNCDIR"/*/; do
    compgen -G "$d*.mnp" > /dev/null && echo "  $(basename "$d")"
  done
  exit 1
}

# runtime flags of each --compare mode
runfn_mode_flags() {
  case "$1" in
    base) FLAGS=() ;;
    trans) FLAGS=(--trans) ;;
    trans-sync) FLAGS=(--trans --sync) ;;
  esac
}

# prints the failure markers found in the output file $1
runfn_check_output() {
  grep -E ': FAIL|ERROR|Aborting' "$1"
  awk '/: COMPARE / { split($0, a, ": COMPARE "); n = index(a[2], "|");
                      if (substr(a[2], 1, n - 1) != substr(a[2], n + 1))
                        print }' "$1"
}

TESTS=()
COMPARE=0
FORWARD=()
while [ $# -gt 0 ]; do
  case "$1" in
    --compare) COMPARE=1; shift ;;
    -h|--help) runfn_usage ;;
    --) shift; FORWARD=("$@"); break ;;
    -*) echo "ERROR: unknown option $1"; runfn_usage ;;
    *) TESTS+=("${1%/}"); shift ;;
  esac
done
if [ ${#TESTS[@]} -eq 0 ]; then
  for d in "$FUNCDIR"/*/; do
    compgen -G "$d*.mnp" > /dev/null && TESTS+=("$(basename "$d")")
  done
fi
for T in "${TESTS[@]}"; do
  MNPS=( "$FUNCDIR/$T"/*.mnp )
  if [ ${#MNPS[@]} -ne 1 ] || [ ! -f "${MNPS[0]}" ]; then
    echo "ERROR: $FUNCDIR/$T is not a test (needs exactly one .mnp file)"
    runfn_usage
  fi
done
MODES=("")
[ $COMPARE -eq 1 ] && MODES=(base trans trans-sync)

. "$RUNFN_DIR/setpath.sh" > /dev/null
TMP="$( mktemp -d )"
trap 'rm -rf "$TMP"' EXIT

PASSED=()
FAILED=()
for T in "${TESTS[@]}"; do
  D="$FUNCDIR/$T"
  P="$( basename "$D"/*.mnp .mnp )"
  echo "==== $T"
  echo "== nomc -p $D --project $P"
  # nom would silently run the .mnil files of an older build, so a failed
  # compile must stop the test
  if ! nomc -p "$D" --project "$P" > "$TMP/nomc.txt" 2>&1 || \
     ! grep -q '^Done!' "$TMP/nomc.txt"; then
    tail -15 "$TMP/nomc.txt" | sed 's/^/  /'
    echo "FAIL: $T does not compile"
    FAILED+=("$T (compile)")
    continue
  fi
  PROBLEMS=()
  for MODE in "${MODES[@]}"; do
    runfn_mode_flags "${MODE:-base}"
    ALL=( -o3 "${FLAGS[@]}" "${FORWARD[@]}" )
    echo "== ${MODE:+[$MODE] }nom ${ALL[*]} $P"
    OUT="$TMP/out-${MODE:-base}.txt"
    ( cd "$D" && timeout "$TIMEOUT" nom -p "$D" "${ALL[@]}" "$P" ) \
      > "$OUT" 2>&1
    RC=$?
    TAG="${MODE:+$MODE: }"
    if [ "${MODE:-base}" = base ]; then
      cat "$OUT"
    elif ! diff -q "$TMP/out-base.txt" "$OUT" > /dev/null; then
      echo "output differs from base (< base, > $MODE):"
      diff "$TMP/out-base.txt" "$OUT" | sed 's/^/  /'
      PROBLEMS+=("${TAG}output differs from base")
    else
      echo "(same output as base)"
    fi
    if [ $RC -eq 124 ]; then
      PROBLEMS+=("${TAG}timed out after ${TIMEOUT}s")
    elif [ $RC -ne 0 ]; then
      PROBLEMS+=("${TAG}nom exited with status $RC")
    fi
    MARKERS=$( runfn_check_output "$OUT" | wc -l )
    [ "$MARKERS" -gt 0 ] && \
      PROBLEMS+=("${TAG}$MARKERS failure line(s) in the output")
  done
  if [ ${#PROBLEMS[@]} -eq 0 ]; then
    echo "PASS: $T"
    PASSED+=("$T")
  else
    echo "FAIL: $T"
    printf '  %s\n' "${PROBLEMS[@]}"
    runfn_check_output "$TMP/out-base.txt" | sed 's/^/  /'
    FAILED+=("$T ($( IFS=';'; echo "${PROBLEMS[*]}" | sed 's/;/; /g' ))")
  fi
done

echo "==== ${#PASSED[@]} passed, ${#FAILED[@]} failed"
[ ${#FAILED[@]} -eq 0 ] && exit 0
printf '  %s\n' "${FAILED[@]}"
exit 1
