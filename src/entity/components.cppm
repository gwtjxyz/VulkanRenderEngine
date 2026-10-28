module;

#include <cstdint>

export module components;

import core_types;
import render_types;
import scene_graph_types;

import glm;

// Draw + Transform + Mesh = draw model system
// Transform + Light = draw light system

export struct CDraw {
    uint32_t index = INDEX_UNSET;
    bool enabled = true;
};

export struct CTransform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    uint32_t index = INDEX_UNSET;
    bool dirty = true;

    void rotateX(const float angle) {
        rotation = glm::rotate(rotation, glm::radians(angle), glm::vec3(1.0f, 0.0f, 0.0f));
        dirty = true;
    }

    void rotateY(const float angle) {
        rotation = glm::rotate(rotation, glm::radians(angle), glm::vec3(0.0f, 1.0f, 0.0f));
        dirty = true;
    }

    void rotateZ(const float angle) {
        rotation = glm::rotate(rotation, glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f));
        dirty = true;
    }

    SceneGraphTypes::Transform toSceneTransform() const {
        return SceneGraphTypes::Transform {
            .translation = position,
            .rotation = rotation,
            .scale = scale
        };
    }
};

export struct CMesh {
    AssetInstance instance {};
    bool outOfDate = true;
};

// TODO finish
export enum class LightType : uint32_t {
    // Global light that has a direction from where the light is coming from
    // Position is ignored
    Directional = 0,
    // Punctual light that emits light uniformly in all directions
    Point,
    // Punctual light that emits light in a particular direction
    Spotlight
};

export struct CLight {
    uint32_t transformIndex = INDEX_UNSET;
    glm::vec3 color {1.0f, 1.0f, 1.0f};
    float intensity {1.0f};
    uint32_t index = INDEX_UNSET;
    LightType type = LightType::Point;
    bool enabled = true;
    bool dirty = true;

    // TODO remove?
    AssetInstance cubeDisplay {};
};
