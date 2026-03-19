#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

fetch_serverbrowser() {
    local branch_flag="$1"
    local outdir="$2"
    local tmpdir=$(mktemp -d)

    DepotDownloader \
        -app 10 $branch_flag -os windows \
        -filelist "$ROOT/steam_filelist.txt" \
        -dir "$tmpdir" \
        -username "$STEAM_USERNAME" -password "$STEAM_PASSWORD"

    local src="$tmpdir/Half-Life/platform/servers/ServerBrowser.dll"
    if [[ ! -f "$src" ]]; then
        src=$(find "$tmpdir" -iname "ServerBrowser.dll" -print -quit)
    fi
    [[ -f "$src" ]] || { echo "FAIL: ServerBrowser.dll not found for $outdir"; find "$tmpdir" -type f; exit 1; }

    mkdir -p "$ROOT/$outdir"
    cp "$src" "$ROOT/$outdir/ServerBrowser.dll"
    echo "OK: $outdir/ServerBrowser.dll ($(stat -c%s "$ROOT/$outdir/ServerBrowser.dll") bytes)"
    rm -rf "$tmpdir"
}

fetch_serverbrowser "-branch steam_legacy" "originals/pre-anniversary"
fetch_serverbrowser "" "originals/anniversary"
echo "All originals fetched successfully."
