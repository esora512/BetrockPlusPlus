/*
 * Copyright (c) 2026, Pixel Brush <pixelbrush.dev>
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "nbt_chunk_writer.h"
#include "nbt/nbt.h"
#include "world/storage/region.h"
#include <fstream>
#include <libdeflate.h>

namespace NbtChunkWriter {

bool WriteChunkToFile(const std::shared_ptr<Chunk>& _chunk, int64_t _timestamp, const std::string& _outPath) {
	// Reuse the exact same tag-building logic the server uses when writing a
	// chunk into a region file, so the payload is bit-identical.
	Tag root = BuildChunkLevelTag(_chunk, _timestamp, nullptr);

	std::vector<uint8_t> raw;
	NBTwriter writer(raw, root);

	libdeflate_compressor* compressor = libdeflate_alloc_compressor(6);
	if (!compressor)
		return false;
	size_t maxSize = libdeflate_gzip_compress_bound(compressor, raw.size());
	std::vector<uint8_t> compressed(maxSize);
	size_t actualSize = libdeflate_gzip_compress(compressor, raw.data(), raw.size(), compressed.data(), maxSize);
	libdeflate_free_compressor(compressor);
	if (actualSize == 0)
		return false;
	compressed.resize(actualSize);

	std::ofstream file(_outPath, std::ios::binary);
	if (!file.is_open())
		return false;
	file.write(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
	return file.good();
}

} // namespace NbtChunkWriter
