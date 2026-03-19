#!/usr/bin/env python3
"""Patch ServerBrowser.dll to load mastersrv.dll instead of steam_api.dll"""
import sys
import shutil

ORIGINAL = b"steam_api.dll\x00"
PATCHED  = b"mastersrv.dll\x00"

def patch(input_path, output_path=None):
    if output_path is None:
        output_path = input_path

    with open(input_path, "rb") as f:
        data = bytearray(f.read())

    idx = data.find(ORIGINAL)
    if idx == -1:
        patched_check = data.find(PATCHED)
        if patched_check != -1:
            print(f"Already patched: {input_path}")
            if output_path != input_path:
                with open(output_path, "wb") as f:
                    f.write(data)
            return True
        print(f"ERROR: Could not find '{ORIGINAL.decode()}' in {input_path}")
        return False

    count = 0
    pos = 0
    while True:
        pos = data.find(ORIGINAL, pos)
        if pos == -1:
            break
        data[pos:pos+len(PATCHED)] = PATCHED
        print(f"  Patched at file offset 0x{pos:X}")
        pos += len(PATCHED)
        count += 1

    with open(output_path, "wb") as f:
        f.write(data)

    print(f"OK: {count} occurrence(s) patched in {output_path}")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <ServerBrowser.dll> [output.dll]")
        sys.exit(1)

    inp = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None

    if out is None:
        backup = inp + ".bak"
        shutil.copy2(inp, backup)
        print(f"Backup: {backup}")
        out = inp

    if not patch(inp, out):
        sys.exit(1)
