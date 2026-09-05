/*
 * Copyright (c) 2026, Pixel Brush <pixelbrush.dev>
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 *
*/

// Item behaviors that require a complete PlayerSession (and therefore the
// server/networking stack), split out of item_properties.cpp so that code
// which only needs item data tables (GetMaxStack, GetMaxDurability, ...) can
// link without pulling in server.h. See item_properties.cpp.

#include "item_properties.h"
#include "../entities/entity_mobile.h"
#include "player_conn/player_session.h"

namespace Items {

void EatFood(PlayerSession& _session, ItemStack* _stack, Entity& _target) {
	// If it's not a mobile entity, we can't heal it, since it doesn't have health
	auto* mobile = dynamic_cast<MobileEntity*>(&_target);
	if (!mobile || !_stack || !IsFood(_stack->id))
		return;
	mobile->Heal(GetRegenerationAmount(_stack->id));
	// Give the bowl back
	// This looks stupid, and it probably is,
	// but it avoids the bowl appearing in the slot after a now-empty slot
	bool giveBowlBack = false;
	if (_stack->id == Items::Id::MUSHROOM_STEW)
		giveBowlBack = true;
	_stack->DecrementCount(1);
	if (giveBowlBack) {
		ItemStack itemStack = ItemStack{ Items::Id::BOWL, 0, 1 };
		_session.inventory.PickupItem(itemStack);
	}
}

}; // namespace Items
