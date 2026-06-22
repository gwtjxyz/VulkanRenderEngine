module;

#ifdef DISABLE_IMPORT_STD
#include <memory>
#include <string>
#include <unordered_map>
#endif

#include <cassert>
#include <cstdint>

export module ecs;

#ifndef DISABLE_IMPORT_STD
import std;
#endif
import glm;

import device_mapper;
import pipeline_manager;
import render_types;
import resource;

// ------------------------------------------------------------------------------------------
// Base class definitions
// ------------------------------------------------------------------------------------------

// Forward declaration
export class Entity;

using std::size_t;

export class ComponentTypeIDSystem {
public:
    template <typename T>
    static size_t getTypeID() {
        static size_t typeID = m_NextTypeID++;
        return typeID;
    }

private:
    static size_t m_NextTypeID;
};

// TODO does this work with C++20 modules?
size_t ComponentTypeIDSystem::m_NextTypeID = 0;

// Base component class
export class Component {
public:
    virtual ~Component() {
        if (m_State != State::Destroyed) {
            Component::onDestroy();
            m_State = State::Destroyed;
        }
    }

    template <typename T>
    static size_t getTypeID() {
        return ComponentTypeIDSystem::getTypeID<T>();
    }

    void initialize() {
        if (m_State == State::Uninitialized) {
            m_State = State::Initializing;
            onInitialize();
            m_State = State::Active;
        }
    }

    void destroy() {
        if (m_State == State::Active) {
            m_State = State::Destroying;
            onDestroy();
            m_State = State::Destroyed;
        }
    }

    [[nodiscard]] bool isActive() const {
        return m_State == State::Active;
    }

    void setOwner(Entity * entity) {
        m_Owner = entity;
    }

    [[nodiscard]] Entity * getOwner() const {
        return m_Owner;
    }

public:
    enum class State {
        Uninitialized,
        Initializing,
        Active,
        Destroying,
        Destroyed
    };

protected:
    virtual void onInitialize() {}
    virtual void onDestroy() {}
    virtual void update(float deltaTime) {}
    virtual void render() {}

    friend class Entity; // Allow Entity to call protected methods
private:
    State m_State = State::Uninitialized;
    Entity * m_Owner = nullptr;
};

export class MeshComponent;
export class TransformComponent;

// Entity class
class Entity {
public:
    explicit Entity(const std::string & entityName) : m_Name(std::string(entityName)) {}

    [[nodiscard]] const std::string & getName() const {
        return m_Name;
    }

    [[nodiscard]] bool isActive() const {
        return m_Active;
    }

    void setActive(const bool active) {
        m_Active = active;

        if (m_DrawBufferIndex != INDEX_UNSET) {
            if (m_Active) {
                DeviceMapperLocator::locate()->getDraw(m_DrawBufferIndex)->layout.enabled = true;
            } else {
                DeviceMapperLocator::locate()->getDraw(m_DrawBufferIndex)->layout.enabled = false;
            }
        }

        if (m_LightBufferIndex != INDEX_UNSET) {
            if (m_Active) {
                DeviceMapperLocator::locate()->getLight(m_LightBufferIndex)->layout.enabled = true;
            } else {
                DeviceMapperLocator::locate()->getLight(m_LightBufferIndex)->layout.enabled = false;
            }
        }
    }

    [[nodiscard]] uint32_t getDrawBufferIndex() const {
        return m_DrawBufferIndex;
    }

    [[nodiscard]] uint32_t getLightBufferIndex() const {
        return m_LightBufferIndex;
    }

    void initialize() {
        for (auto & component : m_Components) {
            component->initialize();
        }
    }

    void update(const float deltaTime) {
        if (!m_Active) return;

        for (const auto & component : m_Components) {
            component->update(deltaTime);
        }
    }

    void render() const {
        if (!m_Active) return;

        for (auto & component : m_Components) {
            component->render();
        }

        if (m_DrawBufferIndex != INDEX_UNSET) {
            auto * meshComponent = getComponent<MeshComponent>();
            auto * transformComponent = getComponent<TransformComponent>();
            if (meshComponent && transformComponent) {
                auto * pipelineManager = PipelineManagerLocator::locate();
                // pipelineManager->bindAndDrawIndexed(m_DrawBufferIndex, 1);
            }
        }
    }

    template <typename T, typename... Args>
    T * addComponent(Args &&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");

        size_t typeID = Component::getTypeID<T>();

        // Check if component of this type already exists
        if (const auto it = m_ComponentMap.find(typeID); it != m_ComponentMap.end()) {
            return static_cast<T *>(it->second);
        }

        // create new component
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T * componentPtr = component.get();
        componentPtr->setOwner(this);
        m_ComponentMap[typeID] = componentPtr;
        m_Components.push_back(std::move(component));

        return componentPtr;
    }

    template <typename T>
    [[nodiscard]] T * getComponent() const {
        const size_t typeID = Component::getTypeID<T>();
        if (const auto it = m_ComponentMap.find(typeID); it != m_ComponentMap.end()) {
            return static_cast<T *>(it->second);
        }
        return nullptr;
    }

    template <typename T>
    bool removeComponent() {
        const size_t typeID = Component::getTypeID<T>();
        if (const auto it = m_ComponentMap.find(typeID); it != m_ComponentMap.end()) {
            const Component * componentPtr = it->second;
            m_ComponentMap.erase(it);

            for (auto compIt = m_Components.begin(); compIt != m_Components.end(); ++compIt) {
                if (compIt->get() == componentPtr) {
                    m_Components.erase(compIt);
                    return true;
                }
            }
        }

        return false;
    }

    void setupDrawBuffer() {
        m_DrawBufferIndex = DeviceMapperLocator::locate()->addDraw();
    }

    void unsetDrawBuffer() {
        if (m_DrawBufferIndex != INDEX_UNSET) {
            DeviceMapperLocator::locate()->removeDraw(m_DrawBufferIndex);
            m_DrawBufferIndex = INDEX_UNSET;
        }
    }

    void setupLightBuffer() {
        m_LightBufferIndex = DeviceMapperLocator::locate()->addLight();
    }

    void unsetLightBuffer() {
        if (m_LightBufferIndex != INDEX_UNSET) {
            DeviceMapperLocator::locate()->removeLight(m_LightBufferIndex);
            m_LightBufferIndex = INDEX_UNSET;
        }
    }

private:
    std::string m_Name;
    bool m_Active = true;
    std::vector<std::unique_ptr<Component>> m_Components {};
    std::unordered_map<size_t, Component *> m_ComponentMap;

    uint32_t m_DrawBufferIndex = INDEX_UNSET;
    uint32_t m_LightBufferIndex = INDEX_UNSET;
};

// ------------------------------------------------------------------------------------------
// Event system
// ------------------------------------------------------------------------------------------

// Event base class
export class Event {
public:
    virtual ~Event() = default;
};

// Event listener interface
export class EventListener {
public:
    virtual ~EventListener() = default;
    virtual void onEvent(const Event & event) = 0;
};

// Event system
export class EventSystem {
public:
    void addListener(EventListener * listener) {
        m_Listeners.push_back(listener);
    }

    void removeListener(EventListener * listener) {
        auto it = std::find(m_Listeners.begin(), m_Listeners.end(), listener);
        if (it != m_Listeners.end()) {
            m_Listeners.erase(it);
        }
    }

    void dispatchEvent(const Event & event) {
        for (auto listener : m_Listeners) {
            listener->onEvent(event);
        }
    }

private:
    std::vector<EventListener *> m_Listeners {};
};

// ------------------------------------------------------------------------------------------
// Component and event definitions
// ------------------------------------------------------------------------------------------

// Transform component
// Handles position, rotation, and scale of entity in 3D space
class TransformComponent : public Component {
public:
    TransformComponent() {}

    void setPosition(const glm::vec3 & pos) {
        m_Position = pos;
        m_TransformDirty = true;
    }

    void setPosition(const float x, const float y, const float z) {
        setPosition(glm::vec3(x, y, z));
    }

    void setRotation(const glm::quat & rot) {
        m_Rotation = rot;
        m_TransformDirty = true;
    }

    void rotateX(const float angle) {
        m_Rotation = glm::rotate(m_Rotation, glm::radians(angle), glm::vec3(1.0f, 0.0f, 0.0f));
        m_TransformDirty = true;
    }

    void rotateY(const float angle) {
        m_Rotation = glm::rotate(m_Rotation, glm::radians(angle), glm::vec3(0.0f, 1.0f, 0.0f));
        m_TransformDirty = true;
    }

    void rotateZ(const float angle) {
        m_Rotation = glm::rotate(m_Rotation, glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f));
        m_TransformDirty = true;
    }

    void setScale(const glm::vec3 s) {
        m_Scale = s;
        m_TransformDirty = true;
    }

    void setScale(const float s) {
        m_Scale = glm::vec3(s);
        m_TransformDirty = true;
    }

    const glm::vec3 & getPosition() const {
        return m_Position;
    }

    const glm::quat & getRotation() const {
        return m_Rotation;
    }

    const glm::vec3 & getScale() const {
        return m_Scale;
    }

    uint32_t getIndex() const {
        return m_Index;
    }

protected:
    void onInitialize() override {
        m_DeviceMapper = DeviceMapperLocator::locate();
        assert(m_DeviceMapper != nullptr);

        // m_Index = m_DeviceMapper->addTransform(glm::mat4(1.0f));
        m_DeviceMapper->getDraw(getOwner()->getDrawBufferIndex())->layout.transformIndex = m_Index;
    }

    void onDestroy() override {
        m_DeviceMapper->removeTransform(m_Index);
        m_DeviceMapper->getDraw(getOwner()->getDrawBufferIndex())->layout.transformIndex = INDEX_UNSET;
    }

    void render() override {
        if (!m_TransformDirty) return;

        auto * transformPtr = m_DeviceMapper->getTransform(m_Index);
        transformPtr->layout.position = glm::vec4(m_Position, 1.0);
        transformPtr->layout.rotation = glm::vec4(m_Rotation.x, m_Rotation.y, m_Rotation.z, m_Rotation.w);
        transformPtr->layout.scale = glm::vec4(m_Scale, 0.0f);

        // Calculate transformation matrix
        const glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), m_Position);
        const glm::mat4 rotationMatrix = glm::mat4_cast(m_Rotation);
        const glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), m_Scale);
        //
        // const glm::mat4 transformMatrix = translationMatrix * rotationMatrix * scaleMatrix;
        // m_DeviceMapper->getTransform(m_Index)->layout.modelMatrix = transformMatrix;

        m_TransformDirty = false;
    }

private:
    glm::vec3 m_Position = glm::vec3(0.0f);
    glm::quat m_Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion
    glm::vec3 m_Scale = glm::vec3(1.0f);

    DeviceMapper * m_DeviceMapper = nullptr;

    // Cached transformation matrix
    uint32_t m_Index = INDEX_UNSET;
    mutable bool m_TransformDirty = true;
};

// Mesh component
// Manages the visual representation of an entity by handling its 3D mesh and material
class MeshComponent : public Component {
public:
    MeshComponent(Mesh * m, Material * mat) : m_Mesh(m), m_Material(mat) {}

    void setMesh(Mesh * m) {
        m_Mesh = m;
    }

    void setMaterial(Material * mat) {
        m_Material = mat;
        m_MaterialOutOfDate = true;
    }

    [[nodiscard]] Mesh * getMesh() const {
        return m_Mesh;
    }

    [[nodiscard]] Material * getMaterial() const {
        return m_Material;
    }

    [[nodiscard]] BoundingBox getBoundingBox() const {
        return BoundingBox {}; // TODO
    }

protected:
    void render() override {
        if (!m_Mesh || !m_Material) return;

        // m_Mesh->fillDrawData(getOwner()->getDrawBufferIndex());

        // Need to set new params if either underlying material changed, or we set a different material
        if (m_Material->update() || m_MaterialOutOfDate) {
            DeviceMapperLocator::locate()->getDraw(getOwner()->getDrawBufferIndex())->layout.materialIndex = m_Material->getIndex();

            m_MaterialOutOfDate = false;
        }
    }

    void onInitialize() override {
        getOwner()->setupDrawBuffer();
    }

    void onDestroy() override {
        getOwner()->unsetDrawBuffer();
    }

private:
    Mesh * m_Mesh = nullptr;
    Material * m_Material = nullptr;

    bool m_MaterialOutOfDate = false;
};

// Light component
// Manages data related to light-emitting entities
export class LightComponent : public Component {
public:
    explicit LightComponent(const glm::vec3 & color = glm::vec3(1.0f), const float intensity = 1.0f)
        : m_Color(color), m_Intensity(intensity) {}

    [[nodiscard]] glm::vec3 getColor() const {
        return m_Color;
    }

    void setColor(const glm::vec3 & color) {
        m_Color = color;

        m_LightDirty = true;
    }

    [[nodiscard]] float getIntensity() const {
        return m_Intensity;
    }

    void setIntensity(const float intensity) {
        m_Intensity = intensity;

        m_LightDirty = true;
    }

protected:
    void render() override {
        if (!m_LightDirty) return;

        auto * lightPtr = DeviceMapperLocator::locate()->getLight(getOwner()->getLightBufferIndex());
        lightPtr->layout.color = m_Color;
        lightPtr->layout.intensity = m_Intensity;

        m_LightDirty = false;
    }

    void onInitialize() override {
        getOwner()->setupLightBuffer();
    }

    void onDestroy() override {
        getOwner()->unsetLightBuffer();
    }

private:
    glm::vec3 m_Color;
    float m_Intensity;

    bool m_LightDirty = true;
};

export class CameraComponent : public Component {
public:
    CameraComponent() {}

    void setPerspective(float fov, float aspect, float near, float far) {
        m_FieldOfView = fov;
        m_AspectRatio = aspect;
        m_NearPlane = near;
        m_FarPlane = far;
        m_ProjectionDirty = true;
    }

    [[nodiscard]] glm::mat4 getViewMatrix() const {
        // Get transform component
        auto transform = getOwner()->getComponent<TransformComponent>();
        if (transform) {
            // Calculate view matrix from transform
            glm::vec3 position = transform->getPosition();
            glm::quat rotation = transform->getRotation();

            // Forward vector (local -Z)
            glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            // Up vector (local +Y)
            glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);

            return glm::lookAt(position, position + forward, up);
        }
        return { 1.0f };
    }

    [[nodiscard]] glm::mat4 getProjectionMatrix() {
        if (m_ProjectionDirty) {
            m_ProjectionMatrix = glm::perspective(
                glm::radians(m_FieldOfView),
                m_AspectRatio,
                m_NearPlane,
                m_FarPlane
            );
            m_ProjectionDirty = false;
        }
        return m_ProjectionMatrix;
    }

private:
    float m_FieldOfView = 45.0f;
    float m_AspectRatio = 16.0f / 9.0f;
    float m_NearPlane = 0.1;
    float m_FarPlane = 1000.0f;

    glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
    glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);
    bool m_ProjectionDirty = true;
};

export class CollisionEvent : public Event {
public:
    CollisionEvent(Entity * e1, Entity * e2) : m_Entity1(e1), m_Entity2(e2) {}

    [[nodiscard]] Entity * getEntity1() const {
        return m_Entity1;
    }

    [[nodiscard]] Entity * getEntity2() const {
        return m_Entity2;
    }

private:
    Entity * m_Entity1;
    Entity * m_Entity2;
};

// Component that listens for events
// Handles physics-related behaviour and responds to collision events through the event system
export class PhysicsComponent : public Component, public EventListener {
public:
    ~PhysicsComponent() override {
        // Unregister as event listener
        getEventSystem().removeListener(this);
    }

    void onEvent(const Event & event) override {
        if (auto collisionEvent = dynamic_cast<const CollisionEvent *>(&event)) {
            // TODO handle collision event
        }
    }

protected:
    void onInitialize() override {
        // Register as event listener
        getEventSystem().addListener(this);
    }

private:
    EventSystem & getEventSystem() {
        // get event system from somewhere (e.g., service locator)
        // TODO probably change this
        static EventSystem eventSystem;
        return eventSystem;
    }
};
