#!/usr/bin/env bash

echo "newgraph"
echo "newline pts"
awk "{ printf(\"%f %f\n\", \$1, \$2); }" $1
