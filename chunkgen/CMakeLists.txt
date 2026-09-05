cmake_minimum_required(VERSION 3.25.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(chunkgen VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# --- libdeflate -------------------------------------------------------------
# Same compression library the parent BetrockPlusPlus project uses for chunk/
# region (de)compression (see ../vcpkg.json and ../CMakeLists.txt). It isn't
# available as a dev package in every environment, so prefer an already
# installed copy (find_package) and fall back to fetching+building it from
# source (pinned tag, same upstream project: https://github.com/ebiggers/libdeflate).
find_package(libdeflate CONFIG QUIET)
if(NOT libdeflate_FOUND)
    include(FetchContent)
    set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
    set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libdeflate
        GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
        GIT_TAG        v1.19
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(libdeflate)
endif()

# --- Parent source tree ------------------------------------------------------
# This CLI does not reimplement Beta 1.7.3 world generation: it compiles the
# real generator/noise/biome/NBT source files straight out of the parent
# BetrockPlusPlus repository so output is guaranteed bit-identical to what the
# server itself would produce. See DECISIONS.md for the rationale.
set(BPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(BPP_SHARED ${BPP_ROOT}/src/bpp_shared)

set(BPP_SOURCES
    ${BPP_SHARED}/enums/biomes.cpp
    ${BPP_SHARED}/strings/ucs2.cpp
    ${BPP_SHARED}/strings/labels.cpp
    ${BPP_SHARED}/world/chunk.cpp
    ${BPP_SHARED}/world/world_wrapper.cpp
    ${BPP_SHARED}/world/world.cpp
    ${BPP_SHARED}/world/spawner.cpp
    ${BPP_SHARED}/world/lighter.cpp
    ${BPP_SHARED}/world/storage/region.cpp
    ${BPP_SHARED}/world/storage/region_manager.cpp
    ${BPP_SHARED}/world/tick_scheduler.cpp
    ${BPP_SHARED}/blocks/block_properties_behaviors.cpp
    ${BPP_SHARED}/blocks/block_behaviors.cpp
    ${BPP_SHARED}/blocks/block_properties.cpp
    ${BPP_SHARED}/items/item_map.cpp
    ${BPP_SHARED}/items/item_properties.cpp
    ${BPP_SHARED}/items/tool_properties.cpp
    # item_properties_interactions.cpp / tool_item_properties.cpp define player-interaction
    # item behaviors (EatFood/onUse/whileHeld/...) and need a complete PlayerSession, which
    # pulls in the entire server/networking stack. Nothing reachable from world generation
    # (including dungeon chest loot, which just constructs ItemStack{id, count} directly)
    # touches those behaviors, so they are intentionally excluded here. See DECISIONS.md.
    ${BPP_SHARED}/inventory/inventory_interaction.cpp
    ${BPP_SHARED}/inventory/interactions/chest.cpp
    ${BPP_SHARED}/inventory/interactions/large_chest.cpp
    ${BPP_SHARED}/inventory/interactions/crafting.cpp
    ${BPP_SHARED}/inventory/interactions/furnace.cpp
    ${BPP_SHARED}/inventory/interactions/player.cpp
    ${BPP_SHARED}/inventory/interactions/crafting_table.cpp
    ${BPP_SHARED}/crafting/recipe_manager.cpp
    ${BPP_SHARED}/crafting/vanilla_recipes.cpp
    ${BPP_SHARED}/pathfinding/pathfinder.cpp
    ${BPP_SHARED}/tile_entities/tile_entity_manager.cpp
    ${BPP_SHARED}/tile_entities/tile_entity.cpp
    ${BPP_SHARED}/entities/entity.cpp
    ${BPP_SHARED}/entities/entity_item.cpp
    ${BPP_SHARED}/entities/entity_player.cpp
    ${BPP_SHARED}/entities/entity_mobile.cpp
    ${BPP_SHARED}/entities/entity_manager.cpp
    ${BPP_SHARED}/entities/entity_pig.cpp
    ${BPP_SHARED}/entities/entity_cow.cpp
    ${BPP_SHARED}/entities/entity_sheep.cpp
    ${BPP_SHARED}/entities/entity_chicken.cpp
    ${BPP_SHARED}/entities/entity_falling_block.cpp
    ${BPP_SHARED}/entities/entity_animal.cpp
    ${BPP_SHARED}/entities/entity_mob.cpp
    ${BPP_SHARED}/entities/entity_zombie.cpp
    ${BPP_SHARED}/entities/entity_hostile.cpp
    ${BPP_SHARED}/entities/entity_skeleton.cpp
    ${BPP_SHARED}/entities/entity_creeper.cpp
    ${BPP_SHARED}/entities/entity_spider.cpp

    # Noise / world generation (the part we actually care about)
    ${BPP_SHARED}/world/noise/noise_generator.cpp
    ${BPP_SHARED}/world/noise/noise_perlin.cpp
    ${BPP_SHARED}/world/noise/noise_simplex.cpp
    ${BPP_SHARED}/world/generator/generator.cpp
    ${BPP_SHARED}/world/generator/overworld/chunk_gen.cpp
    ${BPP_SHARED}/world/generator/overworld/tree_gen.cpp
    ${BPP_SHARED}/world/generator/overworld/biome_gen.cpp
    ${BPP_SHARED}/world/generator/shared/cave_gen.cpp
    ${BPP_SHARED}/world/generator/shared/feature_gen.cpp
    ${BPP_SHARED}/world/generator/nether/chunk_gen.cpp

    ${BPP_SHARED}/logger/logger.cpp
    ${BPP_SHARED}/logger/logstream.cpp
    ${BPP_SHARED}/logger/style.cpp
    ${BPP_SHARED}/config/config.cpp

    ${BPP_ROOT}/src/bpp_utilities/utilities.cpp
    ${BPP_SHARED}/helpers/explosion.cpp
)

add_library(bpp_worldgen STATIC ${BPP_SOURCES})

target_include_directories(bpp_worldgen PUBLIC
    ${BPP_ROOT}/include
    ${BPP_ROOT}/src/bpp_shared
    ${BPP_ROOT}/src/bpp_utilities
    ${BPP_SHARED}/enums
    ${BPP_SHARED}/enums/network
    ${BPP_SHARED}/strings
    ${BPP_SHARED}/logger
    ${BPP_SHARED}/world
    ${BPP_SHARED}/world/noise
    ${BPP_SHARED}/helpers/java
    ${BPP_SHARED}/helpers
)

target_compile_definitions(bpp_worldgen PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(bpp_worldgen PUBLIC
    $<IF:$<TARGET_EXISTS:libdeflate::libdeflate_shared>,libdeflate::libdeflate_shared,$<IF:$<TARGET_EXISTS:libdeflate::libdeflate_static>,libdeflate::libdeflate_static,libdeflate_static>>
)

# --- chunkgen CLI ------------------------------------------------------------
add_executable(chunkgen
    src/main.cpp
    src/nbt_chunk_writer.cpp
)
target_link_libraries(chunkgen PRIVATE bpp_worldgen)
target_include_directories(chunkgen PRIVATE src)
