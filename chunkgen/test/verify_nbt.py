#!/usr/bin/env python3
"""Minimal, dependency-free big-endian NBT reader used to sanity-check chunkgen's
output without relying on any third-party NBT library. Not a general-purpose
NBT tool -- just enough to walk a chunk's Level compound and print/validate it.
"""
import gzip
import struct
import sys

(TAG_END, TAG_BYTE, TAG_SHORT, TAG_INT, TAG_LONG, TAG_FLOAT, TAG_DOUBLE, TAG_BYTEARRAY, TAG_STRING, TAG_LIST,
 TAG_COMPOUND, TAG_INTARRAY) = range(12)


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read(self, n):
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def i8(self):
        return struct.unpack(">b", self.read(1))[0]

    def u8(self):
        return struct.unpack(">B", self.read(1))[0]

    def i16(self):
        return struct.unpack(">h", self.read(2))[0]

    def i32(self):
        return struct.unpack(">i", self.read(4))[0]

    def i64(self):
        return struct.unpack(">q", self.read(8))[0]

    def f32(self):
        return struct.unpack(">f", self.read(4))[0]

    def f64(self):
        return struct.unpack(">d", self.read(8))[0]

    def string(self):
        length = struct.unpack(">H", self.read(2))[0]
        return self.read(length).decode("utf-8", errors="replace")

    def payload(self, tag_type):
        if tag_type == TAG_BYTE:
            return self.i8()
        if tag_type == TAG_SHORT:
            return self.i16()
        if tag_type == TAG_INT:
            return self.i32()
        if tag_type == TAG_LONG:
            return self.i64()
        if tag_type == TAG_FLOAT:
            return self.f32()
        if tag_type == TAG_DOUBLE:
            return self.f64()
        if tag_type == TAG_STRING:
            return self.string()
        if tag_type == TAG_BYTEARRAY:
            n = self.i32()
            return list(self.read(n))
        if tag_type == TAG_INTARRAY:
            n = self.i32()
            return [self.i32() for _ in range(n)]
        if tag_type == TAG_LIST:
            inner_type = self.u8()
            n = self.i32()
            return [self.payload(inner_type) for _ in range(n)]
        if tag_type == TAG_COMPOUND:
            result = {}
            while True:
                t = self.u8()
                if t == TAG_END:
                    break
                name = self.string()
                result[name] = self.payload(t)
            return result
        raise ValueError(f"Unknown tag type {tag_type}")

    def named_tag(self):
        t = self.u8()
        if t == TAG_END:
            return None, None
        name = self.string()
        return name, self.payload(t)


def load(path):
    with open(path, "rb") as f:
        raw = f.read()
    try:
        raw = gzip.decompress(raw)
    except OSError:
        pass  # not gzipped
    r = Reader(raw)
    name, root = r.named_tag()
    assert r.pos == len(raw), f"trailing bytes: {len(raw) - r.pos}"
    return root


def main():
    ok = True
    for path in sys.argv[1:]:
        print(f"=== {path} ===")
        root = load(path)
        level = root["Level"]
        blocks = level["Blocks"]
        data = level["Data"]
        sky = level["SkyLight"]
        block_light = level["BlockLight"]
        height = level["HeightMap"]

        checks = [
            ("Level present", "Level" in root),
            ("xPos type", isinstance(level["xPos"], int)),
            ("zPos type", isinstance(level["zPos"], int)),
            ("TerrainPopulated present", "TerrainPopulated" in level),
            ("LastUpdate present", "LastUpdate" in level),
            ("Blocks length == 32768", len(blocks) == 32768),
            ("Data length == 16384", len(data) == 16384),
            ("SkyLight length == 16384", len(sky) == 16384),
            ("BlockLight length == 16384", len(block_light) == 16384),
            ("HeightMap length == 256", len(height) == 256),
            ("Entities is list", isinstance(level["Entities"], list)),
            ("TileEntities is list", isinstance(level["TileEntities"], list)),
        ]
        for label, cond in checks:
            status = "OK" if cond else "FAIL"
            if not cond:
                ok = False
            print(f"  [{status}] {label}")

        nonzero_blocks = sum(1 for b in blocks if (b & 0xFF) != 0)
        block_id_hist = {}
        for b in blocks:
            block_id_hist[b & 0xFF] = block_id_hist.get(b & 0xFF, 0) + 1
        top_blocks = sorted(block_id_hist.items(), key=lambda kv: -kv[1])[:6]
        print(f"  xPos={level['xPos']} zPos={level['zPos']} TerrainPopulated={level['TerrainPopulated']}"
              f" LastUpdate={level['LastUpdate']}")
        print(f"  non-air blocks: {nonzero_blocks}/{len(blocks)}")
        print(f"  top block ids (id:count): {top_blocks}")
        print(f"  #TileEntities={len(level['TileEntities'])} #Entities={len(level['Entities'])}")
        for te in level["TileEntities"]:
            print(f"    TileEntity id={te.get('id')!r} pos=({te.get('x')},{te.get('y')},{te.get('z')})")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
