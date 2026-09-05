#include "Camera.h"

namespace nebula {


// Reverse-Z infinite perspective stores 0 in m[3][3]; an orthographic matrix stores 1.
static bool isProjOrthographic(const glm::mat4& proj) {
    return proj[3][3] == 1.0f;
}

// Aspect ratio is |m[1][1] / m[0][0]| for this projection layout.
static float extractRatio(const glm::mat4& proj) {
    const float f = proj[1][1];
    return std::abs(1.0f / (proj[0][0] / f));
}

// Near plane lives in m[3][2] of the reverse-Z infinite projection.
static float extractNear(const glm::mat4& proj) {
    return proj[3][2];
}

// Vertical FOV is 2*atan(1/m[1][1]), recovered from the perspective scale.
static float extractFov(const glm::mat4& proj) {
    ALWAYS_ASSERT(!isProjOrthographic(proj), "Orthographic camera doesn't have a FoV");
    const float f = proj[1][1];
    return 2.0f * std::atan(1.0f / f);
}

// Camera world position is -R^T * t, recovered from the view matrix rotation and translation.
static glm::vec3 extractPosition(const glm::mat4& view) {
    glm::vec3 pos = {};
    for(u32 i = 0; i != 3; ++i) {
        pos -= glm::vec3(view[0][i], view[1][i], view[2][i]) * view[3][i];
    }
    return pos;
}

// View-space -Z is the third row of the lookAt rotation; negate it to get world forward.
static glm::vec3 extractForward(const glm::mat4& view) {
    return -glm::normalize(glm::vec3(view[0][2], view[1][2], view[2][2]));
}

static glm::vec3 extractRight(const glm::mat4& view) {
    return glm::normalize(glm::vec3(view[0][0], view[1][0], view[2][0]));
}

static glm::vec3 extractUp(const glm::mat4& view) {
    return glm::normalize(glm::vec3(view[0][1], view[1][1], view[2][1]));
}





// Reverse-Z infinite-far projection: near maps to 1 and far to 0 so depth precision clusters at the camera.
glm::mat4 Camera::perspective(float fovY, float ratio, float zNear) {
    float f = 1.0f / std::tan(fovY / 2.0f);
    return glm::mat4(f / ratio, 0.0f,  0.0f,  0.0f,
                  0.0f,    f,  0.0f,  0.0f,
                  0.0f, 0.0f,  0.0f, -1.0f,
                  0.0f, 0.0f, zNear,  0.0f);
}

// glm::orthoZO gives 0–1 depth; the extra matrix flips Z so far is 0 (reverse-Z).
glm::mat4 Camera::orthographic(float left, float right, float bottom, float top, float zNear, float zFar) {
    glm::mat4 reverseZ = glm::mat4(1.0f);
    reverseZ[2][2] = -1.0f;
    reverseZ[3][2] = 1.0f;
    return reverseZ * glm::orthoZO<float>(left, right, bottom, top, zNear, zFar);
}

Camera::Camera() {
    _projection = perspective(toRad(60.0f), 16.0f / 9.0f, 0.001f);
    _view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    update();
}

void Camera::setView(const glm::mat4& matrix) {
    _view = matrix;
    update();
}

void Camera::setProj(const glm::mat4& matrix) {
    _projection = matrix;
    update();
}

void Camera::setFov(float fov) {
    setProj(perspective(fov, ratio(), extractNear(_projection)));
}

void Camera::setRatio(float ratio) {
    setProj(perspective(fov(), ratio, extractNear(_projection)));
}

glm::vec3 Camera::position() const {
    return extractPosition(_view);
}

glm::vec3 Camera::forward() const {
    return extractForward(_view);
}

glm::vec3 Camera::right() const {
    return extractRight(_view);
}

glm::vec3 Camera::up() const {
    return extractUp(_view);
}

const glm::mat4& Camera::projectionMatrix() const {
    return _projection;
}

const glm::mat4& Camera::viewMatrix() const {
    return _view;
}

const glm::mat4& Camera::viewProjMatrix() const {
    return _viewProj;
}

bool Camera::isOrthographic() const {
    return isProjOrthographic(_projection);
}

float Camera::fov() const {
    return isOrthographic() ? 0.0f : extractFov(_projection);
}

float Camera::ratio() const {
    return extractRatio(_projection);
}

void Camera::update() {
    _viewProj = _projection * _view;
}

// Inward-facing plane normals from camera axes, tilted by half-FOV (and its aspect-corrected sibling).
Frustum Camera::buildFrustum() const {
    const glm::vec3 cameraForward = forward();
    const glm::vec3 cameraUp = up();
    const glm::vec3 cameraRight = right();

    Frustum frustum;
    frustum._tipPosition = position();
    frustum._nearNormal = cameraForward;

    const float halfFov = fov() * 0.5f;
    const float halfFovV = std::atan(std::tan(halfFov) * ratio());
    {
        const float c = std::cos(halfFov);
        const float s = std::sin(halfFov);
        frustum._bottomNormal = cameraForward * s + cameraUp * c;
        frustum._topNormal = cameraForward * s - cameraUp * c;
    }
    {
        const float c = std::cos(halfFovV);
        const float s = std::sin(halfFovV);
        frustum._leftNormal = cameraForward * s + cameraRight * c;
        frustum._rightNormal = cameraForward * s - cameraRight * c;
    }

    return frustum;
}
}
