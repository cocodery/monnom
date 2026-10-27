#!/bin/bash
# usage: experiments/monnom/function/bugs/run_test.sh [TestName] [-- NOM_FLAGS...]
# Without a name, runs every test in one process. With a name (e.g.
# Test_Alignment), runs only that test, so a crash cannot hide other results.
# Uses the nomc / nom from setpath.sh (the current build). Default flag: -o3.
# Set NOMC to another compiler command to compile with it, e.g.
#   NOMC="dotnet /path/to/nomc.dll" .../run_test.sh Test_IfAssign
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME=""
if [ $# -gt 0 ] && [ "$1" != "--" ]; then NAME=$1; shift; fi
[ "$1" = "--" ] && shift
FLAGS=("$@")
[ ${#FLAGS[@]} -eq 0 ] && FLAGS=(-o3)

# setpath.sh is in the repository root, somewhere above this folder
ROOT="$HERE"
while [ "$ROOT" != "/" ] && [ ! -f "$ROOT/setpath.sh" ]; do ROOT="$(dirname "$ROOT")"; done
if [ ! -f "$ROOT/setpath.sh" ]; then
  echo "setpath.sh not found above $HERE"; exit 2
fi
cd "$ROOT" && . ./setpath.sh > /dev/null 2>&1

W=$(mktemp -d)
cp "$HERE/BugTests.mnp" "$W/"
if [ -n "$NAME" ]; then
  if ! grep -q "Main.$NAME();" "$HERE/BugTests.mn"; then
    echo "unknown test $NAME"; rm -rf "$W"; exit 2
  fi
  # the runtime compiles every method of Main when it loads it, so drop the
  # other tests' functions (# BEGIN/# END blocks) and calls, not just the calls
  awk -v name="$NAME" -v keep="Main.$NAME();" '
    /# BEGIN Test_/ { skip = ($3 != name) }
    skip { if ($0 ~ /# END Test_/) skip = 0; next }
    /# TESTS-BEGIN/ {inside=1; print; next}
    /# TESTS-END/ {inside=0}
    inside && index($0, "Main.Test_") && !index($0, keep) {next}
    {print}' "$HERE/BugTests.mn" > "$W/BugTests.mn"
else
  cp "$HERE/BugTests.mn" "$W/"
fi
if ! ${NOMC:-nomc} -p "$W" --project BugTests > "$W/compile.txt" 2>&1 || ! grep -q "Done!" "$W/compile.txt"; then
  echo "COMPILE ERROR:"; grep -v '^ *at ' "$W/compile.txt" | head -5
  rm -rf "$W"; exit 1
fi
# a "<name>: COMPARE" line is followed by the actual and the expected text,
# one line each; they become PASS / FAIL
(cd "$W" && timeout 120 nom -p "$W" "${FLAGS[@]}" BugTests 2>&1 |
  awk '/: COMPARE/ { split($0, a, ": COMPARE"); got = ""; want = "";
         getline got; getline want;
         if (got == want) print a[1] ": PASS"; else print a[1] ": FAIL got " got " expected " want; next }
       {print}' |
  grep -E "PASS|FAIL|rror|terminate|Abort|UNIMPL|dumped|Invalid|verify" | head -40)
STATUS=${PIPESTATUS[0]}
rm -rf "$W"
exit $STATUS
