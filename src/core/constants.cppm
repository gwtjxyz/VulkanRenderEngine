module;

#include <cstdint>

#ifdef DISABLE_IMPORT_STD
#include <string>
#endif

export module constants;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

export constexpr uint32_t WIDTH = 1280;
export constexpr uint32_t HEIGHT = 720;
export constexpr uint32_t PARTICLE_COUNT = 8192;
export constexpr uint32_t MAZE_WIDTH = 20;
export constexpr uint32_t MAZE_HEIGHT = 40;

// OBJ

export const std::string VIKING_ROOM_MODEL_NAME = "viking_room.obj";
export const std::string VIKING_ROOM_TEXTURE_NAME = "viking_room.png";
export const std::string VIKING_ROOM_MATERIAL_NAME = "viking_room";
export const std::string VIKING_ROOM_ENTITY_NAME = "viking_room";

export const std::string VIKING_ROOM_2_ENTITY_NAME = "viking_room_2";

export const std::string TERRAIN_MODEL_NAME = "terrain.obj";
export const std::string TERRAIN_TEXTURE_NAME = "terrain_diffuse.png";
export const std::string TERRAIN_MATERIAL_NAME = "terrain";
export const std::string TERRAIN_ENTITY_NAME = "terrain";

export const std::string CUBE_MODEL_NAME = "cube.obj";

export const std::string WALL_CUBE_MODEL_NAME = "wall_cube.obj";
export const std::string FLOOR_MODEL_NAME = "floor.obj";
export const std::string FLOOR_START_MODEL_NAME = "floor_start.obj";
export const std::string FLOOR_FINISH_MODEL_NAME = "floor_finish.obj";

// GLTF

export const std::string CASTLE_MODEL_NAME = "scene.gltf";
export const std::string CASTLE_MATERIAL_NAME = "castle";
export const std::string CASTLE_ENTITY_NAME = "castle";

export const std::string LIGHT_ENTITY_NAME = "light_";

export const std::string GRAPHICS_PIPELINE_NAME = "graphics";
export const std::string POINT_GRAPHICS_PIPELINE_NAME = "point_graphics";
export const std::string COMPUTE_PIPELINE_NAME = "compute";
export const std::string MINIMAP_PIPELINE_NAME = "minimap";
