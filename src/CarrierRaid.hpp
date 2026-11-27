#pragma once

#include <SFML/System/Vector2.hpp>

#include <optional>
#include <array>

enum class CarrierPhase
{
    Waiting,
    Approach,
    Battle,
    Sinking,
    Aftermath,
    Finished
};
enum class CarrierPart
{
    Catapult,
    ForwardLight,
    AftLight,
    Hull
};

struct CarrierRaid
{
    static constexpr float arrival = 140.f;
    static constexpr float approachDuration = 8.f;
    static constexpr float sinkDuration = 3.f;
    static constexpr int baseHealth = 600;
    static constexpr float fireThreshold = 0.35f;
    static constexpr sf::Vector2f spriteOrigin{176.f, 84.f};
    CarrierPhase phase = CarrierPhase::Waiting;
    float age = 0.f;
    sf::Vector2f position{744.f, 499.f};
    int maxHealth = baseHealth;
    int health = maxHealth;
    float hitFlash = 0.f;
    float launchTimer = 0.9f;
    int queuedFighters = 3;
    float salvoCharge = 0.f;
    sf::Vector2f salvoTarget{};
    unsigned salvos = 0;
    unsigned launches = 0;
    std::array<bool, 3> destroyed{};

    bool occupiesWater() const;
    float harbourLight(float screenX) const;
    sf::Vector2f mount(CarrierPart part) const;
    sf::Vector2f deckGun(unsigned index) const;
    sf::Vector2f readyFighter(unsigned index) const;
    int partHealth(CarrierPart part) const;
    int lights() const { return 2 - int(destroyed[1]) - int(destroyed[2]); }
    bool deckBurning() const { return health <= fireThreshold * maxHealth; }
    float salvoPeriod() const;
    float lockAt() const;
    sf::Vector2f nearestHullPoint(sf::Vector2f point) const;
    std::optional<float> hullRayHit(sf::Vector2f origin, sf::Vector2f direction, float reach, float radius) const;
};
