#!/bin/bash

REPO_ROOT=$(git rev-parse --show-toplevel)
find ${REPO_ROOT}/chratos -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs clang-format -i
