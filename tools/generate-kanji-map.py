#!/usr/bin/env python3
"""Regenerate character mapping data from the pinned, user-owned C# port.

Development tool only; Python is not a library build or runtime dependency.
Source: SpecQR/SpecQR-CSharp@057c4b3f25e52c4786a8f94c744ff884eedcecfa
        src/SpecQR/KanjiMap.cs
The input is mapping data, not QR encoder implementation source.
"""
import argparse
import base64
import hashlib
from pathlib import Path
import re
import sys

SOURCE_SHA = "057c4b3f25e52c4786a8f94c744ff884eedcecfa"
PACKED_SHA256 = "5f7126c89bb1522b107287b11701d4aca3ab3624b622798997851f76873b9bbf"
BANNER = (
    "// Unicode to QR Kanji values from SpecQR-Swift/SpecQR-CSharp mapping data.\n"
    "// Original mapping: WHATWG Shift_JIS decoder, first eligible code per scalar.\n"
    "// Character mapping data only; no third-party QR encoder implementation.\n"
)


def regenerate(source):
    text = source.read_text(encoding="utf-8")
    start = text.index("private const string Packed")
    end = text.index("private static readonly", start)
    encoded = "".join(re.findall(r'"([A-Za-z0-9+/=]+)"', text[start:end]))
    raw = base64.b64decode(encoded, validate=True)
    if hashlib.sha256(raw).hexdigest() != PACKED_SHA256:
        raise ValueError("Source mapping differs from pinned C# mapping at " + SOURCE_SHA)
    if len(raw) % 4:
        raise ValueError("Mapping records must contain four bytes")
    mappings = {}
    for offset in range(0, len(raw), 4):
        scalar = int.from_bytes(raw[offset:offset + 2], "big")
        code = int.from_bytes(raw[offset + 2:offset + 4], "big")
        if scalar in mappings:
            raise ValueError("Duplicate Unicode scalar")
        if not (0x8140 <= code <= 0x9FFC or 0xE040 <= code <= 0xEBBF):
            raise ValueError("Code is outside the QR Kanji Shift_JIS ranges")
        adjusted = code - (0x8140 if code <= 0x9FFC else 0xC140)
        value = (adjusted >> 8) * 0xC0 + (adjusted & 255)
        if not 0 <= value < 8192:
            raise ValueError("QR Kanji value is outside 13 bits")
        mappings[scalar] = value
    if len(mappings) != 6953:
        raise ValueError("Expected 6,953 mapping entries")
    return BANNER + "".join("{0x%04Xu, 0x%04Xu},\n" % pair for pair in sorted(mappings.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="Pinned SpecQR-CSharp src/SpecQR/KanjiMap.cs")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent.parent / "src" / "kanji_map.inc")
    parser.add_argument("--check", action="store_true", help="Verify checked-in output without writing")
    args = parser.parse_args()
    generated = regenerate(args.source)
    if args.check:
        if args.output.read_text(encoding="utf-8") != generated:
            raise ValueError("Checked-in Kanji mapping does not match regeneration")
        print("Verified 6,953 Kanji mapping entries")
    else:
        args.output.write_text(generated, encoding="utf-8", newline="\n")
        print("Generated 6,953 Kanji mapping entries")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print("error: " + str(error), file=sys.stderr)
        sys.exit(1)
