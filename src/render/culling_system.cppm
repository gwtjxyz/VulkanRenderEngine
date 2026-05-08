module;

export module culling_system;

import std;

import camera;
import ecs;
import render_types;

class CullingSystem {
public:
    explicit CullingSystem(Camera * camera) : m_Camera(camera) {}

    void setCamera(Camera * camera) {
        m_Camera = camera;
    }

    // TODO: finish implementation (for now pretty much nothing will get culled)
    void cullScene(const std::vector<Entity *> & allEntities) {
        m_VisibleEntities.clear();

        if (!m_Camera) return;

        // Get camera frustum
        Frustum frustum = m_Camera->getFrustum();

        // Check each entity against the frustum
        for (auto entity : allEntities) {
            if (!entity->isActive()) continue;

            auto meshComponent = entity->getComponent<MeshComponent>();
            if (!meshComponent) continue;

            auto transformComponent = entity->getComponent<TransformComponent>();
            if (!transformComponent) continue;

            // Get bounding box of the mesh
            BoundingBox boundingBox = meshComponent->getBoundingBox();
            boundingBox.transform(transformComponent->getTransformMatrix());

            if (frustum.intersects(boundingBox)) {
                m_VisibleEntities.push_back(entity);
            }
        }
    }

    const std::vector<Entity *> & getVisibleEntities() const {
        return m_VisibleEntities;
    }
private:
    Camera * m_Camera;
    std::vector<Entity *> m_VisibleEntities {};
};
