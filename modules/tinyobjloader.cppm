module;

// tiny_obj_loader.h includes C++ standard library headers which conflicts with using std module,
// therefore we are wrapping the library in a module so we can use it instead and avoid these conflicts

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

export module tinyobjloader;

export namespace tinyobj {
    // Typedefs
    using tinyobj::real_t;
    using tinyobj::texture_type_t;
    // Structs
    using tinyobj::texture_option_t;
    using tinyobj::material_t;
    using tinyobj::tag_t;
    using tinyobj::joint_and_weight_t;
    using tinyobj::skin_weight_t;
    using tinyobj::index_t;
    using tinyobj::mesh_t;
    using tinyobj::lines_t;
    using tinyobj::points_t;
    using tinyobj::shape_t;
    using tinyobj::attrib_t;
    using tinyobj::callback_t;
    // Classes
    using tinyobj::MaterialReader;
    using tinyobj::MaterialFileReader;
    using tinyobj::MaterialStreamReader;
    using tinyobj::ObjReaderConfig;
    using tinyobj::ObjReader;
    // Functions
    using tinyobj::LoadObj;
    using tinyobj::LoadObjWithCallback;
    using tinyobj::LoadMtl;
    using tinyobj::ParseTextureNameAndOption;
}