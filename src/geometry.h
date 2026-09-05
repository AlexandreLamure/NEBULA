#pragma once

#include <glm/vec3.hpp>
#include <glm/glm.hpp>

namespace nebula {

struct Sphere {
    glm::vec3 _center;
    float _radius;

    Sphere(const glm::vec3& center = { 0.0f, 0.0f, 0.0f }, float radius = 0.0f)
    : _center(center)
    , _radius(radius)
    {}
};

struct Frustum {
    glm::vec3 _nearNormal;
    // No far plane (zFar is +inf)
    glm::vec3 _topNormal;
    glm::vec3 _bottomNormal;
    glm::vec3 _rightNormal;
    glm::vec3 _leftNormal;
    glm::vec3 _tipPosition; // position of the tip of the pyramid, which corresponds to the camera position

    Frustum(const glm::vec3& nearNormal = { 0.0f, 0.0f, 1.0f },
            const glm::vec3& topNormal = { 0.0f, 1.0f, 0.0f },
            const glm::vec3& bottomNormal = { 0.0f, -1.0f, 0.0f },
            const glm::vec3& rightNormal = { 1.0f, 0.0f, 0.0f },
            const glm::vec3& leftNormal = { -1.0f, 0.0f, 0.0f },
            const glm::vec3& tipPosition = { 0.0f, 0.0f, 0.0f })
    : _nearNormal(nearNormal)
    , _topNormal(topNormal)
    , _bottomNormal(bottomNormal)
    , _rightNormal(rightNormal)
    , _leftNormal(leftNormal)
    , _tipPosition(tipPosition)
    {}
};

inline bool frustumSphereIntersection(const Frustum& frustum, const Sphere& sphere)
{
    const glm::vec3 v = sphere._center - frustum._tipPosition;
    return glm::dot(v, frustum._nearNormal) + sphere._radius > 0.0f
        && glm::dot(v, frustum._bottomNormal) + sphere._radius > 0.0f
        && glm::dot(v, frustum._topNormal) + sphere._radius > 0.0f
        && glm::dot(v, frustum._leftNormal) + sphere._radius > 0.0f
        && glm::dot(v, frustum._rightNormal) + sphere._radius > 0.0f;
}

}