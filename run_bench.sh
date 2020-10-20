#!/usr/bin/env bash

CMD=$@

function run {
    LD_PRE="LD_PRELOAD=$1"
    /usr/bin/time -v env ${LD_PRE} ${CMD} 2>&1 1>/dev/null |
        awk '/wall clock/       { printf("%s ",  $8); }
             /Maximum resident/ { printf("%s\n", $6); }' |
        numfmt --field=2 --from-unit=1024 --to=iec
}

ALLO="/usr/lib/x86_64-linux-gnu/libc-2.28.so"
echo "using allocator $ALLO"
for i in $(seq 1 3); do
    run $ALLO
done

echo ""

ALLO="/usr/lib/x86_64-linux-gnu/libjemalloc.so.2"
echo "using allocator $ALLO"
for i in $(seq 1 3); do
    run $ALLO
done

echo ""

ALLO="/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4"
echo "using allocator $ALLO"
for i in $(seq 1 3); do
    run $ALLO
done

echo ""

ALLO="lib/libhmalloc.so"
echo "using allocator $ALLO"
for i in $(seq 1 3); do
    run $ALLO
done
