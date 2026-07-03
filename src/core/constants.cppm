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

export const std::string VIKING_ROOM_MODEL_NAME = "viking_room";
export const std::string VIKING_ROOM_TEXTURE_NAME = "viking_room";
export const std::string VIKING_ROOM_MATERIAL_NAME = "viking_room";
export const std::string VIKING_ROOM_ENTITY_NAME = "viking_room";

export const std::string TERRAIN_MODEL_NAME = "terrain";
export const std::string TERRAIN_TEXTURE_NAME = "terrain_diffuse";
export const std::string TERRAIN_MATERIAL_NAME = "terrain";
export const std::string TERRAIN_ENTITY_NAME = "terrain";

export const std::string LIGHT_ENTITY_NAME = "light1";

export const std::string GRAPHICS_PIPELINE_NAME = "graphics";
export const std::string POINT_GRAPHICS_PIPELINE_NAME = "point_graphics";
export const std::string COMPUTE_PIPELINE_NAME = "compute";
