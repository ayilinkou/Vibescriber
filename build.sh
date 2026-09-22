#!/usr/bin/env sh

set -eu

project_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_directory="$project_directory/build"
build_jobs=${VIBESCRIBER_BUILD_JOBS:-2}

cmake \
    -S "$project_directory" \
    -B "$build_directory" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF

cmake --build "$build_directory" --config Release --parallel "$build_jobs"
