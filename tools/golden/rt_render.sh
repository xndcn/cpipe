#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 cpipe contributors

set -euo pipefail

rt_render_tmpdir=""

usage() {
    cat <<'USAGE'
Usage:
  tools/golden/rt_render.sh --self-test
  tools/golden/rt_render.sh <input.dng> <profile.pp3> <output.exr>

Environment:
  RAWTHERAPEE_CLI       Optional path to rawtherapee-cli.
  RAWTHERAPEE_APPIMAGE  Optional path to RawTherapee_5.10.AppImage.
USAGE
}

die() {
    printf 'rt_render.sh: %s\n' "$*" >&2
    exit 1
}

sha256_file() {
    sha256sum "$1" | awk '{print $1}'
}

extract_appimage_cli() {
    local appimage="$1"
    local cache_root="${XDG_CACHE_HOME:-"$HOME/.cache"}/cpipe/rawtherapee"
    local key
    key="$(sha256_file "$appimage")"
    local extract_dir="$cache_root/$key"
    local cli="$extract_dir/squashfs-root/usr/bin/rawtherapee-cli"
    if [[ ! -x "$cli" ]]; then
        mkdir -p "$extract_dir"
        (
            cd "$extract_dir"
            "$appimage" --appimage-extract >/dev/null
        )
    fi
    [[ -x "$cli" ]] || die "rawtherapee-cli missing after AppImage extraction"
    printf '%s\n' "$cli"
}

find_rt_cli() {
    if [[ -n "${RAWTHERAPEE_CLI:-}" ]]; then
        [[ -x "$RAWTHERAPEE_CLI" ]] || die "RAWTHERAPEE_CLI is not executable: $RAWTHERAPEE_CLI"
        printf '%s\n' "$RAWTHERAPEE_CLI"
        return
    fi
    if command -v rawtherapee-cli >/dev/null 2>&1; then
        command -v rawtherapee-cli
        return
    fi
    if [[ -n "${RAWTHERAPEE_APPIMAGE:-}" ]]; then
        [[ -x "$RAWTHERAPEE_APPIMAGE" ]] ||
            die "RAWTHERAPEE_APPIMAGE is not executable: $RAWTHERAPEE_APPIMAGE"
        extract_appimage_cli "$RAWTHERAPEE_APPIMAGE"
        return
    fi
    die "set RAWTHERAPEE_APPIMAGE or RAWTHERAPEE_CLI"
}

find_converter() {
    if command -v oiiotool >/dev/null 2>&1; then
        printf 'oiiotool\n'
        return
    fi
    if command -v convert >/dev/null 2>&1 &&
        convert -list format 2>/dev/null | grep -Eq '^[[:space:]]+EXR[[:space:]]+EXR[[:space:]]+rw'; then
        printf 'convert\n'
        return
    fi
    die "need oiiotool or ImageMagick convert with EXR write support"
}

self_test() {
    local cli converter version
    cli="$(find_rt_cli)"
    version="$("$cli" -h 2>&1 || true)"
    version="${version%%$'\n'*}"
    [[ "$version" == *"version 5.10"* ]] || die "expected RawTherapee 5.10, got: $version"
    converter="$(find_converter)"
    printf 'rawtherapee-cli=%s\n' "$cli"
    printf 'converter=%s\n' "$converter"
}

render() {
    local input="$1"
    local profile="$2"
    local output="$3"
    [[ -f "$input" ]] || die "input missing: $input"
    [[ -f "$profile" ]] || die "profile missing: $profile"
    [[ "$output" == *.exr ]] || die "output must end in .exr"

    local cli converter tiff
    cli="$(find_rt_cli)"
    converter="$(find_converter)"
    rt_render_tmpdir="$(mktemp -d)"
    trap 'rm -rf -- "${rt_render_tmpdir:-}"' EXIT
    tiff="$rt_render_tmpdir/rt-output.tif"

    "$cli" -q -p "$profile" -t -b32 -Y -o "$tiff" -c "$input"
    [[ -f "$tiff" ]] || die "RawTherapee did not write $tiff"

    mkdir -p "$(dirname "$output")"
    if [[ "$converter" == "oiiotool" ]]; then
        oiiotool "$tiff" -o "$output"
    else
        convert "$tiff" "$output"
    fi
}

case "${1:-}" in
    --self-test)
        self_test
        ;;
    -h|--help|"")
        usage
        ;;
    *)
        [[ "$#" -eq 3 ]] || {
            usage >&2
            exit 2
        }
        render "$1" "$2" "$3"
        ;;
esac
