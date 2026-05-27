#!/bin/bash
# Release build — optimised, no sanitizers, runs main_app (the scratchpad).
#
# -DCMAKE_BUILD_TYPE=Release   = enables optimisations (-O2/-O3), disables sanitizers

cmake -B build -DCMAKE_BUILD_TYPE=Release && \
cmake --build build --parallel && \
./build/main_app
