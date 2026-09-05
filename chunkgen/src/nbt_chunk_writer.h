/*
 * Copyright (c) 2026, Pixel Brush <pixelbrush.dev>
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#pragma once
#include "chunk.h"
#include <cstdint>
#include <memory>
#include <string>

namespace NbtChunkWriter {
// Writes the given chunk as a standalone, gzip-compressed NBT file: the same
// "Level" compound (xPos, zPos, Blocks, Data, SkyLight, BlockLight, HeightMap,
// TerrainPopulated, Entities, TileEntities, ...) the server itself builds via
// BuildChunkLevelTag() (see ../../src/bpp_shared/world/storage/region.cpp),
// gzip-compressed the same way save_manager.h writes level.dat/player data.
// Returns true on success.
bool WriteChunkToFile(const std::shared_ptr<Chunk>& _chunk, int64_t _timestamp, const std::string& _outPath);
} // namespace NbtChunkWriter
