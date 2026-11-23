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
import draw_executor;
import maze_builder;
import render_types;
import scene_graph_types;

#ifndef DISABLE_IMPORT_STD
import std;
#endif
import glm;

// TODO think about making a locator for this
export class EntitySystem {
public:
    EntitySystem() {
        m_ObjectPrefab = m_World.prefab("Prefab_Object").set(Components::Transform {}).set(Components::Mesh {});
        m_LightPrefab = m_World.prefab("Prefab_Light").set(Components::Light {}).set(Components::Transform {});

        m_ObjectsQuery = m_World.query_builder<const Components::Transform, const Components::Mesh>()
            .cached()
            .build();

        prepareLightSystem();
        prepareObjectSystem();
        prepareRenderSystem();
        prepareLightCubeSystem();
    }

    // Creates all the entities that we will be drawing
    void setupWorld(AssetManager & assetManager) {
        uint32_t innerMazeWidth = 10;
        uint32_t innerMazeHeight = 15;

        m_MazeBuilder.setDimensions(innerMazeWidth, innerMazeHeight);
        m_MazeBuilder.generateMaze();
        m_MazeBuilder.printMazeToFile();

        auto walledMaze = m_MazeBuilder.toWalledMaze();
        uint32_t totalMazeWidth = innerMazeWidth + 2;
        uint32_t totalMazeHeight = innerMazeHeight + 2;

        uint32_t currentMazeX = 0;
        uint32_t currentMazeY = 0;

        const std::string tilePrefix = "tile_";

        uint32_t cellIndex = 0;
        for (const auto & c : walledMaze.grid) {
            glm::vec3 position = { 2.0f * currentMazeX, -1.0f, 2.0f * currentMazeY };
            glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
            constexpr float scale = 1.0f;

            if (cellIndex == walledMaze.entrance) {
                m_StartPosition = { position.x, position.y + 1.0f, position.z };

                glm::vec3 entranceWallPosition = position;
                entranceWallPosition.x -= 2.0f;
                entranceWallPosition.y += 1.0f;
                createObject(
                    assetManager,
                    tilePrefix + "entranceWall",
                    WALL_CUBE_MODEL_NAME,
                    entranceWallPosition,
                    rotation,
                    scale
                );
            } else if (cellIndex == walledMaze.exit) {
                m_ExitPosition = position;

                glm::vec3 exitWallPosition = position;
                exitWallPosition.x += 2.0f;
                exitWallPosition.y += 1.0f;
                createObject(
                    assetManager,
                    tilePrefix + "exitWall",
                    WALL_CUBE_MODEL_NAME,
                    exitWallPosition,
                    rotation,
                    scale
                );
            }

            if (c.type == MazeCellType::Wall) {
                position.y += 1.0f;
            }

            std::string tileName = tilePrefix + std::to_string(currentMazeX + currentMazeY * totalMazeWidth);

            std::string modelName;
            if (c.type == MazeCellType::Wall) {
                modelName = WALL_CUBE_MODEL_NAME;
            } else if (cellIndex == walledMaze.entrance) {
                modelName = FLOOR_START_MODEL_NAME;
            } else if (cellIndex == walledMaze.exit) {
                modelName = FLOOR_FINISH_MODEL_NAME;
            } else {
                modelName = FLOOR_MODEL_NAME;
            }

            createObject(
                assetManager,
                tileName,
                modelName,
                position,
                rotation,
                scale
            );

            if (c.type == MazeCellType::Corridor) {
                tileName += "_ceiling";
                position.y += 2.0f;
                rotation.x += 180.0f;

                createObject(
                    assetManager,
                    tileName,
                    modelName,
                    position,
                    rotation,
                    scale
                );
            }

            currentMazeX++;
            if (currentMazeX == totalMazeWidth) {
                currentMazeY++;
                currentMazeX = 0;
            }

            cellIndex++;
        }

        glm::vec3 mazeCenter = { 2.0f * static_cast<float>(totalMazeWidth) / 2, 6.0f, 2.0f * static_cast<float>(totalMazeHeight) / 2 };
        createLight(assetManager, mazeCenter);

        createMinimap(walledMaze, totalMazeWidth, totalMazeHeight);
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

        m_WorldObjects.emplace_back(std::move(name));
    }

    void createLight(
        AssetManager & assetManager,
        const glm::vec3 & position = { 0.0f, 0.0f, 0.0f },
        const glm::vec3 & rotation = { 0.0f, 0.0f, 0.0f }
    ) {
        // Give the light a unique name
        const std::string name = LIGHT_ENTITY_NAME + std::to_string(m_CreatedLights);
        m_CreatedLights++;

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

        m_LightObjects.emplace_back(std::move(name));
    }

    void removeLight(AssetManager & assetManager, int lightIndex) {
        assert(lightIndex >= 0);
        auto & name = m_LightObjects.at(lightIndex);
        auto entity = m_World.lookup(name.c_str());
        if (entity != 0 && entity.is_a(m_LightPrefab)) {
            auto * deviceMapper = DeviceMapperLocator::locate();

            auto & l = entity.get_mut<Components::Light>();
            deviceMapper->removeLight(l.index);
            l.cubeDisplay.unload();
            auto & t = entity.get_mut<Components::Transform>();
            deviceMapper->removeTransform(t.index);

            entity.destruct();
        }

        std::vector<std::string> newLightObjectsList;
        size_t i = 0;
        for (const auto & it : m_LightObjects) {
            if (i != lightIndex) {
                newLightObjectsList.emplace_back(it);
            }
            ++i;
        }
        m_LightObjects = newLightObjectsList;
    }

    void prepareAndRender(const uint32_t frameIndex, const bool drawLightCubes) const {
        auto * deviceMapper = DeviceMapperLocator::locate();
        auto * drawExecutor = DrawExecutorLocator::locate();

        m_LightPrepareSystem.run();
        m_ObjectPrepareSystem.run();
        if (drawLightCubes) {
            m_LightCubeSystem.run();
        }

        drawExecutor->prepareForNextFrame();

        m_RenderPrepareSystem.run();

        drawExecutor->uploadBatchesToGPU();
        deviceMapper->prepareCopyCommands();
        deviceMapper->uploadToGPU(frameIndex);
        drawExecutor->executeDrawIndirect(frameIndex);
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

    void setColor(const std::string & entityName, const float colors[3]) const {
        const auto entity = m_World.lookup(entityName.c_str());
        if (auto * l = entity.try_get_mut<Components::Light>()) {
            l->color.r = colors[0];
            l->color.g = colors[1];
            l->color.b = colors[2];
            l->dirty = true;
        }
    }

    void setIntensity(const std::string & entityName, const float intensity) const {
        const auto entity = m_World.lookup(entityName.c_str());
        if (auto * l = entity.try_get_mut<Components::Light>()) {
            l->intensity = intensity;
            l->dirty = true;
        }
    }

    void setLightType(const std::string & entityName, const Components::LightType type) const {
        const auto entity = m_World.lookup(entityName.c_str());
        if (auto * l = entity.try_get_mut<Components::Light>()) {
            l->type = type;
            l->dirty = true;
        }
    }

    const std::vector<std::string> & getObjectNames() const {
        return m_WorldObjects;
    }

    const std::vector<std::string> & getLightNames() const {
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

    glm::vec3 getStartPosition() const {
        return m_StartPosition;
    }

    glm::vec3 getExitPosition() const {
        return m_ExitPosition;
    }

    const flecs::query<const Components::Transform, const Components::Mesh> & getObjectsQuery() const {
        return m_ObjectsQuery;
    }

    const std::vector<Particle> & getMinimapDisplay() const {
        return m_MinimapDisplay;
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

                if (t.dirty) {
                    mapTransform(t, deviceMapper);

                    auto & lightDeviceData = deviceMapper->getLight(lightIndex);
                    lightDeviceData.transformIndex = transformIndex;

                    t.dirty = false;
                }

                if (l.dirty) {
                    auto & lightDeviceData = deviceMapper->getLight(lightIndex);

                    lightDeviceData.enabled = l.enabled;
                    lightDeviceData.color = l.color;
                    lightDeviceData.intensity = l.intensity;

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
        m_RenderPrepareSystem = m_World.system<Components::Transform, Components::Mesh>().each(
            [this](flecs::iter & it, size_t row, Components::Transform & t, Components::Mesh & m) {
                if (!m.instance.getAsset() || !m.instance.isLoaded()) {
                    return;
                }

                auto drawCallData = m.instance.prepareDrawData();
                if (!drawCallData.empty()) {
                    auto * drawExecutor = DrawExecutorLocator::locate();
                    for (auto & drawCall : drawCallData) {
                        drawExecutor->batchDrawCall(drawCall);
                    }
                }
            }
        );
    }

    // FIXME drawing light cubes broke for some reason
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
                    auto * drawExecutor = DrawExecutorLocator::locate();
                    for (auto & drawCall : drawCallData) {
                        drawExecutor->batchDrawCall(drawCall);
                    }
                }
            }
        );
    }

    static void mapTransform(Components::Transform & t, DeviceMapper * deviceMapper) {
        auto & transformDeviceData = deviceMapper->getTransform(t.index);
        transformDeviceData.position = glm::vec4(t.position, 1.0f);
        transformDeviceData.rotation = glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
        transformDeviceData.scale = glm::vec4(t.scale, 0.0f);
    }

    void createMinimap(const WalledMaze & walledMaze, const uint32_t totalMazeWidth, const uint32_t totalMazeHeight) {
        constexpr float ndcStartX = 0.0f;
        constexpr float ndcStartY = -0.6f;

        constexpr float mapCellSize = 0.02f;

        uint32_t currentMazeX = 0;
        uint32_t currentMazeY = 0;
        uint32_t index = 0;
        for (const auto c : walledMaze.grid) {
            Particle p {};

            if (index == walledMaze.entrance) {
                p.color = {0.0f, 1.0f, 0.0f, 1.0f};
            } else if (index == walledMaze.exit) {
                p.color = {1.0f, 0.0f, 0.0f, 1.0f};
            } else if (c.type == MazeCellType::Corridor) {
                p.color = {0.1f, 0.6f, 0.1f, 1.0f};
            } else {
                p.color = {0.2f, 0.2f, 0.2f, 1.0f};
            }

            p.position.x = ndcStartX + mapCellSize * currentMazeX;
            p.position.y = ndcStartY + mapCellSize * currentMazeY;
            p.position.z = 1.0f;
            p.position.w = 1.0f;

            m_MinimapDisplay.emplace_back(p);

            currentMazeX++;
            if (currentMazeX == totalMazeWidth) {
                currentMazeY++;
                currentMazeX = 0;
            }

            index++;
        }
    }

private:
    flecs::world m_World {};

    flecs::query<const Components::Transform, const Components::Mesh> m_ObjectsQuery;

    MazeBuilder m_MazeBuilder {};

    flecs::entity m_ObjectPrefab;
    flecs::entity m_LightPrefab;

    flecs::system m_ObjectPrepareSystem;
    flecs::system m_LightPrepareSystem;
    flecs::system m_RenderPrepareSystem;

    // TODO change - temporary solution, not very clean
    flecs::system m_LightCubeSystem;

    // TODO change to vector of strings so we can add arbitrary objects at runtime
    std::vector<std::string> m_WorldObjects {};

    std::vector<std::string> m_LightObjects {};

    uint32_t m_CreatedLights = 0;

    glm::vec3 m_StartPosition {};
    glm::vec3 m_ExitPosition {};

    std::vector<Particle> m_MinimapDisplay {};
};
