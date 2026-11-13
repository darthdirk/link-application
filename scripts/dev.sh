#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/environment.sh"
cd "$LOGGER_REPO_DIR"
command_name=${1:-}
target=${2:-thingy}
case "$target" in
    thingy) build_dir=$THING_BUILD_DIR_53; board=$THING_BOARD_53 ;;
    dk) build_dir=$DK_BUILD_DIR; board=nrf5340dk/nrf5340/cpuapp ;;
    *) printf 'Unknown target: %s\n' "$target" >&2; exit 2 ;;
esac
extra_conf=debug.conf
extra_args=()
case "$command_name" in
    build-diagnostics|flash-diagnostics)
        [[ $target == thingy ]] || { printf 'Diagnostics target is Thingy:53 only.\n' >&2; exit 2; }
        build_dir="${THING_BUILD_DIR_53}-diagnostics"
        command_name=${command_name%-diagnostics}
        extra_conf='debug.conf;diagnostics.conf'
        extra_args+=("-DEXTRA_DTC_OVERLAY_FILE=boards/thingy53-i2c-diagnostics.overlay")
        ;;
esac
case "$command_name" in
    build)
        if [[ -n ${BSP_DIR:-} ]]; then
            [[ -d $BSP_DIR/zephyr ]] || { printf 'Invalid BSP_DIR: %s\n' "$BSP_DIR" >&2; exit 2; }
            extra_args+=("-DEXTRA_ZEPHYR_MODULES=$BSP_DIR")
        fi
        west build --no-sysbuild --pristine=auto --board "$board" --build-dir "$build_dir" "$LOGGER_REPO_DIR/app" -- "-DEXTRA_CONF_FILE=$extra_conf" "${extra_args[@]}"
        ;;
    flash|debugserver)
        [[ -f $build_dir/zephyr/zephyr.elf ]] || { printf 'Build this target first.\n' >&2; exit 2; }
        probe_id=$DEBUG_53_SN
        case "$IP_DEBUG" in
            false) ;;
            true)
                debug_ip=$(getent ahostsv4 "$DEBUG_HOST" | awk 'NR == 1 {print $1; exit}')
                [[ -n $debug_ip ]] || { printf 'Cannot resolve DEBUG_HOST=%s\n' "$DEBUG_HOST" >&2; exit 2; }
                probe_id=$debug_ip
                ;;
            *) printf 'IP_DEBUG must be true or false.\n' >&2; exit 2 ;;
        esac
        runner_args=(--build-dir "$build_dir" --runner jlink --dev-id "$probe_id")
        if [[ $command_name == flash ]]; then runner_args+=(--no-erase --reset); fi
        west "$command_name" "${runner_args[@]}"
        ;;
    serial)
        exec python -m serial.tools.miniterm "$SERIAL_PORT" 115200 --eol LF
        ;;
    *) printf 'Usage: %s {build|flash|debugserver} {thingy|dk}, {build-diagnostics|flash-diagnostics} thingy, or serial\n' "$0" >&2; exit 2 ;;
esac
