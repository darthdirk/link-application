#!/usr/bin/env bash
# Install only the ARM Zephyr SDK, checking both downloaded archives first.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/environment.sh"
case $(uname -sm) in
    'Linux x86_64') sdk_arch=linux-x86_64 ;;
    'Linux aarch64') sdk_arch=linux-aarch64 ;;
    *) printf 'This setup supports x86_64/aarch64 Linux only.\n' >&2; exit 2 ;;
esac
sdk_version=0.17.0
sdk_dir=$ZEPHYR_SDK_INSTALL_DIR
if [[ -f $sdk_dir/.logger-verified-sdk && -x $sdk_dir/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc ]]; then
    printf 'Verified Zephyr SDK already installed: %s\n' "$sdk_dir"
    exit 0
fi
if [[ -f $sdk_dir/sdk_version && $(cat "$sdk_dir/sdk_version") != "$sdk_version" ]]; then
    printf 'SDK directory contains a different version: %s\n' "$sdk_dir" >&2
    exit 2
fi
cache_dir="$LOGGER_WORKSPACE_DIR/toolchains/downloads"
mkdir -p "$cache_dir" "$sdk_dir"
cd "$cache_dir"
release_url="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$sdk_version"
minimal_archive="zephyr-sdk-${sdk_version}_${sdk_arch}_minimal.tar.xz"
toolchain_archive="toolchain_${sdk_arch}_arm-zephyr-eabi.tar.xz"
curl --fail --location --retry 3 "$release_url/sha256.sum" -o sha256.sum
for archive in "$minimal_archive" "$toolchain_archive"; do
    checksum=$(awk -v name="$archive" '$2 == name || $2 == "*" name {print $1}' sha256.sum)
    [[ $checksum =~ ^[[:xdigit:]]{64}$ ]] || { printf 'Missing or duplicate official checksum for %s\n' "$archive" >&2; exit 1; }
    if [[ ! -f $archive ]] || ! printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status; then
        curl --fail --location --retry 3 "$release_url/$archive" -o "$archive.part"
        printf '%s  %s\n' "$checksum" "$archive.part" | sha256sum --check
        mv -- "$archive.part" "$archive"
    fi
done
# Both archives are authenticated against the official release checksums before extraction.
tar -xJf "$minimal_archive" --strip-components=1 -C "$sdk_dir"
tar -xJf "$toolchain_archive" -C "$sdk_dir"
cd "$sdk_dir"
# The ARM directory is already installed, so setup.sh does not download unchecked files.
./setup.sh -t arm-zephyr-eabi -h
"$sdk_dir/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" --version | head -1
printf '%s\n' "$sdk_version $sdk_arch" > .logger-verified-sdk
