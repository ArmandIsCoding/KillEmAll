#pragma once

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace hull
{
struct Plate
{
    float left, top, right, bottom;
};

inline bool clipRayAxis(float origin, float direction, float low, float high, float& enter, float& leave)
{
    if (direction == 0.f)
        return origin >= low && origin <= high;
    const float a = (low - origin) / direction;
    const float b = (high - origin) / direction;
    enter = std::max(enter, std::min(a, b));
    leave = std::min(leave, std::max(a, b));
    return enter <= leave;
}

template<std::size_t N>
sf::Vector2f nearestPoint(const std::array<Plate, N>& plates, sf::Vector2f point)
{
    sf::Vector2f nearest{};
    float best = std::numeric_limits<float>::infinity();
    for (const auto& plate : plates)
    {
        const sf::Vector2f candidate{std::clamp(point.x, plate.left, plate.right),
                                     std::clamp(point.y, plate.top, plate.bottom)};
        const float distance = (point - candidate).lengthSquared();
        if (distance < best)
        {
            nearest = candidate;
            best = distance;
        }
    }
    return nearest;
}

template<std::size_t N>
std::optional<float> rayHit(const std::array<Plate, N>& plates, sf::Vector2f origin,
                            sf::Vector2f direction, float reach, float radius)
{
    std::optional<float> nearest;
    for (const auto& plate : plates)
    {
        float enter = 0.f, leave = nearest.value_or(reach);
        if (clipRayAxis(origin.x, direction.x, plate.left - radius, plate.right + radius, enter, leave) &&
            clipRayAxis(origin.y, direction.y, plate.top - radius, plate.bottom + radius, enter, leave))
            nearest = enter;
    }
    return nearest;
}
}
