set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

# List the local development commands.
default:
    @just --list

# Install the Ubuntu/Debian development environment (never flashes hardware).
fullsetup *args:
    bash scripts/setup-local.sh {{args}}

# Build the standalone Thingy:53 application core image.
build_53:
    bash scripts/dev.sh build thingy

# Separate diagnostic image with I2C chip-ID reads on the two Thingy buses.
build_53_diagnostics:
    bash scripts/dev.sh build-diagnostics thingy

flash_53_diagnostics:
    bash scripts/dev.sh flash-diagnostics thingy

build_dk:
    bash scripts/dev.sh build dk

# Normal programming: no recover, mass erase, or filesystem format.
flash_53:
    bash scripts/dev.sh flash thingy

flash_dk:
    bash scripts/dev.sh flash dk

# Leave running, then launch the matching VS Code external-server debug entry.
debug_server_53:
    bash scripts/dev.sh debugserver thingy

debug_server_dk:
    bash scripts/dev.sh debugserver dk

serial:
    bash scripts/dev.sh serial

# Print a UTC time command to paste into the device shell.
time_command:
    @date -u '+time set %Y-%m-%dT%H:%M:%SZ'

test:
    bash scripts/test.sh

format:
    bash scripts/check.sh format

format-check:
    bash scripts/check.sh format-check

cppcheck:
    bash scripts/check.sh cppcheck

check: format-check cppcheck test

hooks:
    pre-commit install
