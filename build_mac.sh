#!/bin/bash
set -e

cd "$(dirname "$0")"

cmake -B Build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build Build --config Release -j4
