#!/bin/bash
#
# Script to run state merging experiments to support an MSR talk (2015).

mkdir -p merging-out
: >merging-out/commands.txt

SEQNO=0
for CONFIG in hl-dfs-topo.lua hl-dfs-sprout.lua; do
	for SYMSIZE in $(seq 1 20); do
		OUTDIR=merging-out/${CONFIG%.*}/size-${SYMSIZE}
		mkdir -p ${OUTDIR}
		echo ./run_qemu.py --command-port $((1024+SEQNO)) sym -f jenkins/config/${CONFIG} --time-out 600 --out-dir ${OUTDIR} /bin/bash -c "\"python tools/calibrate/calibrate.py && python tools/symtests/asplos_tests.py ValidationStringOps --sym-size ${SYMSIZE}\"" >>merging-out/commands.txt
		SEQNO=$((SEQNO+1))
	done
done

cat merging-out/commands.txt | parallel --delay 1 --ungroup --joblog merging-out/joblog.log
