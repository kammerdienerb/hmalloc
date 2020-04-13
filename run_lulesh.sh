#!/usr/bin/env bash

export LD_PRELOAD=./lib/libhmalloc.so
export HMALLOC_PROFILE="yes"
lulesh/lulesh2.0 -s 220 -i 5 -r 11 -b 0 -c 64 -p
# lulesh/lulesh2.0 -s 100 -i 5 -r 11 -b 0 -c 64 -p
