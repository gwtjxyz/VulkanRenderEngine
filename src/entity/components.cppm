module;

#include <cstdint>

export module components;

import render_types;
import scene_graph_types;

import glm;

// Draw + Transform + Mesh = draw model system
// LightSource + Transform + Light = draw light system

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
};

export struct CMesh {
    SceneGraphAsset * asset = nullptr;       // contains vertex/index buffer info + material info
    bool outOfDate = true;
};

export struct CLight {
    uint32_t transformIndex = INDEX_UNSET;
    glm::vec3 color {1.0f, 1.0f, 1.0f};
    float intensity {1.0f};
    uint32_t index = INDEX_UNSET;
    bool enabled = true;
    bool dirty = true;
};
