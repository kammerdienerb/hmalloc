#!/usr/bin/env bash

./mk_phase_jgraph.sh hmalloc.phase_profile | jgraph -P | ps2pdf -q - out.pdf
