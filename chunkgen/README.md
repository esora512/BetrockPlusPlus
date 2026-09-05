# chunkgen

A standalone CLI that generates a single Beta 1.7.3 chunk and writes it out as
NBT. It does not reimplement world generation -- it compiles and links the
real generator/noise/biome/NBT source files from the parent BetrockPlusPlus
repository, so its output is bit-identical to what the server itself would
generate for the same seed and coordinates. See [DECISIONS.md](DECISIONS.md)
for the reasoning behind every non-obvious choice made below.

## Building

Requires CMake 3.25+, a C++23 compiler (tested with GCC 13), and network
access the first time you configure (to fetch `libdeflate` if no system copy
with CMake config is found).

```sh
cd chunkgen
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

This produces `build/chunkgen`.

## Running

```sh
./build/chunkgen --seed <int64> --dimension <0|-1> --x <chunk_x> --z <chunk_z> --out <file.nbt>
```

| Flag           | Meaning                                                              |
|----------------|-----------------------------------------------------------------------|
| `--seed`       | World seed, signed 64-bit integer                                    |
| `--dimension`  | `0` for the Overworld, `-1` for the Nether (Beta 1.7.3's only two)   |
| `--x`, `--z`   | **Chunk** coordinates (block coordinate / 16), not block coordinates |
| `--out`        | Output `.nbt` file path                                              |

Example:

```sh
./build/chunkgen --seed 12345 --dimension 0 --x 0 --z 0 --out chunk_0_0.nbt
./build/chunkgen --seed 12345 --dimension -1 --x 0 --z 0 --out chunk_nether_0_0.nbt
```

Exits `0` on success, non-zero (with a message on stderr) on failure. A
`logs/` directory will appear next to wherever you run it (see
[DECISIONS.md #10](DECISIONS.md#10-side-effect-a-logs-directory-appears-next-to-wherever-you-run-it)).

## Output format

A gzip-compressed NBT file containing a root compound with a single `Level`
compound, matching the same fields the server writes into a region file for
this chunk (see `Region::BuildChunkLevelTag` in
`../src/bpp_shared/world/storage/region.cpp`):

| Field              | Type                | Notes                                            |
|--------------------|---------------------|---------------------------------------------------|
| `xPos`, `zPos`     | `TAG_Int`           | Chunk coordinates                                  |
| `TerrainPopulated` | `TAG_Byte`          | Always `1` for this tool's output                  |
| `LastUpdate`       | `TAG_Long`          | Wall-clock time at generation, not authoritative (see DECISIONS.md #6) |
| `Blocks`           | `TAG_Byte_Array`    | 32768 bytes, one block id per position             |
| `Data`             | `TAG_Byte_Array`    | 16384 bytes, packed block-metadata nibbles         |
| `SkyLight`         | `TAG_Byte_Array`    | 16384 bytes, packed sky-light nibbles              |
| `BlockLight`       | `TAG_Byte_Array`    | 16384 bytes, packed block-light nibbles            |
| `HeightMap`        | `TAG_Byte_Array`    | 256 bytes, one per (x,z) column                    |
| `Entities`         | `TAG_List` (empty)  | Worldgen never creates live entities directly      |
| `TileEntities`     | `TAG_List`          | Non-empty when the chunk contains a dungeon (mob spawner + up to 2 loot chests) |

The block/nibble array layout matches the standard Beta/Alpha on-disk
ordering used elsewhere in this codebase (`y + z*128 + x*128*16` for
`Blocks`, same index / 2 with y-parity nibble packing for the rest).

## Verifying output

`test/verify_nbt.py` is a small, dependency-free NBT reader used to sanity
check a generated file's structure (array lengths, presence of required
fields, tile entity contents). `test/run_tests.sh` runs a fixed set of
seed/dimension/coordinate combinations through `chunkgen`, validates each
with `verify_nbt.py`, and checks that regenerating the same chunk produces
byte-identical output (aside from the `LastUpdate` timestamp), proving
generation is deterministic:

```sh
test/run_tests.sh            # uses ./build/chunkgen by default
test/run_tests.sh /path/to/chunkgen
```

## Known limitations / assumptions

- **Population order-dependence**: Beta 1.7.3's decoration pass can, in
  principle, be affected by the population history of an unbounded region of
  previously-loaded chunks (see DECISIONS.md #7 for the full explanation).
  `chunkgen` reproduces the "server loading this area for the very first
  time" case exactly; it cannot account for divergence caused by a live
  server having populated a different surrounding footprint first. There is
  no seed-and-coordinates-only way to fix this -- it's an inherent property
  of the original game.
- **No structures beyond dungeons**: Beta 1.7.3 doesn't generate villages,
  strongholds, or mineshafts, so there's nothing missing there. Dungeons
  (mob spawner + chest loot) are generated for real.
- **No mob spawning**: entities are never created by world generation itself
  in Beta 1.7.3 (mob spawning is a separate, tick-driven runtime system), so
  `Entities` is always empty -- this isn't a shortcut this tool takes, it's
  how the original game works.
- **`LastUpdate` is not meaningful** -- see DECISIONS.md #6.
- **Requires network access on first configure** if `libdeflate`'s CMake
  package isn't already installed (see DECISIONS.md #4).
