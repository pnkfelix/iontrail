#!/bin/bash

MODE=compare
if [[ "$1" == "--seq" ]]; then
    MODE=seq
    shift
elif [[ "$1" == "--par" ]]; then
    MODE=par
    shift
elif [[ "$1" == "--one" ]]; then
    MODE=one
    shift
elif [[ "$1" == "--two" ]]; then
    MODE=two
    shift
fi

if [[ -z "$1" ]] || [[ "$1" == "--help" ]]; then
    echo "Usage: run.sh [--seq | --par | --one | --two] path-to-shell paths-to-test"
    echo ""
    echo "Runs the given benchmark(s) using the given shell and "
    echo "prints the results.  If -seq or -par is supplied, only"
    echo "runs sequentially or in parallel.  If -one or -two is "
    echo "supplied, only runs first or second benchmark."
    echo "Otherwise, runs both and compares the performance."
fi

D="$(dirname $0)"
S="$1"
shift
for T in "$@"; do
    echo "$S" --ion-parallel-compile=on -e "'"'var libdir="'$D'/"; var MODE="'$MODE'";'"'" "$T"
    "$S" --ion-parallel-compile=on -e 'var libdir="'$D'/"; var MODE="'$MODE'";' "$T"
done
