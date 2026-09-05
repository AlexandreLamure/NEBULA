#ifndef CAMERA_H
#define CAMERA_H

#include <glm/gtc/matrix_transform.hpp>

#include <utils.h>

namespace nebula {

struct Frustum {
    glm::vec3 _nearNormal;
    // No far plane (zFar is +inf)
    glm::vec3 _topNormal;
    glm::vec3 _bottomNormal;
    glm::vec3 _rightNormal;
    glm::vec3 _leftNormal;
};


class Camera {
    public:
        static glm::mat4 perspective(float fovY, float ratio, float zNear);
        static glm::mat4 orthographic(float left, float right, float bottom, float top, float zNear, float zFar);

        Camera();

        void setView(const glm::mat4& matrix);
        void setProj(const glm::mat4& matrix);

        void setFov(float fov);
        void setRatio(float ratio);

        glm::vec3 position() const;
        glm::vec3 forward() const;
        glm::vec3 right() const;
        glm::vec3 up() const;

        const glm::mat4& projectionMatrix() const;
        const glm::mat4& viewMatrix() const;
        const glm::mat4& viewProjMatrix() const;

        bool isOrthographic() const;

        float fov() const;
        float ratio() const;

        Frustum buildFrustum() const;

    private:
        void update();

        glm::mat4 _projection;
        glm::mat4 _view;
        glm::mat4 _viewProj;
};

}

#endif // CAMERA_H
