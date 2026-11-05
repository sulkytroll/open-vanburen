cmake_minimum_required(VERSION 3.16)
project(vanburen CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Shared asset-parsing library (GRP + RHT + G3D + TGA + format enums)
# ---------------------------------------------------------------------------
add_library(vb_assets
  src/assets/rht.cpp
  src/assets/grp.cpp
  src/assets/g3d.cpp
  src/assets/tga.cpp
  src/assets/tre.cpp
  src/assets/col.cpp
  src/assets/map.cpp
  src/assets/entity.cpp
  src/assets/oodle0.cpp
  src/assets/gr2.cpp
  src/assets/anim.cpp
  src/assets/amx.cpp
  src/assets/stf.cpp
  src/assets/gui.cpp
  src/assets/vfx.cpp
  src/assets/sound.cpp
  src/assets/vfs.cpp
)
target_include_directories(vb_assets PUBLIC src)

# game world runtime (scripts, entities, dialogue; no graphics)
add_library(vb_game_core src/game/world.cpp)
target_link_libraries(vb_game_core PUBLIC vb_assets)
target_include_directories(vb_game_core PUBLIC src)

# graphics deps
find_package(SDL2 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)
find_package(OpenAL REQUIRED)

# ---------------------------------------------------------------------------
# Tool: asset extractor  ->  vb_extract
# ---------------------------------------------------------------------------
add_executable(vb_extract tools/vb_extract/main.cpp)
target_link_libraries(vb_extract vb_assets)

# ---------------------------------------------------------------------------
# Tool: G3D validator  ->  vb_g3d_validate
# ---------------------------------------------------------------------------
add_executable(vb_g3d_validate tools/vb_g3d_validate/main.cpp)
target_link_libraries(vb_g3d_validate vb_assets)

# ---------------------------------------------------------------------------
# Tool: native renderer  ->  vb_render
# ---------------------------------------------------------------------------
add_executable(vb_render tools/vb_render/main.cpp)
target_link_libraries(vb_render vb_assets SDL2::SDL2 GLEW::GLEW OpenGL::GL)

# ---------------------------------------------------------------------------
# Tool: map/octree/collision validator  ->  vb_map_validate
# ---------------------------------------------------------------------------
add_executable(vb_map_validate tools/vb_map_validate/main.cpp)
target_link_libraries(vb_map_validate vb_assets)
target_include_directories(vb_map_validate PRIVATE tools)

# ---------------------------------------------------------------------------
# Tool: level viewer  ->  vb_map
# ---------------------------------------------------------------------------
add_executable(vb_map tools/vb_map/main.cpp)
target_link_libraries(vb_map vb_assets SDL2::SDL2 GLEW::GLEW OpenGL::GL)

# ---------------------------------------------------------------------------
# Tool: G3D model viewer  ->  vb_model
# ---------------------------------------------------------------------------
add_executable(vb_model tools/vb_model/main.cpp)
target_link_libraries(vb_model vb_assets SDL2::SDL2 GLEW::GLEW OpenGL::GL)
target_include_directories(vb_map PRIVATE tools)
target_include_directories(vb_model PRIVATE tools)

# ---------------------------------------------------------------------------
# Tool: Granny .skel/.anim validator  ->  vb_gr2_validate
# ---------------------------------------------------------------------------
add_executable(vb_gr2_validate tools/vb_gr2_validate/main.cpp)
target_link_libraries(vb_gr2_validate vb_assets)

# ---------------------------------------------------------------------------
# Game: the start of the engine (forked from the map viewer)  ->  vanburen
# ---------------------------------------------------------------------------
add_executable(vanburen tools/vanburen/main.cpp tools/vanburen/embedded.cpp)
target_compile_definitions(vanburen PRIVATE VB_DATA_DIR="${CMAKE_SOURCE_DIR}/data")      # the port's own data (menu button picture)
target_link_libraries(vanburen vb_game_core vb_assets SDL2::SDL2 GLEW::GLEW OpenGL::GL Freetype::Freetype OpenAL::OpenAL)
target_include_directories(vanburen PRIVATE tools)

# ---------------------------------------------------------------------------
# Tool: VFS check against an extracted copy  ->  vb_vfs_test
# ---------------------------------------------------------------------------
add_executable(vb_vfs_test tools/vb_vfs_test/main.cpp)
target_link_libraries(vb_vfs_test vb_assets)

# ---------------------------------------------------------------------------
# Tool: AMX script VM smoke test  ->  vb_amx_test
# ---------------------------------------------------------------------------
add_executable(vb_amx_test tools/vb_amx_test/main.cpp)
target_link_libraries(vb_amx_test vb_assets)

# ---------------------------------------------------------------------------
# Tool: headless world/script simulation  ->  vb_world_sim
# ---------------------------------------------------------------------------
add_executable(vb_world_sim tools/vb_world_sim/main.cpp)
target_link_libraries(vb_world_sim vb_game_core)
target_include_directories(vb_world_sim PRIVATE tools)

# ---------------------------------------------------------------------------
# Tool: interface layout (.gui) parser check / dumper  ->  vb_gui_dump
# ---------------------------------------------------------------------------
add_executable(vb_gui_dump tools/vb_gui_dump/main.cpp)
target_link_libraries(vb_gui_dump vb_assets)
target_include_directories(vb_gui_dump PRIVATE tools)
