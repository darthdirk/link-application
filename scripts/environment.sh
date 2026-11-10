#!/usr/bin/env bash
# Shared defaults. Source this file; keep personal overrides in .envrc.private.
LOGGER_REPO_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export LOGGER_REPO_DIR
export PRE_COMMIT_HOME="${PRE_COMMIT_HOME:-$LOGGER_REPO_DIR/.cache/pre-commit}"
export LOGGER_WORKSPACE_DIR="${LOGGER_WORKSPACE_DIR:-$(dirname -- "$LOGGER_REPO_DIR")}"
export VIRTUAL_ENV="${LOGGER_VENV:-$LOGGER_WORKSPACE_DIR/.venv}"
export PATH="$VIRTUAL_ENV/bin:$PATH"
export ZEPHYR_BASE="$LOGGER_WORKSPACE_DIR/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$LOGGER_WORKSPACE_DIR/toolchains/zephyr-sdk-0.17.0}"
export DEBUG_53_SN="${DEBUG_53_SN:-504403721}"
export IP_DEBUG="${IP_DEBUG:-false}"
export DEBUG_HOST="${DEBUG_HOST:-localhost}"
export THING_BOARD_53="${THING_BOARD_53:-thingy53/nrf5340/cpuapp}"
export THING_BUILD_DIR_53="${THING_BUILD_DIR_53:-$LOGGER_REPO_DIR/build/thingy53}"
export DK_BUILD_DIR="${DK_BUILD_DIR:-$LOGGER_REPO_DIR/build/dk}"
export SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
