#!/bin/bash
# Debug build — compiles with AddressSanitizer + UBSan, then runs tests.
#
# cmake -B build               = configure: read CMakeLists.txt, generate build files in build/
#   -DCMAKE_BUILD_TYPE=Debug   = activates the sanitizer flags defined in CMakeLists.txt
#
# cmake --build build          = compile everything using those generated build files
#   --parallel                 = use all CPU cores
#
# ./build/test_runner          = run the test binary directly

cmake -B ../build -DCMAKE_BUILD_TYPE=Debug && \
cmake --build ../build --parallel && \
../build/test_runner
