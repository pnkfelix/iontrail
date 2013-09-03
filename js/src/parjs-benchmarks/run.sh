#!/bin/bash

ION_OPTS="--ion-parallel-compile=on "

MODE=compare
for var in "$@"
do
    if [[ "$var" == "--seq" ]]; then
        MODE=seq
        shift
    elif [[ "$var" == "--par" ]]; then
        MODE=par
        shift
    elif [[ "$var" == "--one" ]]; then
        MODE=one
        shift
    elif [[ "$var" == "--two" ]]; then
        MODE=two
        shift
    elif [[ "$var" == "--logs" ]]; then # for getting IONFLAGS=logs to work
        ION_OPTS="--ion-parallel-compile=off --ion-limit-script-size=off "
        shift
    else
        break
    fi
done

if [[ -z "$1" ]] || [[ "$1" == "--help" ]]; then
    echo "Usage: run.sh [--seq | --par | --one | --two] [--logs] path-to-shell paths-to-test"
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
    echo "$S" $ION_OPTS -e "'"'var libdir="'$D'/"; var MODE="'$MODE'";'"'" "$T"
    "$S" $ION_OPTS -e 'var libdir="'$D'/"; var MODE="'$MODE'";' "$T"
done
