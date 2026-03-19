#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

if [[ -f "$ROOT/.env" ]]; then
    set -a && source "$ROOT/.env" && set +a
fi

if ! command -v DepotDownloader &>/dev/null; then
    echo "Installing DepotDownloader..."
    wget -qO /tmp/dd.zip https://github.com/SteamRE/DepotDownloader/releases/latest/download/DepotDownloader-linux-x64.zip
    sudo unzip -o /tmp/dd.zip -d /usr/local/bin/
    sudo chmod +x /usr/local/bin/DepotDownloader
fi

rm -rf "$ROOT/dist"
bash "$ROOT/scripts/fetch_originals.sh"
make -C "$ROOT" -f Makefile.mingw clean
make -C "$ROOT" -f Makefile.mingw

mkdir -p "$ROOT/dist"
cd "$ROOT/Release" && zip -r "$ROOT/dist/cs16-masterservers-pre-anniversary.zip" . -x '*.bak'
cd "$ROOT/Release-anniversary" && zip -r "$ROOT/dist/cs16-masterservers-anniversary.zip" . -x '*.bak'
cd "$ROOT/Release-goldclient" && zip -r "$ROOT/dist/cs16-masterservers-goldclient.zip" . -x '*.bak'

echo ""
echo "Build complete. Output in dist/"
ls -lh "$ROOT/dist/"
