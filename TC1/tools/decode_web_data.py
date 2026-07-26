#!/usr/bin/env python3
"""
Decode existing hex arrays from web_data.c back into HTML files.
Reads web_data.c, extracts all const unsigned char arrays, writes .html files.
"""

import re
import os
import sys

def decode(infile, outdir):
    os.makedirs(outdir, exist_ok=True)

    with open(infile, "r") as f:
        content = f.read()

    # Pattern: const unsigned char varname[...] = { 0xNN, 0xNN, ... };
    pattern = r'const unsigned char\s+(\w+)(?:\[[^\]]*\])?\s*=\s*\{(.*?)\};'
    matches = re.findall(pattern, content, re.DOTALL)

    if not matches:
        print("No const unsigned char arrays found.")
        return

    for name, hex_data in matches:
        # Extract all hex bytes
        bytes_list = re.findall(r'0x([0-9A-Fa-f]{2})', hex_data)
        data = bytes(int(b, 16) for b in bytes_list)

        outfile = os.path.join(outdir, f"{name}.html")
        with open(outfile, "wb") as f:
            f.write(data)
        print(f"  -> {outfile}  ({len(data)} bytes)")

    print(f"\nDecoded {len(matches)} files to {outdir}/")

if __name__ == "__main__":
    if len(sys.argv) >= 3:
        decode(sys.argv[1], sys.argv[2])
    else:
        # Default: decode from the project file
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(script_dir)
        decode(os.path.join(project_root, "http_server", "web_data.c"),
               os.path.join(project_root, "http_server", "pages"))
