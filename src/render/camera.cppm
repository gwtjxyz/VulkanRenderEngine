module;

export module camera;

import components;
import entity_system;
import glm;
import render_types;

export enum class CameraMovement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

export struct Frustum {
    Plane topFace;
    Plane bottomFace;

    Plane leftFace;
    Plane rightFace;

    Plane farFace;
    Plane nearFace;

    bool intersects(BoundingBox boundingBox) const {
        return true; // TODO
    }
};

// TODO: third-person camera
// TODO: switch to quaternions eventually, once I have a UI to debug them
export class Camera {
public:
    explicit Camera(
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), // World origin start
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), // Y-axis as world up
        float yaw = 0.0f,
        float pitch = 0.0f  // Level horizon,
    ) : m_Position(position), m_WorldUp(up) {
        m_Spin.pitch = pitch;
        m_Spin.yaw = yaw;
        m_Spin.roll = 0.0f; // TODO unused for now, maybe use later?

        m_BoundingBox.min = glm::vec3(-0.2f, 0.0f, -0.2f);
        m_BoundingBox.max = glm::vec3(0.2f, 0.5f, 0.2f);

        // initialize front, up, and right
        updateCameraVectors();

        m_Zoom = 50.0f;
        m_MouseSensitivity = 0.1f;
        m_MovementSpeed = 2.5f;
        m_AspectRatio = 16.0f / 9.0f;
        m_Near = 0.1f;
        m_Far = 100.0f;
    }

    [[nodiscard]] glm::mat4 getViewMatrix() const {
        return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
        // return glm::translate(glm::toMat4(glm::quatLookAt(m_Front, m_Up)), m_Position);
    }

    [[nodiscard]] glm::mat4 getProjectionMatrix() const {
        return glm::perspective(glm::radians(m_Zoom), m_AspectRatio, m_Near, m_Far);
    }

    void processKeyboard(const CameraMovement direction, const float deltaTime, bool sprinting, EntitySystem & entitySystem) {
        float velocity = m_MovementSpeed * deltaTime;
        if (sprinting) velocity *= 2;

        glm::vec3 newPosition = m_Position;

        if (m_Gravity) {
            switch (direction) {
                case CameraMovement::FORWARD:
                    newPosition += m_LockedFront * velocity;
                    break;
                case CameraMovement::BACKWARD:
                    newPosition -= m_LockedFront * velocity;
                    break;
                case CameraMovement::LEFT:
                    newPosition -= m_LockedRight * velocity;
                    break;
                case CameraMovement::RIGHT:
                    newPosition += m_LockedRight * velocity;
                    break;
                default:
                    break;
            }
        } else {
            switch (direction) {
                case CameraMovement::FORWARD:
                    newPosition += m_Front * velocity;
                    break;
                case CameraMovement::BACKWARD:
                    newPosition -= m_Front * velocity;
                    break;
                case CameraMovement::LEFT:
                    newPosition -= m_Right * velocity;
                    break;
                case CameraMovement::RIGHT:
                    newPosition += m_Right * velocity;
                    break;
                case CameraMovement::UP:
                    newPosition += m_WorldUp * velocity;
                    break;
                case CameraMovement::DOWN:
                    newPosition -= m_WorldUp * velocity;
                    break;
            }
        }

        // Disable collision for flying around because we don't handle getting stuck in objects yet
        if (!m_Gravity) {
            m_Position = newPosition;
            return;
        }

        BoundingBox newCameraBB {};
        newCameraBB.min = m_BoundingBox.min + newPosition;
        newCameraBB.max = m_BoundingBox.max + newPosition;

        bool collisionFound = false;
        auto query = entitySystem.getObjectsQuery();
        query.each([&](const Components::Transform & t, const Components::Mesh & m) {
            BoundingBox objectBB = m.instance.getBoundingBox();

            if (newCameraBB.intersects(objectBB)) {
                collisionFound = true;
            }
        });

        if (!collisionFound) {
            m_Position = newPosition;
        }
    }

    void processMouseMovement(float xOffset, float yOffset, bool constrainPitch = true) {
        xOffset *= m_MouseSensitivity;
        yOffset *= m_MouseSensitivity;

        m_Spin.yaw += xOffset;
        m_Spin.pitch += yOffset;

        // Avoid flipping
        if (constrainPitch) {
            m_Spin.pitch = glm::clamp(m_Spin.pitch, -89.0f, 89.0f);
        }

        updateCameraVectors();
    }

    Frustum getFrustum() const {
        Frustum frustum;

        const float halfVSide = m_Far * glm::tan(m_Zoom * 0.5f);
        const float halfHSide = halfVSide * m_AspectRatio;
        const glm::vec3 frontMultFar = m_Far * m_Front;

        // Not sure if we should use fastDistance here; but maybe it's okay to compute this "slowly" every frame?
        frustum.nearFace = { m_Position + m_Near * m_Front, m_Front };
        frustum.farFace = { m_Position + frontMultFar, -m_Front };

        // Do we need to normalize here?
        frustum.leftFace = { m_Position, glm::cross(m_Up, frontMultFar + m_Right * halfHSide) };
        frustum.rightFace = { m_Position, glm::cross(frontMultFar - m_Right * halfHSide, m_Up) };
        frustum.topFace = { m_Position, glm::cross(m_Right, frontMultFar - m_Up * halfVSide) };
        frustum.bottomFace = { m_Position, glm::cross(frontMultFar + m_Up * halfVSide, m_Right) };

        return frustum;
    }

    void processMouseScroll(float yOffset) {
        m_Zoom += yOffset;
    }

    [[nodiscard]] glm::vec3 getPosition() const {
        return m_Position;
    }

    [[nodiscard]] glm::vec3 getFront() const {
        return m_Front;
    }

    [[nodiscard]] float getZoom() const {
        return m_Zoom;
    }

    void updateAspectRatio(const float aspectRatio) {
        m_AspectRatio = aspectRatio;
    }

    void setPosition(const glm::vec3 position) {
        m_Position = position;
    }

    void toggleGravity() {
        m_Gravity = !m_Gravity;
        if (m_Gravity && m_Position.y > 0.0f) {
            m_Position.y = 0.0f;
        }
    }
private:
    // TODO use this somewhere else probably, or for interpolation
    // don't really need this for camera but I'll keep it as reference
    // maybe I'll use it in updateCameraVectors() later once I can verify the "easier" method works
    [[nodiscard]] glm::quat quatFromAngles() const {
        glm::quat result{};
        // from https://gamemath.com/book/orient.html#euler_angles_to_quaternion
        const float pitch = m_Spin.pitch, yaw = m_Spin.yaw, roll = m_Spin.roll;
        constexpr float half = 0.5f;

        const float cosPitch = glm::cos(glm::radians(pitch * half));
        const float sinPitch = glm::sin(glm::radians(pitch * half));
        const float cosYaw = glm::cos(glm::radians(yaw * half));
        const float sinYaw = glm::sin(glm::radians(yaw * half));
        const float cosRoll = glm::cos(glm::radians(roll * half));
        const float sinRoll = glm::sin(glm::radians(roll * half));

        result.w = cosPitch * cosYaw * cosRoll + sinPitch * sinYaw * sinRoll;
        result.x = sinPitch * cosYaw * cosRoll + cosPitch * sinYaw * sinRoll;
        result.y = cosPitch * sinYaw * cosRoll - sinPitch * cosYaw * sinRoll;
        result.z = cosPitch * cosYaw * sinRoll - sinPitch * sinYaw * cosRoll;

        return result;
    }

    void updateCameraVectors() {
        m_Front.x = glm::cos(glm::radians(m_Spin.yaw)) * glm::cos(glm::radians(m_Spin.pitch));
        m_Front.y = glm::sin(glm::radians(m_Spin.pitch));
        m_Front.z = glm::sin(glm::radians(m_Spin.yaw)) * glm::cos(glm::radians(m_Spin.pitch));
        m_Front = glm::normalize(m_Front);

        m_LockedFront.x = glm::cos(glm::radians(m_Spin.yaw)) * glm::cos(glm::radians(m_Spin.pitch));
        m_LockedFront.y = 0.0f;
        m_LockedFront.z = glm::sin(glm::radians(m_Spin.yaw)) * glm::cos(glm::radians(m_Spin.pitch));
        m_LockedFront = glm::normalize(m_LockedFront);

        m_Right = glm::normalize(glm::cross(m_Front, m_WorldUp));
        m_LockedRight = glm::normalize(glm::cross(m_LockedFront, m_WorldUp));

        m_Up = glm::normalize(glm::cross(m_Right, m_Front));
    }

private:
    glm::vec3 m_Position{}; // Camera's location in world coordinates
    glm::vec3 m_Front{}; // Forward direction (where camera is looking)
    glm::vec3 m_Up{}; // Camera's local up direction (for roll control)
    glm::vec3 m_Right{}; // Camera's local right direction (perpendicular to front and up)
    glm::vec3 m_WorldUp{}; // Global up vector reference (typically Y-axis)

    glm::vec3 m_LockedFront {};
    glm::vec3 m_LockedRight {};

    // in degrees
    struct Spin {
        float pitch; // X-axis
        float yaw; // Y-axis (also known as head)
        float roll; // Z-axis (also known as bank), unused for now
    };

    Spin m_Spin {};

    BoundingBox m_BoundingBox;

    float m_MovementSpeed{};
    float m_MouseSensitivity{};
    float m_Zoom{};                 // Vertical FOV
    float m_AspectRatio{};
    float m_Near {};
    float m_Far {};

    bool m_Gravity = true;
};
