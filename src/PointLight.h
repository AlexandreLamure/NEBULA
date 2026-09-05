#ifndef POINTLIGHT_H
#define POINTLIGHT_H

#include <utils.h>

#include <glm/vec3.hpp>

namespace nebula {

class PointLight {

    public:
        PointLight() = default;

        void setPosition(const glm::vec3& pos) {
            _position = pos;
        }

        void setColor(const glm::vec3& color) {
            _color = color;
        }

        void setRadius(float radius) {
            _radius = radius;
        }


        const glm::vec3& position() const {
            return _position;
        }

        const glm::vec3& color() const {
            return _color;
        }

        float radius() const {
            return _radius;
        }

    private:
        glm::vec3 _position = {};
        glm::vec3 _color = glm::vec3(1.0f);
        float _radius = 10.0f;
};

}

#endif // POINTLIGHT_H
