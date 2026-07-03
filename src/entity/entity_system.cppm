module;

#include <flecs.h>

#ifdef DISABLE_IMPORT_STD
#include <iostream>
#include <string>
#endif

export module entity_system;

import components;
import constants;
import device_mapper;
import pipeline_manager;
import render_types;
import resource;

#ifndef DISABLE_IMPORT_STD
import std;
#endif
import glm;

export class EntitySystem {
public:
    EntitySystem() {
        m_ObjectPrefab = m_World.prefab("Prefab_Object").set(CDraw {}).set(CTransform {}).set(CMesh {});
        m_LightPrefab = m_World.prefab("Prefab_Light").set(CLight {}).set(CTransform {});

        prepareLightSystem();
        prepareObjectSystem();
        prepareRenderSystem();
    }

    // Creates all the entities that we will be drawing
    // TODO modularize
    void setupWorld(ResourceManager & resourceManager) {
        // Spinning room
        auto room = m_World.entity(VIKING_ROOM_ENTITY_NAME.c_str()).is_a(m_ObjectPrefab);
        auto & t1 = room.get_mut<CTransform>();
        t1.position = { 0.0f, -0.5f, -2.0f };
        t1.rotateX(-90.0f);

        auto & m1 = room.get_mut<CMesh>();
        auto textureHandle = resourceManager.load<Texture>("assets/" + VIKING_ROOM_TEXTURE_NAME + ".png");
        auto materialHandle = resourceManager.load<Material>(VIKING_ROOM_MATERIAL_NAME);
        auto meshHandle = resourceManager.load<Asset3D>("assets/" + VIKING_ROOM_MODEL_NAME + ".obj");
        materialHandle.get()->setTexture(textureHandle.get());
        m1.material = materialHandle.get();
        m1.mesh = meshHandle.get();

        auto terrain = m_World.entity(TERRAIN_ENTITY_NAME.c_str()).is_a(m_ObjectPrefab);
        auto & t2 = terrain.get_mut<CTransform>();
        t2.position = { -20.0f, -15.0f, 30.0f };
        t2.scale = glm::vec3(0.2f);

        auto & m2 = terrain.get_mut<CMesh>();
        textureHandle = resourceManager.load<Texture>("assets/" + TERRAIN_TEXTURE_NAME + ".png");
        materialHandle = resourceManager.load<Material>(TERRAIN_MATERIAL_NAME);
        meshHandle = resourceManager.load<Asset3D>("assets/" + TERRAIN_MODEL_NAME + ".obj");
        materialHandle.get()->setTexture(textureHandle.get());
        m2.material = materialHandle.get();
        m2.mesh = meshHandle.get();

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
        m_ObjectPrepareSystem = m_World.system<CDraw, CTransform, CMesh>().each(
            [this](flecs::iter & it, size_t row, CDraw & d, CTransform & t, CMesh & m) {
                auto * deviceMapper = DeviceMapperLocator::locate();
                // Set up indices if they were unset before
                if (d.index == INDEX_UNSET) {
                    d.index = deviceMapper->addDraw();
                }
                if (t.index == INDEX_UNSET) {
                    t.index = deviceMapper->addTransform();
                }
                // Don't need to do it for mesh component - material system handles it automatically

                uint32_t drawIndex = d.index;
                uint32_t transformIndex = t.index;
                uint32_t materialIndex = m.material->getIndex();

                auto * drawPtr = deviceMapper->getDraw(drawIndex);
                drawPtr->layout.enabled = d.enabled;

                if (t.dirty) {
                    mapTransform(t, deviceMapper);
                    t.dirty = false;
                }
                // Pretty sure we need to do this every time, because what if the same transform is used for
                // both light source and object? One of them will update first, then the other will be not dirty.
                drawPtr->layout.transformIndex = transformIndex;

                // If material is out of date, update draw buffer with it
                if (m.material->update() || m.outOfDate) {
                    drawPtr->layout.materialIndex = materialIndex;
                    m.outOfDate = false;
                }
            }
        );
    }

    void prepareRenderSystem() {
        m_RenderSystem = m_World.system<CDraw, CTransform, CMesh>().each(
            [](flecs::iter & it, size_t row, CDraw & d, CTransform & t, CMesh & m) {
                auto * pipelineManager = PipelineManagerLocator::locate();
                // Don't draw if data is incomplete
                bool drawOffOrIncomplete = d.enabled == false || d.index == INDEX_UNSET;
                bool transformIncomplete = t.index == INDEX_UNSET;
                bool meshIncomplete = m.material->getIndex() == INDEX_UNSET;

                if (drawOffOrIncomplete || transformIncomplete || meshIncomplete) return;

                pipelineManager->bindAndDrawIndexed(
                    d.index,
                    m.mesh->getVertexBuffer(),
                    m.mesh->getIndexBuffer(),
                    m.mesh->getIndexCount(),
                    1
                );
            }
        );
    }

    static void mapTransform(CTransform & t, DeviceMapper * deviceMapper) {
        auto * transformPtr = deviceMapper->getTransform(t.index);
        transformPtr->layout.position = glm::vec4(t.position, 1.0f);
        transformPtr->layout.rotation = glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
        transformPtr->layout.scale = glm::vec4(t.scale, 0.0f);
    }

private:
    flecs::world m_World {};

    flecs::entity m_ObjectPrefab;
    flecs::entity m_LightPrefab;

    flecs::system m_ObjectPrepareSystem;
    flecs::system m_LightPrepareSystem;
    flecs::system m_RenderSystem;
};
