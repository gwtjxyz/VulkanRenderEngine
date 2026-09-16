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

// OBJ

export const std::string VIKING_ROOM_MODEL_NAME = "viking_room.obj";
export const std::string VIKING_ROOM_TEXTURE_NAME = "viking_room.png";
export const std::string VIKING_ROOM_MATERIAL_NAME = "viking_room";
export const std::string VIKING_ROOM_ENTITY_NAME = "viking_room";

export const std::string TERRAIN_MODEL_NAME = "terrain.obj";
export const std::string TERRAIN_TEXTURE_NAME = "terrain_diffuse.png";
export const std::string TERRAIN_MATERIAL_NAME = "terrain";
export const std::string TERRAIN_ENTITY_NAME = "terrain";

// GLTF

export const std::string CASTLE_MODEL_NAME = "scene.gltf";
export const std::string CASTLE_MATERIAL_NAME = "castle";
export const std::string CASTLE_ENTITY_NAME = "castle";

export const std::string LIGHT_ENTITY_NAME = "light1";

export const std::string GRAPHICS_PIPELINE_NAME = "graphics";
export const std::string POINT_GRAPHICS_PIPELINE_NAME = "point_graphics";
export const std::string COMPUTE_PIPELINE_NAME = "compute";
