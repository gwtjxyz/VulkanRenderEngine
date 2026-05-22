module;

export module ecs;

import std;
import glm;

import render_types;
import resource;

// ------------------------------------------------------------------------------------------
// Base class definitions
// ------------------------------------------------------------------------------------------

// Forward declaration
export class Entity;

export class ComponentTypeIDSystem {
public:
    template<typename T>
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

    template<typename T>
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

// Entity class
class Entity {
public:
    explicit Entity(std::string entityName) : m_Name(std::move(entityName)) {}

    [[nodiscard]] const std::string & getName() const {
        return m_Name;
    }

    [[nodiscard]] bool isActive() const {
        return m_Active;
    }

    void setActive(const bool active) {
        m_Active = active;
    }

    void initialize() {
        for (auto & component: m_Components) {
            component->initialize();
        }
    }

    void update(const float deltaTime) {
        if (!m_Active) return;

        for (auto & component : m_Components) {
            component->update(deltaTime);
        }
    }

    void render() {
        if (!m_Active) return;

        for (auto & component : m_Components) {
            component->render();
        }
    }

    template<typename T, typename... Args>
    T * addComponent(Args&&... args) {
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

    template<typename T>
    [[nodiscard]] T * getComponent() const {
        const size_t typeID = Component::getTypeID<T>();
        if (const auto it = m_ComponentMap.find(typeID); it != m_ComponentMap.end()) {
            return static_cast<T *>(it->second);
        }
        return nullptr;
    }

    template<typename T>
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
private:
    std::string m_Name;
    bool m_Active = true;
    std::vector<std::unique_ptr<Component>> m_Components {};
    std::unordered_map<size_t, Component *> m_ComponentMap;
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
export class TransformComponent : public Component {
public:
    void setPosition(const glm::vec3 & pos) {
        m_Position = pos;
        m_TransformDirty = true;
    }

    void setRotation(const glm::quat & rot) {
        m_Rotation = rot;
        m_TransformDirty = true;
    }

    void setScale(const glm::vec3 s) {
        m_Scale = s;
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

    glm::mat4 getTransformMatrix() const {
        if (m_TransformDirty) {
            // Calculate transformation matrix
            glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), m_Position);
            glm::mat4 rotationMatrix = glm::mat4_cast(m_Rotation);
            glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), m_Scale);

            m_TransformMatrix = translationMatrix * rotationMatrix * scaleMatrix;
            m_TransformDirty = false;
        }
        return m_TransformMatrix;
    }
private:
    glm::vec3 m_Position = glm::vec3(0.0f);
    glm::quat m_Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion
    glm::vec3 m_Scale = glm::vec3(1.0f);

    // Cached transformation matrix
    mutable glm::mat4 m_TransformMatrix = glm::mat4(1.0f);
    mutable bool m_TransformDirty = true;
};

// Mesh component
// Manages the visual representation of an entity by handling its 3D mesh and material
export class MeshComponent : public Component {
public:
    MeshComponent(Mesh * m, Material * mat) : m_Mesh(m), m_Material(mat) {}

    void setMesh(Mesh * m) {
        m_Mesh = m;
    }

    void setMaterial(Material * mat) {
        m_Material = mat;
    }

    [[nodiscard]] Mesh * getMesh() const {
        return m_Mesh;
    }

    [[nodiscard]] Material * getMaterial() const {
        return m_Material;
    }

    [[nodiscard]] BoundingBox getBoundingBox() const {
        return BoundingBox{}; // TODO
    }
protected:
    void render() override {
        if (!m_Mesh || !m_Material) return;

        auto transform = getOwner()->getComponent<TransformComponent>();
        if (!transform) return;

        // TODO uncomment and finish
        // m_Material->bind();
        // m_Material->setUniform("modelMatrix", transform->getTransformMatrix());
        // m_Mesh->render();
    }
private:
    Mesh * m_Mesh = nullptr;
    Material * m_Material = nullptr;
};

export class CameraComponent : public Component {
public:
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
        return {1.0f};
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
