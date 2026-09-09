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
        m_ObjectPrefab = m_World.prefab("Prefab_Object").set(CTransform {}).set(CMesh {});
        m_LightPrefab = m_World.prefab("Prefab_Light").set(CLight {}).set(CTransform {});

        prepareLightSystem();
        prepareObjectSystem();
        prepareRenderSystem();
    }

    // Creates all the entities that we will be drawing
    // TODO modularize
    void setupWorld(AssetManager & assetManager) {
        // Spinning room
        auto room = m_World.entity(VIKING_ROOM_ENTITY_NAME.c_str()).is_a(m_ObjectPrefab);
        auto & t1 = room.get_mut<CTransform>();
        t1.position = { 0.0f, -0.5f, -2.0f };
        t1.rotateX(-90.0f);

        auto & m1 = room.get_mut<CMesh>();
        // auto textureHandle = resourceManager.load<Texture>("assets/" + VIKING_ROOM_TEXTURE_NAME + ".png");
        // auto materialHandle = resourceManager.load<Material>(VIKING_ROOM_MATERIAL_NAME);
        // auto meshHandle = resourceManager.load<Asset3D>("assets/" + VIKING_ROOM_MODEL_NAME + ".obj");
        // materialHandle.get()->setTexture(textureHandle.get());
        auto * vikingRoomAssetHandle = assetManager.getOrCreateAsset(VIKING_ROOM_MODEL_NAME);
        vikingRoomAssetHandle->load();
        m1.asset = vikingRoomAssetHandle;

        auto terrain = m_World.entity(TERRAIN_ENTITY_NAME.c_str()).is_a(m_ObjectPrefab);
        auto & t2 = terrain.get_mut<CTransform>();
        t2.position = { -20.0f, -15.0f, 30.0f };
        t2.scale = glm::vec3(0.2f);

        auto & m2 = terrain.get_mut<CMesh>();
        // textureHandle = resourceManager.load<Texture>("assets/" + TERRAIN_TEXTURE_NAME + ".png");
        // materialHandle = resourceManager.load<Material>(TERRAIN_MATERIAL_NAME);
        // meshHandle = resourceManager.load<Asset3D>("assets/" + TERRAIN_MODEL_NAME + ".obj");
        // materialHandle.get()->setTexture(textureHandle.get());
        auto * terrainAssetHandle = assetManager.getOrCreateAsset(TERRAIN_MODEL_NAME);
        terrainAssetHandle->load();
        m2.asset = terrainAssetHandle;

        auto light = m_World.entity(LIGHT_ENTITY_NAME.c_str()).is_a(m_LightPrefab);
        auto & t3 = light.get_mut<CTransform>();
        t3.position = { 0.0f, -10.0f, 10.0f };

        // TODO Setup callbacks
    }

    void prepareAndRender(uint32_t frameIndex) {
        auto * deviceMapper = DeviceMapperLocator::locate();

        m_LightPrepareSystem.run();
        m_ObjectPrepareSystem.run();
        deviceMapper->uploadToGPU(frameIndex);

        m_RenderSystem.run();
    }

    void setPosition(const std::string & entityName, glm::vec3 pos) {
        auto entity = m_World.lookup(entityName.c_str());
        auto * t = entity.try_get_mut<CTransform>();
        if (t) {
            t->position = pos;
            t->dirty = true;
        }
    }

    void rotate(const std::string & entityName, float rotX, float rotY, float rotZ) {
        auto entity = m_World.lookup(entityName.c_str());
        auto * t = entity.try_get_mut<CTransform>();
        if (t) {
            t->rotateX(rotX);
            t->rotateY(rotY);
            t->rotateZ(rotZ);
            t->dirty = true;
        }
    }

private:
    void prepareLightSystem() {
        m_LightPrepareSystem = m_World.system<CLight, CTransform>().each(
            [this](flecs::iter & it, size_t, CLight & l, CTransform & t) {
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
        m_ObjectPrepareSystem = m_World.system<CTransform, CMesh>().each(
            [this](flecs::iter & it, size_t row, CTransform & t, CMesh & m) {
                if (!m.asset || !m.asset->isLoaded()) {
                    return;
                }

                if (t.dirty || m.outOfDate) {
                    m.asset->updateSceneTransform(
                        toSceneGraphTransform(t),
                        TransformUpdateFlags::Translation | TransformUpdateFlags::Scale | TransformUpdateFlags::Rotation,
                        true
                    );
                    t.dirty = false;
                    m.outOfDate = false;
                }
            }
        );
    }

    void prepareRenderSystem() {
        m_RenderSystem = m_World.system<CTransform, CMesh>().each(
            [](flecs::iter & it, size_t row, CTransform & t, CMesh & m) {
                if (!m.asset || !m.asset->isLoaded()) {
                    return;
                }

                auto drawCallData = m.asset->prepareDrawData();
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

    static void mapTransform(CTransform & t, DeviceMapper * deviceMapper) {
        auto * transformPtr = deviceMapper->getTransform(t.index);
        transformPtr->layout.position = glm::vec4(t.position, 1.0f);
        transformPtr->layout.rotation = glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
        transformPtr->layout.scale = glm::vec4(t.scale, 0.0f);
    }

    static SceneGraphTypes::Transform toSceneGraphTransform(const CTransform & transform) {
        return { transform.position, transform.rotation, transform.scale };
    }

private:
    flecs::world m_World {};

    flecs::entity m_ObjectPrefab;
    flecs::entity m_LightPrefab;

    flecs::system m_ObjectPrepareSystem;
    flecs::system m_LightPrepareSystem;
    flecs::system m_RenderSystem;
};
