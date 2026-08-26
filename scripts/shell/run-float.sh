#!/bin/bash
SOURCE="$( realpath "${BASH_SOURCE[0]}" )"
DIRNAME="$( dirname "$SOURCE" )"
. "$DIRNAME/setpath.sh"
if [ -z ${RUNS+x} ]; then RUNS=1 ; else :; fi
ORIG=`pwd`
cd "$MONNOMBASE"
python3 $(MONNOMBASE)/scripts/python/run-csharp-float.py "$MONNOMBASE"
python3 $(MONNOMBASE)/scripts/python/run-java-float.py "$MONNOMBASE"
python3 $(MONNOMBASE)/scripts/python/run-node-float.py "$MONNOMBASE"
python3 $(MONNOMBASE)/scripts/python/run-retic-float.py "$MONNOMBASE"
python3 $(MONNOMBASE)/scripts/python/run-grift-float.py "$MONNOMBASE"
nombench --path "$MONNOMBASE/experiments/monnom/float" -w 2 --project Float -r $RUNS
$DIRNAME/collect.sh "monnom/float"
cd $ORIG

