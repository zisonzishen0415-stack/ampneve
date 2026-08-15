#!/usr/bin/env python3
"""Map FS.bin (Zoom effect-library image): global header, 32-byte file-table
items (block nr, size, name), and embedded ZDL extraction + integrity check
against the stock_zdls corpus.

Usage: python tools/fsbin_map.py <FS.bin> [--extract DIR] [--corpus DIR]
"""
import sys, struct, os, hashlib

ZDL_SIG = b"\x00\x00\x00\x00SIZE"

FS_BASE = 0xA006   # data-region base: offset = FS_BASE + block*4096 (block 1 -> 0xB006)

def item_pos(blk):
    return FS_BASE + blk * 4096

def main():
    fsbin = sys.argv[1]
    extract_dir = None
    corpus_dir = None
    if "--extract" in sys.argv:
        extract_dir = sys.argv[sys.argv.index("--extract") + 1]
    if "--corpus" in sys.argv:
        corpus_dir = sys.argv[sys.argv.index("--corpus") + 1]

    data = open(fsbin, "rb").read()
    print(f"FS.bin: {len(data)} bytes ({len(data)/1024:.0f} KB)")

    # ---- global header ----
    print("\n== global header (first 32 bytes) ==")
    for i in range(0, 32, 4):
        print(f"  +0x{i:02X}: {data[i:i+4].hex()}  u16={struct.unpack_from('<H', data, i)[0]}  u32={struct.unpack_from('<I', data, i)[0]}")

    # ---- scan 32-byte file-table items (cheap-filter byte loop) ----
    print("\n== file-table items ==")
    items = []
    seen = set()
    i = 0
    n = len(data)
    while i < n - 32:
        if data[i + 2] != 1 or data[i + 3] != 0xFF:   # cheap reject
            i += 1
            continue
        blk = struct.unpack_from("<H", data, i)[0]
        size = struct.unpack_from("<I", data, i + 4)[0]
        name = data[i + 8:i + 20].split(b"\0")[0].decode("latin1", "replace")
        name = name.encode("ascii", "replace").decode("ascii")
        tail = data[i + 20:i + 32]
        if 0 < size < 0x100000 and name and tail == b"\xFF" * 12 \
           and blk * 4096 + FS_BASE < n and "?" not in name:
            key = (blk, name)
            if key not in seen:
                seen.add(key)
                items.append((i, blk, size, name))
            i += 32
            continue
        i += 1
    print(f"  {len(items)} items found")
    if not items:
        return

    # group by contiguous runs (each run = one table)
    runs = []
    for it in items:
        if runs and it[0] == runs[-1][-1][0] + 32:
            runs[-1].append(it)
        else:
            runs.append([it])
    print(f"  {len(runs)} contiguous table run(s)")
    for ri, run in enumerate(runs):
        blks = [it[1] for it in run]
        print(f"    run {ri}: {len(run)} items @0x{run[0][0]:X}, blocks {min(blks):04X}..{max(blks):04X}, "
              f"first={run[0][3]}")

    # ---- validate: FS_BASE + block*4096 -> ZDL sig + size match ----
    print("\n== validation (offset = 0xA006 + block*4096) ==")
    ok = bad = 0
    for (off, blk, size, name) in items:
        pos = item_pos(blk)
        have_sig = data[pos:pos + 8] == ZDL_SIG
        # ZDL internal size fields: header+8 = SIZE payload size; +16 = ELF size
        elf_size = struct.unpack_from("<I", data, pos + 16)[0] if have_sig else 0
        sz_ok = (elf_size + 76) <= size if have_sig else False
        if have_sig:
            ok += 1
            if not sz_ok:
                print(f"    ? {name}: block {blk:04X} pos 0x{pos:X} sig OK but size {size} < ELF+76 {elf_size + 76}")
        else:
            bad += 1
            print(f"    ! {name}: block {blk:04X} pos 0x{pos:X} NO ZDL SIG")
    print(f"  sig OK: {ok}, no-sig: {bad}")

    # ---- ZDL sig scan vs table coverage (fast find-based) ----
    sigs = []
    start = 0
    while True:
        s = data.find(ZDL_SIG, start)
        if s < 0:
            break
        sigs.append(s)
        start = s + 1
    print(f"\n== raw ZDL sig scan: {len(sigs)} hits ==")
    covered = 0
    for s in sigs:
        if any(s == item_pos(it[1]) for it in items):
            covered += 1
    print(f"  at table offsets: {covered}, elsewhere: {len(sigs) - covered}")
    for s in sigs:
        if not any(s == item_pos(it[1]) for it in items):
            print(f"    orphan sig @0x{s:X}")

    # ---- extract ----
    if extract_dir:
        os.makedirs(extract_dir, exist_ok=True)
        for (off, blk, size, name) in items:
            pos = item_pos(blk)
            with open(os.path.join(extract_dir, name), "wb") as f:
                f.write(data[pos:pos + size])
        print(f"\nextracted {len(items)} files -> {extract_dir}")

    # ---- corpus compare ----
    if corpus_dir:
        print("\n== corpus comparison ==")
        found = matched = 0
        for (off, blk, size, name) in items:
            pos = item_pos(blk)
            blob = data[pos:pos + size]
            h = hashlib.sha256(blob).hexdigest()
            cpath = None
            for cand in (os.path.join(corpus_dir, name),
                         os.path.join(corpus_dir, "G1on_" + name),
                         os.path.join(corpus_dir, "MS70_" + name)):
                if os.path.exists(cand):
                    cpath = cand
                    break
            if cpath:
                found += 1
                if hashlib.sha256(open(cpath, "rb").read()).hexdigest() == h:
                    matched += 1
                else:
                    print(f"    DIFFERS: {name} (fsbin vs {os.path.basename(cpath)})")
        print(f"  corpus hits: {found}/{len(items)}, byte-identical: {matched}")

if __name__ == "__main__":
    main()
