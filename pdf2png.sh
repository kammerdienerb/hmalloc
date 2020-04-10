#!/usr/bin/env bash

BASE=$(basename "$1" ".pdf")
PDF="$1"
PNG="${BASE}.png"

convert +profile "*" -trim -fuzz 20% -channel RGB -negate -transparent "black" -quality 100 -density 300 "$PDF" "$PNG"
