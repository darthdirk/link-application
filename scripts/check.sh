#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/environment.sh"
cd "$LOGGER_REPO_DIR"
mapfile -t sources < <(rg --files app/src app/include tests -g '*.c' -g '*.h' | sort)
case "${1:-}" in
    format) clang-format -i "${sources[@]}" ;;
    format-check) clang-format --dry-run --Werror "${sources[@]}" ;;
    cppcheck)
        # Zephyr's kernel/shell macros are not supported by cppcheck's preprocessor.
        # Check the portable modules here; all firmware modules build with -Werror.
        cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
            --inline-suppr --std=c11 -I app/include \
            app/src/state.c app/src/timekeeping.c app/src/record.c
        ;;
    *) printf 'Usage: %s {format|format-check|cppcheck}\n' "$0" >&2; exit 2 ;;
esac
