/*
 * Copyright (c) 2025-2026, Pixel Brush <pixelbrush.dev>
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "../command.h"
#include "../command_manager.h"
#include "blocks.h"
#include "items.h"
#include "strings/labels.h"
#include <algorithm>
#include <string>

// Fills a horizontal rectangular area at a given Y level with a block.
// Usage:
//   /plane <block> <x1> <z1> <x2> <z2> <y>
std::string CommandPlane::Execute(std::vector<std::string>& _parameters, PlayerSession& _session,
                                  WorldManager& _world,
                                  [[maybe_unused]] std::function<void(PlayerSession&)> _transferDimension,
                                  [[maybe_unused]] Server& _server) {
	if (_parameters.size() != 7)
		return ERROR_REASON_SYNTAX;

	// Resolve block from name or numeric id
	BlockType blockType = BLOCK_AIR;
	const std::string& blockArg = _parameters[1];

	// Try numeric id first
	try {
		size_t pos = 0;
		int id = std::stoi(blockArg, &pos);
		if (pos == blockArg.size() && id > BLOCK_AIR && id < BLOCK_MAX) {
			blockType = static_cast<BlockType>(id);
		}
	} catch (...) {
	}

	// Try name lookup (case-insensitive)
	if (blockType == BLOCK_AIR && blockArg != "air") {
		std::string lower = blockArg;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		for (int i = 1; i < BLOCK_MAX; i++) {
			std::string label = blockLabels[i];
			std::transform(label.begin(), label.end(), label.begin(), ::tolower);
			if (label == lower) {
				blockType = static_cast<BlockType>(i);
				break;
			}
		}
	}

	if (blockType == BLOCK_AIR && blockArg != "air")
		return "Unknown block: " + blockArg;

	// Parse coordinates
	int x1, z1, x2, z2, y;
	try {
		x1 = std::stoi(_parameters[2]);
		z1 = std::stoi(_parameters[3]);
		x2 = std::stoi(_parameters[4]);
		z2 = std::stoi(_parameters[5]);
		y = std::stoi(_parameters[6]);
	} catch (...) {
		return ERROR_REASON_PARAMETERS;
	}

	int minX = std::min(x1, x2);
	int maxX = std::max(x1, x2);
	int minZ = std::min(z1, z2);
	int maxZ = std::max(z1, z2);

	if (!WorldManager::InBounds(y))
		return "Y coordinate out of bounds!";

	// Fill the area
	int placed = 0;
	for (int x = minX; x <= maxX; x++) {
		for (int z = minZ; z <= maxZ; z++) {
			_world.SetBlock({ x, y, z }, blockType);
			placed++;
		}
	}

	Packet::ChatMessage reply;
	reply.message = "§ePlaced " + std::to_string(placed) + " " + WIdToLabel(blockType) + " block(s) in filled area at y=" + std::to_string(y);
	reply.Serialize(_session.stream);
	return "";
}
