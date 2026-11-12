#!/usr/bin/env bash
# Run once after cloning, then use just. Never programs a connected board.
set -euo pipefail
install_apt=true
install_sdk=true
build_firmware=true
for option in "$@"; do
    case "$option" in
        --no-apt) install_apt=false ;;
        --no-sdk) install_sdk=false ;;
        --no-build) build_firmware=false ;;
        *) printf 'Usage: %s [--no-apt] [--no-sdk] [--no-build]\n' "$0" >&2; exit 2 ;;
    esac
done
if "$install_apt"; then
    apt_command=(sudo apt-get)
    if [[ $(id -u) == 0 ]]; then apt_command=(apt-get); fi
    "${apt_command[@]}" update
    "${apt_command[@]}" install -y git python3 python3-venv python3-pip build-essential \
        curl wget xz-utils file gperf ripgrep device-tree-compiler libusb-1.0-0 libncursesw6 libtinfo6 direnv minicom tzdata
fi
source "$(dirname -- "${BASH_SOURCE[0]}")/environment.sh"
cd "$LOGGER_REPO_DIR"
python3 -m venv "$VIRTUAL_ENV"
"$VIRTUAL_ENV/bin/python" -m pip install -r tools/requirements.txt
if [[ ! -d $LOGGER_WORKSPACE_DIR/.west ]]; then
    west init -l "$LOGGER_REPO_DIR"
fi
manifest_path=$(west config manifest.path)
if [[ $(realpath "$(west topdir)/$manifest_path") != "$LOGGER_REPO_DIR" ]]; then
    printf 'Existing west workspace uses another manifest. Use a separate parent folder.\n' >&2
    exit 2
fi
west update --narrow
python -m pip install -r "$ZEPHYR_BASE/scripts/requirements-base.txt"
if "$install_sdk"; then bash scripts/install-sdk.sh; fi
pre-commit install
just check
if "$build_firmware"; then just build_53; fi
cat <<'MESSAGE'
Setup complete. To activate this checkout in your current Bash terminal:
  eval "$(direnv hook bash)"
  direnv allow
For future terminals add that hook once to ~/.bashrc, or run it again.
Install the official SEGGER J-Link Linux package before flashing/debugging.
Then use: just flash_53, just serial, or just debug_server_53.
MESSAGE
