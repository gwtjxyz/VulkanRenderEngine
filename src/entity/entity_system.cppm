module;

#include <flecs.h>

#ifdef DISABLE_IMPORT_STD
#include <iostream>
#include <string>
#endif

export module entity_system;

import asset_manager;
import bit_flags;
import components;
import constants;
import core_types;
import device_mapper;
import pipeline_manager;
import render_types;
import scene_graph_types;

#ifndef DISABLE_IMPORT_STD
import std;
#endif
import glm;

export class EntitySystem {
public:
    EntitySystem() {
        m_ObjectPrefab = m_World.prefab("Prefab_Object").set(Components::Transform {}).set(Components::Mesh {});
        m_LightPrefab = m_World.prefab("Prefab_Light").set(Components::Light {}).set(Components::Transform {});

        prepareLightSystem();
        prepareObjectSystem();
        prepareRenderSystem();
        prepareLightCubeSystem();
    }

    // Creates all the entities that we will be drawing
    // TODO modularize
    void setupWorld(AssetManager & assetManager) {
        // Spinning room
        createObject(
            assetManager,
            VIKING_ROOM_ENTITY_NAME,
            VIKING_ROOM_MODEL_NAME,
            { 0.0f, -0.5f, -2.0f },
            { -90.0f, 0.0f, 0.0f }
        );

        // Non-spinning room (asset instancing test)
        createObject(
            assetManager,
            VIKING_ROOM_2_ENTITY_NAME,
            VIKING_ROOM_MODEL_NAME,
            { 3.5f, 0.5f, 0.5f },
            { -90.0f, 160.0f, 50.0f }
        );

        // Terrain
        createObject(
            assetManager,
            TERRAIN_ENTITY_NAME,
            TERRAIN_MODEL_NAME,
            { -20.0f, -15.0f, 30.0f },
            {},
            0.1f
        );

        // Castle on hills
        createObject(
            assetManager,
            CASTLE_ENTITY_NAME,
            CASTLE_MODEL_NAME,
            { -6.0f, -11.5f, -13.0f }
        );

        createLight(assetManager, LIGHT_ENTITY_NAME, { 0.0f, -10.0f, 10.0f });
    }

    void createObject(
        AssetManager & assetManager,
        const std::string & name,
        const std::string & modelName,
        const glm::vec3 & position = { 0.0f, 0.0f, 0.0f },
        const glm::vec3 & rotation = { 0.0f, 0.0f, 0.0f },
        const float scale = 1.0f
    ) {
        const auto entity = m_World.entity(name.c_str()).is_a(m_ObjectPrefab);

        auto & transform = entity.get_mut<Components::Transform>();
        transform.position = position;
        transform.rotation = glm::quat(glm::radians(rotation));
        transform.scale = { scale, scale, scale };

        auto & mesh = entity.get_mut<Components::Mesh>();
        auto * assetHandle = assetManager.getOrCreateAsset(modelName);
        assert(assetHandle);
        assetHandle->load();

        mesh.instance = AssetInstance(assetHandle);
        mesh.instance.load();

        m_WorldObjects.emplace_back(name.c_str());
    }

    void createLight(
        AssetManager & assetManager,
        const std::string & name,
        const glm::vec3 & position = { 0.0f, 0.0f, 0.0f },
        const glm::vec3 & rotation = { 0.0f, 0.0f, 0.0f }
    ) {
        const auto entity = m_World.entity(name.c_str()).is_a(m_LightPrefab);

        auto & transform = entity.get_mut<Components::Transform>();
        transform.position = position;
        transform.rotation = rotation;

        auto * cubeAssetHandle = assetManager.getOrCreateAsset(CUBE_MODEL_NAME);
        assert(cubeAssetHandle);

        // Load in and scale down the cube if it isn't loaded yet
        if (!cubeAssetHandle->isLoaded()) {
            cubeAssetHandle->load();
            auto cubeTransform = SceneGraphTypes::Transform {
                {},
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.1f, 0.1f, 0.1f }
            };
            cubeAssetHandle->updateSceneTransform(cubeTransform, TransformUpdateFlags::Scale, true);
        }

        auto & light = entity.get_mut<Components::Light>();
        light.cubeDisplay = AssetInstance(cubeAssetHandle);
        light.cubeDisplay.load();

        m_LightObjects.emplace_back(name.c_str());
    }

    void prepareAndRender(const uint32_t frameIndex, const bool drawLightCubes) const {
        auto * deviceMapper = DeviceMapperLocator::locate();

        m_LightPrepareSystem.run();
        m_ObjectPrepareSystem.run();
        if (drawLightCubes) {
            m_LightCubeSystem.run();
        }
        deviceMapper->uploadToGPU(frameIndex);

        m_RenderSystem.run();
    }

    void setPosition(const std::string & entityName, const glm::vec3 pos) const {
        const auto entity = m_World.lookup(entityName.c_str());
        if (auto * t = entity.try_get_mut<Components::Transform>()) {
            t->position = pos;
            t->dirty = true;
        }
    }

    void setPosition(const char * entityName, const glm::vec3 newPos) const {
        const auto entity = m_World.lookup(entityName);
        if (auto * t = entity.try_get_mut<Components::Transform>()) {
            t->position = newPos;
            t->dirty = true;
        }
    }

    void setScale(const char * entityName, const float newScale) const {
        const auto entity = m_World.lookup(entityName);
        if (auto * t = entity.try_get_mut<Components::Transform>()) {
            t->scale = { newScale, newScale, newScale };
            t->dirty = true;
        }
    }

    void setRotation(const std::string & entityName, const float rotX, const float rotY, const float rotZ) const {
        const auto entity = m_World.lookup(entityName.c_str());
        if (auto * t = entity.try_get_mut<Components::Transform>()) {
            t->rotate(rotX, rotY, rotZ);
            t->dirty = true;
        }
    }

    void addRotation(const std::string & entityName, const float rotX, const float rotY, const float rotZ) const {
        const auto entity = m_World.lookup(entityName.c_str());
        if (auto * t = entity.try_get_mut<Components::Transform>()) {
            t->rotateX(rotX);
            t->rotateY(rotY);
            t->rotateZ(rotZ);
            t->dirty = true;
        }
    }

    const std::vector<const char *> & getObjectNames() const {
        return m_WorldObjects;
    }

    const std::vector<const char *> & getLightNames() const {
        return m_LightObjects;
    }

    const Components::Transform * getTransform(const char * objectName) const {
        const auto entity = m_World.lookup(objectName);
        return entity.try_get<Components::Transform>();
    }

    const Components::Light * getLight(const char * objectName) const {
        const auto entity = m_World.lookup(objectName);
        return entity.try_get<Components::Light>();
    }

private:
    void prepareLightSystem() {
        m_LightPrepareSystem = m_World.system<Components::Light, Components::Transform>().each(
            [this](flecs::iter & it, size_t, Components::Light & l, Components::Transform & t) {
                auto * deviceMapper = DeviceMapperLocator::locate();
                // set up indices if they were unset before
                if (l.index == INDEX_UNSET) {
                    l.index = deviceMapper->addLight();
                }
                if (t.index == INDEX_UNSET) {
                    t.index = deviceMapper->addTransform();
                }

                uint32_t lightIndex = l.index;
                uint32_t transformIndex = t.index;

                auto * lightPtr = deviceMapper->getLight(lightIndex);
                lightPtr->layout.enabled = l.enabled;

                if (t.dirty) {
                    mapTransform(t, deviceMapper);
                }
                lightPtr->layout.transformIndex = transformIndex;

                if (l.dirty) {
                    lightPtr->layout.color = l.color;
                    lightPtr->layout.intensity = l.intensity;

                    l.dirty = false;
                }
            }
        );
    }

    void prepareObjectSystem() {
        m_ObjectPrepareSystem = m_World.system<Components::Transform, Components::Mesh>().each(
            [this](flecs::iter & it, size_t row, Components::Transform & t, Components::Mesh & m) {
                if (!m.instance.getAsset() || !m.instance.isLoaded()) {
                    return;
                }

                if (t.dirty || m.outOfDate) {
                    m.instance.update(t.toSceneTransform());
                    t.dirty = false;
                    m.outOfDate = false;
                }
            }
        );
    }

    void prepareRenderSystem() {
        m_RenderSystem = m_World.system<Components::Transform, Components::Mesh>().each(
            [this](flecs::iter & it, size_t row, Components::Transform & t, Components::Mesh & m) {
                if (!m.instance.getAsset() || !m.instance.isLoaded()) {
                    return;
                }

                auto drawCallData = m.instance.prepareDrawData();
                if (!drawCallData.empty()) {
                    auto * pipelineManager = PipelineManagerLocator::locate();
                    for (auto & drawCall : drawCallData) {
                        // TODO draw call sorting, culling, etc
                        pipelineManager->executeDrawCall(drawCall);
                    }
                }
            }
        );
    }

    // TODO change - very hacky, this should ideally use instanced rendering and probably not be a separate system
    void prepareLightCubeSystem() {
        m_LightCubeSystem = m_World.system<Components::Transform, Components::Light>().each(
            [this](flecs::iter & it, size_t row, Components::Transform & t, Components::Light & l) {
                if (!l.cubeDisplay.isLoaded()) {
                    return;
                }

                if (t.dirty) {
                    l.cubeDisplay.update(t.toSceneTransform());
                    t.dirty = false;
                }

                auto drawCallData = l.cubeDisplay.prepareDrawData();
                if (!drawCallData.empty()) {
                    auto * pipelineManager = PipelineManagerLocator::locate();
                    for (auto & drawCall : drawCallData) {
                        pipelineManager->executeDrawCall(drawCall);
                    }
                }
            }
        );
    }

    static void mapTransform(Components::Transform & t, DeviceMapper * deviceMapper) {
        auto * transformPtr = deviceMapper->getTransform(t.index);
        transformPtr->layout.position = glm::vec4(t.position, 1.0f);
        transformPtr->layout.rotation = glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
        transformPtr->layout.scale = glm::vec4(t.scale, 0.0f);
    }

    static SceneGraphTypes::Transform toSceneGraphTransform(const Components::Transform & transform) {
        return { transform.position, transform.rotation, transform.scale };
    }

private:
    flecs::world m_World {};

    flecs::entity m_ObjectPrefab;
    flecs::entity m_LightPrefab;

    flecs::system m_ObjectPrepareSystem;
    flecs::system m_LightPrepareSystem;
    flecs::system m_RenderSystem;

    // TODO change - temporary solution, not very clean
    flecs::system m_LightCubeSystem;

    std::vector<const char *> m_WorldObjects {};
    std::vector<const char *> m_LightObjects {};
};
