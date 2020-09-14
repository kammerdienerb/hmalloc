#!/usr/bin/env bash

CMD=$@

function run {
    LD_PRELOAD=$1 /usr/bin/time -v env ${LD_PRE} ${CMD} 2>&1 1>/dev/null |
        awk '/wall clock/       { printf("%s ",  $8); }
             /Maximum resident/ { printf("%s\n", $6); }' |
        numfmt --field=2 --from-unit=1024 --to=iec
}

ALLO="/usr/lib/libc.so.6"
echo "using allocator $ALLO"
for i in $(seq 1 3); do
    run $ALLO
done

echo ""

ALLO="/usr/lib/libjemalloc.so"
echo "using allocator $ALLO"
for i in $(seq 1 3); do
    run $ALLO
done

echo ""

ALLO="/usr/lib/libtcmalloc.so"
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
