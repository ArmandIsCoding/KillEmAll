#pragma once

#include <SFML/System/Vector2.hpp>

#include <array>
#include <optional>

struct StageBoss
{
    unsigned level = 2;
    float age = 0.f;
    float orbitPhase = 0.f;
    sf::Vector2f position{};
    int health = 0;
    int maxHealth = 0;
    std::array<int, 3> mountHealth{};
    int mountMaxHealth = 0;
    unsigned volleys = 0;
    float reload = 2.5f;
    sf::Vector2f target{};
    float hitFlash = 0.f;
    std::array<float, 3> mountFlash{};
    float lockTime() const { return level == 10 ? 1.05f : .88f; }

    static const char *name(unsigned level);
    unsigned mountCount() const
    {
        return level == 10 ? 3u : 2u;
    }
    bool critical() const
    {
        return health <= 0.38f * maxHealth;
    }
    bool rearPulse() const { return level == 10 && volleys % 3 != 0; }
    float warning() const;
    float gapY() const;
    sf::Vector2f mount(unsigned index) const;
    sf::Vector2f course() const;
    sf::Vector2f nearestHullPoint(sf::Vector2f point) const;
    std::optional<float> hullRayHit(sf::Vector2f origin, sf::Vector2f direction, float reach, float radius) const;
};
