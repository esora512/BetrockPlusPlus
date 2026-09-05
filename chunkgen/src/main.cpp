/*
 * Copyright (c) 2026, Pixel Brush <pixelbrush.dev>
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// Standalone Beta 1.7.3 chunk generator CLI.
//
// This intentionally does NOT reimplement world generation: it links directly
// against the same generator/noise/biome/NBT source files the BetrockPlusPlus
// server itself uses (see ../CMakeLists.txt's bpp_worldgen target and
// ../DECISIONS.md), and drives them the same way WorldManager's own
// generation pipeline does (see WorldManager::PumpPipeline / PopulateReady in
// world.cpp). Output should therefore be bit-identical to what the real
// server would generate for the same seed/coordinates.

#include "nbt_chunk_writer.h"
#include "world/generator/generator.h"
#include "world/generator/nether/chunk_gen.h"
#include "world/generator/overworld/chunk_gen.h"
#include "world/world.h"

#include <charconv>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Args {
	int64_t seed = 0;
	int32_t dimension = 0;
	int32_t x = 0;
	int32_t z = 0;
	std::string out;
};

void PrintUsage(const char* _argv0) {
	std::cerr << "Usage: " << _argv0 << " --seed <int64> --dimension <0|-1> --x <chunk_x> --z <chunk_z> --out <file.nbt>\n"
	          << "\n"
	          << "  --seed        World seed (signed 64-bit integer)\n"
	          << "  --dimension   0 for the Overworld, -1 for the Nether\n"
	          << "  --x, --z      CHUNK coordinates (block coordinate / 16), not block coordinates\n"
	          << "  --out         Output .nbt file path\n";
}

template <typename T>
std::optional<T> ParseInt(std::string_view _s) {
	T value{};
	auto [ptr, ec] = std::from_chars(_s.data(), _s.data() + _s.size(), value);
	if (ec != std::errc() || ptr != _s.data() + _s.size())
		return std::nullopt;
	return value;
}

// nullopt means "print usage and exit"; caller distinguishes clean --help
// from an actual parse error via `_helpRequested`.
std::optional<Args> ParseArgs(int _argc, char** _argv, bool& _helpRequested) {
	_helpRequested = false;
	Args args;
	bool haveSeed = false, haveDim = false, haveX = false, haveZ = false, haveOut = false;

	auto nextValue = [&](int& _i) -> std::optional<std::string_view> {
		if (_i + 1 >= _argc)
			return std::nullopt;
		return std::string_view(_argv[++_i]);
	};

	for (int i = 1; i < _argc; ++i) {
		std::string_view arg(_argv[i]);
		if (arg == "--help" || arg == "-h") {
			_helpRequested = true;
			return std::nullopt;
		} else if (arg == "--seed") {
			auto v = nextValue(i);
			auto parsedValue = v ? ParseInt<int64_t>(*v) : std::nullopt;
			if (!parsedValue) {
				std::cerr << "Invalid or missing value for --seed\n";
				return std::nullopt;
			}
			args.seed = *parsedValue;
			haveSeed = true;
		} else if (arg == "--dimension") {
			auto v = nextValue(i);
			auto parsedValue = v ? ParseInt<int32_t>(*v) : std::nullopt;
			if (!parsedValue || (*parsedValue != 0 && *parsedValue != -1)) {
				std::cerr << "Invalid or missing value for --dimension (must be 0 or -1)\n";
				return std::nullopt;
			}
			args.dimension = *parsedValue;
			haveDim = true;
		} else if (arg == "--x") {
			auto v = nextValue(i);
			auto parsedValue = v ? ParseInt<int32_t>(*v) : std::nullopt;
			if (!parsedValue) {
				std::cerr << "Invalid or missing value for --x\n";
				return std::nullopt;
			}
			args.x = *parsedValue;
			haveX = true;
		} else if (arg == "--z") {
			auto v = nextValue(i);
			auto parsedValue = v ? ParseInt<int32_t>(*v) : std::nullopt;
			if (!parsedValue) {
				std::cerr << "Invalid or missing value for --z\n";
				return std::nullopt;
			}
			args.z = *parsedValue;
			haveZ = true;
		} else if (arg == "--out") {
			auto v = nextValue(i);
			if (!v) {
				std::cerr << "Missing value for --out\n";
				return std::nullopt;
			}
			args.out = std::string(*v);
			haveOut = true;
		} else {
			std::cerr << "Unknown argument: " << arg << "\n";
			return std::nullopt;
		}
	}

	if (!haveSeed || !haveDim || !haveX || !haveZ || !haveOut) {
		std::cerr << "Missing required argument(s)\n";
		return std::nullopt;
	}
	return args;
}

// Generates terrain (no decorations yet) for a single chunk, exactly the way
// WorldManager's own generation-pool worker does it -- see the
// `startGeneration` lambda in WorldManager::PumpPipeline (world.cpp): construct
// the seeded generator, call GenerateChunk(), build the skylight map, then
// mark the chunk Generated.
std::shared_ptr<Chunk> GenerateTerrainChunk(Generator& _gen, Int32_2 _cpos) {
	auto chunk = std::make_shared<Chunk>();
	chunk->cpos = _cpos;
	_gen.GenerateChunk(*chunk);
	chunk->isModified = true;
	chunk->GenerateSkylightMap();
	chunk->state.store(ChunkState::Generated, std::memory_order_release);
	return chunk;
}

} // namespace

int main(int argc, char** argv) {
	bool helpRequested = false;
	auto parsed = ParseArgs(argc, argv, helpRequested);
	if (!parsed) {
		PrintUsage(argv[0]);
		return helpRequested ? 0 : 1;
	}
	const Args& args = *parsed;
	const bool isHell = (args.dimension == -1);

	WorldManager world(isHell);
	world.InitWorldSeed(args.seed);

	// Beta 1.7.3's decoration pass ("populate") can write blocks up to one
	// chunk to the +X/+Z of the chunk it's decorating -- every placement
	// offset in OverworldGenerator::PopulateChunk / NetherGenerator::PopulateChunk
	// is "+8..+23" from that chunk's own block origin (always forward, never
	// negative). So the final, fully-populated content of the target chunk can
	// be affected by decorations placed while populating its (x-1,z-1),
	// (x-1,z) and (x,z-1) neighbours. To reproduce that exactly, we generate
	// terrain for the full 3x3 neighbourhood around the target chunk and then
	// populate using WorldManager::PopulateReady -- the same readiness rule
	// (a chunk only populates once its (+1,z)/(x,+1)/(+1,+1) neighbours have
	// terrain) and the same x-ascending/z-ascending order the live server
	// uses for a freshly-loaded area. See DECISIONS.md for why this is the
	// best achievable fidelity for a single standalone chunk request.
	std::unique_ptr<Generator> gen;
	if (isHell)
		gen = std::make_unique<NetherGenerator>(args.seed);
	else
		gen = std::make_unique<OverworldGenerator>(args.seed);

	for (int32_t dx = -1; dx <= 1; ++dx) {
		for (int32_t dz = -1; dz <= 1; ++dz) {
			Int32_2 cpos{ args.x + dx, args.z + dz };
			world.chunks[cpos] = GenerateTerrainChunk(*gen, cpos);
		}
	}

	// Populates exactly the 2x2 block (x-1,z-1)..(x,z) -- the only chunks in
	// our 3x3 terrain neighbourhood whose NE-neighbours are all present.
	world.PopulateReady(4);

	auto it = world.chunks.find(Int32_2{ args.x, args.z });
	if (it == world.chunks.end()) {
		std::cerr << "Internal error: target chunk missing after generation\n";
		return 1;
	}
	std::shared_ptr<Chunk> target = it->second;
	if (!target->isTerrainPopulated) {
		std::cerr << "Internal error: target chunk failed to populate\n";
		return 1;
	}

	const int64_t timestamp =
	    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	if (!NbtChunkWriter::WriteChunkToFile(target, timestamp, args.out)) {
		std::cerr << "Failed to write NBT file: " << args.out << "\n";
		return 1;
	}

	std::cout << "Wrote chunk (" << args.x << ", " << args.z << ") [dimension " << args.dimension << ", seed "
	          << args.seed << "] -> " << args.out << "\n";
	return 0;
}
