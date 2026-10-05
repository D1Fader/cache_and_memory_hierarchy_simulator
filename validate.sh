#!/bin/bash
# Builds sim, runs every validation config, and diffs against the expected output.
# Usage: ./validate.sh        (all runs)
#        ./validate.sh 1 2    (only val1 and val2)

make -s || { echo "BUILD FAILED"; exit 1; }
mkdir -p out

runs=${@:-1 2 3 4 5 6 7 8}
pass=0; total=0

for n in $runs; do
  vfile=$(ls validation/val${n}.*.txt)           # e.g. validation/val3.16_1024_1_8192_4_0_0_gcc.txt
  name=$(basename "$vfile" .txt)                 # val3.16_1024_1_8192_4_0_0_gcc
  args=$(echo "$name" | cut -d. -f2 | tr '_' ' ') # 16 1024 1 8192 4 0 0 gcc
  set -- $args                                   # $1..$7 = numbers, $8 = trace name
  out="out/val${n}.txt"

  # run from inside traces/ so the header prints "trace_file: gcc_trace.txt"
  (cd traces && ../sim $1 $2 $3 $4 $5 $6 $7 ${8}_trace.txt) > "$out"

  total=$((total+1))
  if diff -iw "$out" "$vfile" > "out/val${n}.diff"; then
    echo "val$n  PASS"; pass=$((pass+1))
  else
    echo "val$n  FAIL   (first difference below; full diff in out/val${n}.diff)"
    head -6 "out/val${n}.diff" | sed 's/^/        /'
  fi
done
echo "----- $pass / $total passed -----"