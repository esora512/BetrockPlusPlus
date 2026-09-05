# chunkgen: design decisions

This tool was built to produce a single Beta 1.7.3 chunk as NBT, using the
*actual* world generation code from this repository (not a re-implementation),
so its output is bit-identical to what the BetrockPlusPlus server itself would
generate. This file records the non-obvious engineering decisions made along
the way and why.

## 0. Fixed bug: missing `Blocks::RegisterAll()` corrupted every generated chunk

**This was the root cause of an early, badly wrong build of this tool**: no
trees, wildly over-placed mushrooms (dozens to ~90 mushroom blocks in a
single chunk instead of the usual handful), scrambled-looking terrain.

The real game runtime always calls `Blocks::RegisterAll()` once at startup
(see `Runtime`'s constructor in `src/bpp_shared/runtime.h`) before touching
any `Chunk`. That call populates the global `Blocks::blockProperties[]`
table -- light opacity, material (solid/liquid/air), hardness, etc. -- for
every block type. `chunkgen` never went through `Runtime` (it builds a
`WorldManager` directly), so it never called this, and every block silently
kept its all-default `BlockProperties` (notably `lightOpacity = 255`, i.e.
*fully opaque*, for every block **including air**).

That one missing call corrupted generation in multiple places at once,
because so much of world gen depends on `blockProperties[]`:
- `Chunk::GenerateHeightMapColumn()` walks down from the top of the chunk
  looking for the first block with `lightOpacity > 0`. With air itself
  reporting opacity 255, it always "found" one at y=127, so **every column's
  height map was a flat 128** regardless of actual terrain.
- Tree placement (`TreeGenerator::GenerateTree` / `GenerateTaiga` /
  `GenerateAltTaiga` / `BigTree`) plants at `y = world.GetHeightValue(x, z)`
  and immediately rejects placement once `y + treeHeight + 1 > CHUNK_HEIGHT`.
  With height map stuck at 128, that check failed unconditionally -- **zero
  trees were ever placed**, in any biome, regardless of seed.
- Mushroom/flower placement (`FeatureGenerator::GenerateFlowers`) and other
  `IsSolid`/`IsOpaque`-gated checks throughout population read from the same
  corrupted table, which made their validity checks pass far more often than
  they should -- observed as **dozens of mushroom blocks per chunk** instead
  of the small 1-8 block patches Beta 1.7.3 actually produces.

The fix is one line in `chunkgen`'s `main.cpp`: call `Blocks::RegisterAll()`
before constructing any `WorldManager`/`Chunk`. Verified after the fix (seed
999, x/z 0..15, Overworld): height maps now vary realistically (~64-90
instead of a flat 128), trees appear at normal forest density (~1500+ log
blocks across 256 chunks vs. 0 before), and mushrooms appear rarely and in
small patches (6/256 chunks, 1-5 blocks each) instead of constantly and in
bulk. A full dungeon (2 loot chests + mob spawner) generated afterward looks
correct too. See `test/run_tests.sh` for the repeatable regression check.

**Lesson for anyone extending this tool**: if you add a new standalone entry
point that constructs `WorldManager`/`Chunk` objects directly (bypassing
`Runtime`), you must call `Blocks::RegisterAll()` (and, if item behavior ever
matters for what you're doing, `Items::RegisterAll()` -- not currently called
here, see #3) yourself first. There's no compile-time or runtime error if you
forget; everything just silently uses wrong defaults.

## 1. Location & source reuse strategy

`chunkgen/` is a sibling CMake project inside this repository, not a copy of
it. Its `CMakeLists.txt` compiles the real files straight out of
`../src/bpp_shared` (generator, noise, biome, chunk, NBT, region code) into a
static library (`bpp_worldgen`) and links a small CLI on top. Nothing about
world generation is reimplemented from memory or re-derived from
documentation: the noise generators, `OverworldGenerator`/`NetherGenerator`,
`BiomeGenerator`, cave/feature/tree generators, and the chunk NBT encoder are
the exact translation units the server links into `BetrockPlusPlus` itself.
A side benefit: future changes to world generation in this repo are picked up
by `chunkgen` automatically, with no manual porting step.

## 2. Extracted `BuildChunkLevelTag()` out of `Region::EncodeNbtData()`

`Region::EncodeNbtData()` (in `src/bpp_shared/world/storage/region.cpp`)
built the chunk's "Level" NBT compound (`xPos`, `zPos`, `Blocks`, `Data`,
`SkyLight`, `BlockLight`, `HeightMap`, `TerrainPopulated`, `LastUpdate`,
`Entities`, `TileEntities`) inline, then zlib-compressed it for storage inside
a region file. That tag-building logic is exactly what `chunkgen` needs too,
so it was pulled out into a free function, `BuildChunkLevelTag()` (declared in
`region.h`), with `EncodeNbtData()` now just calling it and compressing the
result. This is a behavior-preserving refactor (verified: it produced
byte-identical region-file output before/after, other than by construction),
done specifically so `chunkgen` reuses the real encoder instead of
duplicating ~100 lines of tag-building code that could drift out of sync.

## 3. Split `EatFood()` out of `item_properties.cpp`

`Inventory`'s stack-merging logic (used when dungeon chest loot is generated
during population) calls `Items::GetMaxStack()`/`GetMaxDurability()`, which
live in `src/bpp_shared/items/item_properties.cpp`. That file also defined
`EatFood()` (a player item-use callback), which needs a complete
`PlayerSession` and therefore `#include`d `src/bpp_server/server.h` -- pulling
the entire networking/command/session stack into a file that otherwise only
holds static item data tables. `EatFood()` was moved to a new file,
`item_properties_interactions.cpp`, which includes
`player_conn/player_session.h` directly instead. Both files were added to the
parent project's `SHARED_SOURCES` (server/client builds still get the exact
same behavior); `chunkgen` links only the now-lightweight
`item_properties.cpp`. `tool_item_properties.cpp` (which registers `EatFood`
and other player-only behaviors, and itself needs `server.h`) is excluded
from `chunkgen` for the same reason -- nothing reachable from world
generation touches those behavior tables.

## 4. libdeflate dependency

The parent project gets `libdeflate` via vcpkg. `chunkgen` doesn't assume a
vcpkg toolchain: it tries `find_package(libdeflate CONFIG QUIET)` first, and
falls back to fetching+building `libdeflate` from source (pinned to `v1.19`,
the same major version the parent project's `vcpkg.json` would resolve to) if
no system package is found. This kept the tool buildable in this sandbox,
which has the libdeflate runtime `.so` but not the dev headers/CMake config.

## 5. Standalone NBT file format: gzip, not the region's raw zlib framing

Inside a `.mcr` region file, a chunk's NBT payload is raw-zlib-compressed and
wrapped in a 5-byte length+format header, then padded to a 4096-byte sector --
none of that framing means anything outside a region file. For a **standalone**
`.nbt` file we instead gzip-compress the raw NBT bytes, matching how this
codebase already writes other standalone NBT files (see
`src/bpp_shared/world/storage/save_manager.h`'s `level.dat`/player-data
writers, which use `libdeflate_gzip_compress`). This is also the convention
most third-party NBT tools (NBTExplorer, Amulet, etc.) expect for a single-file
NBT dump.

## 6. `LastUpdate` is wall-clock time at generation, not meaningful

The `LastUpdate` tag records the tick/timestamp a chunk was last saved. A
chunk generated in isolation by this tool was never "played", so there is no
authentic tick count to put there. `chunkgen` writes the current wall-clock
time (epoch seconds) purely so the field holds a plausible-looking value;
don't rely on it for anything -- two identical invocations a few seconds apart
produce byte-identical output *except* for this one field (verified: see
`test/`).

## 7. Reproducing "populate" fidelity: the 3x3 terrain / 2x2 populate strategy

This is the least obvious part of the whole tool and worth explaining in
full, because it directly affects how faithfully a single requested chunk
matches what a live server would have generated.

Beta 1.7.3 generates a chunk in two passes:

1. **Terrain** (`GenerateChunk`): fully self-contained given only the world
   seed and the chunk's own coordinates (noise-based terrain, biome-based
   surface blocks, caves). No neighboring chunks are read or written.
2. **Population** (`PopulateChunk`): decorates a chunk with trees, ores,
   dungeons, flowers, snow, etc. Every decoration coordinate offset in both
   `OverworldGenerator::PopulateChunk` and `NetherGenerator::PopulateChunk` is
   of the form `blockX + rand.NextInt(16) + 8`, i.e. always in `[+8, +23]`
   relative to the chunk's own block origin -- **always forward, never
   negative**. That means populating chunk `(px, pz)` can only write blocks
   into the 2x2 block of chunks `(px, pz)`, `(px+1, pz)`, `(px, pz+1)`,
   `(px+1, pz+1)`, and requires terrain to already exist for that same 2x2
   block (to read heights/existing blocks) -- this is exactly the readiness
   rule already implemented in `WorldManager::PopulateReady()`
   (`CanPopulateDirect`/`ordered` in `src/bpp_shared/world/world.cpp`).

   Working backwards: the final content of a target chunk `(cx, cz)` can be
   affected by population "spillover" from `(cx-1, cz-1)`, `(cx-1, cz)`,
   `(cx, cz-1)`, and `(cx, cz)` itself -- and each of *those* needs terrain
   for its own NE 2x2 block. Taking the union of all of that terrain
   dependency comes out to exactly the 3x3 chunk neighborhood centered on the
   target.

   So `chunkgen`:
   - generates terrain (`GenerateChunk`) for the full 3x3 neighborhood
     `(cx-1..cx+1, cz-1..cz+1)`,
   - then calls the real, unmodified `WorldManager::PopulateReady(4)`, which
     populates exactly the 4 chunks whose NE-neighbours have terrain --
     `(cx-1,cz-1)`, `(cx-1,cz)`, `(cx,cz-1)`, `(cx,cz)` -- in the same
     x-ascending/z-ascending order the live server uses,
   - and writes out only the target chunk `(cx, cz)`.

   This exactly mirrors what a live server does the *first* time it loads a
   previously-untouched region centered on the target chunk.

### The unavoidable caveat: population is history-dependent

Beta 1.7.3's decoration pass has a known, inherent property: it reads
back live state (e.g. `WorldWrapper::GetHeightValue`/`FindTopSolidBlock`,
which reflect whatever has already been written by *earlier* population
calls, including ones from further-out chunks that spilled into this
neighborhood). This means the "true" content of a chunk near tree lines,
snow, or dungeon chest RNG draws can, in principle, depend on the entire
population history of every chunk ever loaded near it in a real, long-running
server -- not just its immediate neighbors. This is a real property of the
original game, not a bug introduced here (the same
`WorldManager::PopulateReady()` this tool reuses has this exact same
limitation for any server that loads a fresh area for the first time).

`chunkgen` reproduces the "freshly discovered area" case exactly (bit for
bit, per the determinism check in `test/`). It cannot account for divergent
results caused by an existing server having loaded/populated a different,
larger footprint around the same coordinates first. There is no way to fix
this without knowing that server's exact chunk-load history, so it's
documented here as a known limitation rather than worked around.

## 8. Dungeon chests/spawners are real, `TileEntities`/`Entities` are not forced empty

Dungeons (with a mob spawner and up to 2 loot chests) are a genuine part of
Beta 1.7.3's population pass and are reproduced faithfully; when a generated
chunk happens to contain one, `TileEntities` will be non-empty. `Entities` is
empty in practice because Beta 1.7.3 world generation never creates live
entities directly (mob spawning is a separate, tick-driven runtime system --
`EntitySpawner`/`WorldManager::PerformRandomTicks` -- not part of chunk
generation), not because it's forced to be empty.

## 9. `--x`/`--z` are chunk coordinates, not block coordinates

Matches the units `xPos`/`zPos` are stored in on disk (block coordinate
divided by 16, rounded toward negative infinity).

## 10. Side effect: a `logs/` directory appears next to wherever you run it

`chunkgen` links the shared `Logger` (`src/bpp_shared/logger/`), which
creates a `logs/<timestamp>.log` file in the current working directory on
startup, same as the main server/client. This is intentional (surfacing
warnings from the reused NBT/inventory code is more useful than suppressing
them) but is worth knowing about before scripting many invocations.
