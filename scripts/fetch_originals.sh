#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

fetch_serverbrowser() {
    local branch_flag="$1"
    local outdir="$2"
    local tmpdir=$(mktemp -d)

    DepotDownloader \
        -app 10 -depot 2 $branch_flag -os windows \
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

fetch_goldclient() {
    local tmpdir=$(mktemp -d)
    local url="https://github.com/gold-plus/builds/releases/download/2.5.6.0/CS_GoldClient.zip"

    echo "Fetching GoldClient from $url ..."
    wget -qO "$tmpdir/gc.zip" "$url"
    unzip -qo "$tmpdir/gc.zip" -d "$tmpdir/gc"

    local src=$(find "$tmpdir/gc" -path "*/CS 1.6 GoldClient/steam_api.dll" -print -quit)
    [[ -f "$src" ]] || src=$(find "$tmpdir/gc" -iname "steam_api.dll" -print -quit)
    [[ -f "$src" ]] || { echo "FAIL: steam_api.dll not found in GoldClient zip"; find "$tmpdir/gc" -type f; exit 1; }

    mkdir -p "$ROOT/originals/goldclient"
    cp "$src" "$ROOT/originals/goldclient/steam_api.dll"
    echo "OK: originals/goldclient/steam_api.dll ($(stat -c%s "$ROOT/originals/goldclient/steam_api.dll") bytes)"
    rm -rf "$tmpdir"
}

fetch_goldclient
echo "All originals fetched successfully."
