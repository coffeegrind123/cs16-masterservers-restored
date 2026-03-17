#!/usr/bin/env python3
"""Patch CSPro/GoldClient steam_api.dll to replace GMS master server URLs.

The URLs are XOR-obfuscated as immediate operands in XOR EAX instructions (83 F0 XX).
Each byte at offset+2 from the 83 F0 prefix is the plaintext ASCII character.
"""
import sys
import shutil

# Original URLs and their XOR imm8 byte file offsets
URLS = [
    {
        "original": "mscf1.cs-css.com:8880",
        "replacement": "master1.cs16.net:8880",
        "offsets": [
            0x4432B, 0x4433D, 0x44351, 0x44365, 0x44379, 0x4438D,
            0x443A1, 0x443B5, 0x443C9, 0x443DD, 0x443F1, 0x44405,
            0x44419, 0x4442D, 0x44441, 0x44455, 0x44469, 0x44484,
            0x44498, 0x444AC, 0x444C0,
        ],
    },
    {
        "original": "mscf2.cs-css.com:2052",
        "replacement": "master1.cs16.net:8880",
        "offsets": [
            0x4465E, 0x44670, 0x44684, 0x44698, 0x446AC, 0x446C0,
            0x446D4, 0x446E8, 0x446FC, 0x44710, 0x44724, 0x44738,
            0x4474C, 0x44760, 0x44774, 0x44788, 0x4479C, 0x447B0,
            0x447CB, 0x447DF, 0x447F3,
        ],
    },
    {
        "original": "mssw1.cs-css.com:80",
        "replacement": "msrv1.cs16.net:8880",
        "offsets": [
            0x44989, 0x4499B, 0x449AF, 0x449C3, 0x449D7, 0x449EB,
            0x449FF, 0x44A13, 0x44A27, 0x44A3B, 0x44A4F, 0x44A63,
            0x44A77, 0x44A8B, 0x44A9F, 0x44AB3, 0x44AC7, 0x44AE2,
            0x44AF6,
        ],
    },
]

def patch(input_path, output_path=None):
    if output_path is None:
        output_path = input_path

    with open(input_path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < 4000000:
        print(f"ERROR: File too small ({len(data)} bytes), expected ~4MB GoldClient steam_api.dll")
        return False

    total_patched = 0
    for url_info in URLS:
        orig = url_info["original"]
        repl = url_info["replacement"]
        offsets = url_info["offsets"]

        assert len(orig) == len(repl) == len(offsets), \
            f"Length mismatch: {orig}({len(orig)}) vs {repl}({len(repl)}) vs offsets({len(offsets)})"

        # Verify original bytes
        verified = True
        for i, off in enumerate(offsets):
            if off >= len(data) or data[off - 2] != 0x83 or data[off - 1] != 0xF0:
                print(f"  WARNING: Unexpected instruction at 0x{off:X} for {orig}[{i}] ('{orig[i]}')")
                verified = False
            elif data[off] != ord(orig[i]):
                print(f"  WARNING: Expected '{orig[i]}' (0x{ord(orig[i]):02X}) at 0x{off:X}, got 0x{data[off]:02X}")
                verified = False

        if not verified:
            print(f"SKIP: {orig} — verification failed")
            continue

        # Patch
        for i, off in enumerate(offsets):
            data[off] = ord(repl[i])

        print(f"  {orig} -> {repl} ({len(offsets)} bytes patched)")
        total_patched += len(offsets)

    if total_patched == 0:
        print("ERROR: No URLs were patched")
        return False

    with open(output_path, "wb") as f:
        f.write(data)

    print(f"OK: {total_patched} bytes patched in {output_path}")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <steam_api.dll> [output.dll]")
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
