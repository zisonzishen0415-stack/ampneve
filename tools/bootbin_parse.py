#!/usr/bin/env python3
"""Parse the Zoom boot.bin container: 'TIPAcYSX' magic + YSX chunks.

Chunk framing (load segments):
    [u8 type] "YSX" [u32 addr] [u32 size] [size bytes data]
The first handful of chunks (header records) use the same tag but carry
short config payloads (version / entry / checksums) instead of segments.

Usage: python tools/bootbin_parse.py <boot.bin> [--dump DIR]
"""
import sys, struct, os

MAGIC = b"TIPAcYSX"

def main():
    path = sys.argv[1]
    dump_dir = None
    if "--dump" in sys.argv:
        dump_dir = sys.argv[sys.argv.index("--dump") + 1]
    data = open(path, "rb").read()
    print(f"boot.bin: {len(data)} bytes; magic: {data[:8]!r}")

    if data[:8] != MAGIC:
        print("not a TIPAcYSX container")
        return

    # iterate load segments: pattern [0x01]"YSX"[addr u32][size u32]
    chunks = []
    pos = 8
    while True:
        p = data.find(b"\x01YSX", pos)
        if p < 0:
            break
        addr, size = struct.unpack_from("<II", data, p + 4)
        payload_off = p + 12
        payload = data[payload_off:payload_off + size]
        plausible = (0xC0000000 <= addr <= 0xC2000000 or 0x11800000 <= addr <= 0x11880000) \
            and 0 < size < 0x100000 and payload_off + size <= len(data)
        if plausible:
            chunks.append((payload_off, addr, size))
            print(f"  seg @0x{p:06X}: addr=0x{addr:08X} size={size} (0x{size:X}) "
                  f"head={payload[:12].hex()}")
            pos = payload_off + size
        else:
            pos = p + 4
    print(f"\n{len(chunks)} load segments")

    # reconstruct runtime image
    img = {}
    for off, addr, size in chunks:
        img[addr] = data[off:off + size]
    if img:
        lo = min(img); hi = max(a + len(b) for a, b in img.items())
        print(f"runtime image: {len(img)} segments, 0x{lo:08X}..0x{hi:08X}")
        if dump_dir:
            os.makedirs(dump_dir, exist_ok=True)
            for a, b in sorted(img.items()):
                fn = os.path.join(dump_dir, f"seg_{a:08X}_{len(b):X}.bin")
                open(fn, "wb").write(b)
            print(f"segments dumped -> {dump_dir}")

if __name__ == "__main__":
    main()
