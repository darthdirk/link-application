#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_build_dir="${TEST_BUILD_DIR:-${repo_dir}/build/host-tests}"
cmake -S "${repo_dir}/tests" -B "${test_build_dir}" -G Ninja
cmake --build "${test_build_dir}"
ctest --test-dir "${test_build_dir}" --output-on-failure
