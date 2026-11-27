#include "Combat.hpp"
#include "HullGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
using Vec = sf::Vector2f;
// Only the solid hull and island block shots above the waterline.
constexpr std::array<hull::Plate, 9> plates{{{6, 49, 345, 59},
                                             {16, 59, 338, 69},
                                             {24, 69, 334, 80},
                                             {29, 80, 329, 91},
                                             {32, 91, 325, 100},
                                             {209, 35, 277, 49},
                                             {218, 27, 275, 35},
                                             {240, 19, 272, 27},
                                             {257, 6, 264, 20}}};

}

bool CarrierRaid::occupiesWater() const
{
    return phase != CarrierPhase::Waiting && phase != CarrierPhase::Finished;
}

float CarrierRaid::harbourLight(float screenX) const
{
    const float x = std::clamp(screenX / 960.f, 0.f, 1.f);
    if (phase == CarrierPhase::Approach)
        return 1.f - 0.88f * std::clamp((age - x * 1.6f) / 0.34f, 0.f, 1.f);
    if (phase == CarrierPhase::Battle)
        return 0.12f;
    if (phase == CarrierPhase::Sinking)
        return 0.12f + 0.88f * std::clamp((age - x * 0.85f) / 1.55f, 0.f, 1.f);
    return 1.f;
}

sf::Vector2f CarrierRaid::mount(CarrierPart part) const
{
    Vec local{146, 72};
    switch (part)
    {
    case CarrierPart::Catapult:
        local = {49, 42};
        break;
    case CarrierPart::ForwardLight:
        local = {121, 22};
        break;
    case CarrierPart::AftLight:
        local = {246, 6};
        break;
    case CarrierPart::Hull:
        break;
    }
    return position - spriteOrigin + local;
}

sf::Vector2f CarrierRaid::deckGun(unsigned i) const
{
    return position - spriteOrigin + Vec{i == 0 ? 24.f : 319.f, 44.f};
}

sf::Vector2f CarrierRaid::readyFighter(unsigned index) const
{
    const float rail = std::clamp(1.f - launchTimer / 0.65f, 0.f, 1.f);
    return position - spriteOrigin + Vec{90.f + 45.f * index - rail * 44.f, 40.f};
}

int CarrierRaid::partHealth(CarrierPart part)
{
    return part == CarrierPart::Hull ? maxHealth : part == CarrierPart::Catapult ? 60 : 40;
}

sf::Vector2f CarrierRaid::nearestHullPoint(Vec point) const
{
    const Vec corner = position - spriteOrigin;
    return corner + hull::nearestPoint(plates, point - corner);
}

std::optional<float> CarrierRaid::hullRayHit(Vec origin, Vec direction, float reach, float radius) const
{
    return hull::rayHit(plates, origin - position + spriteOrigin, direction, reach, radius);
}

const Enemy* Combat::carrierPart(CarrierPart part) const
{
    for (const auto& e : enemies_)
        if ((e.kind == EnemyKind::CarrierPart || e.kind == EnemyKind::CarrierHull) && e.carrierPart == part &&
            e.health > 0)
            return &e;
    return nullptr;
}

void Combat::updateCarrier(float dt, Vec player)
{
    auto& raid = carrierRaid_;
    if (raid.phase == CarrierPhase::Finished)
        return;
    if (raid.phase == CarrierPhase::Waiting)
    {
        if (elapsed_ < CarrierRaid::arrival || battleship_)
            return;
        raid.phase = CarrierPhase::Approach;
        raid.salvoTarget = player;
        return;
    }
    raid.age += dt;
    raid.hitFlash = std::max(0.f, raid.hitFlash - dt);
    if (raid.phase == CarrierPhase::Sinking)
    {
        if (raid.age >= CarrierRaid::sinkDuration)
        {
            raid.phase = CarrierPhase::Aftermath;
            raid.age = 0.f;
        }
        return;
    }
    if (raid.phase == CarrierPhase::Aftermath)
    {
        if (raid.age >= 2.f)
        {
            raid.phase = CarrierPhase::Finished;
            raid.age = 0.f;
            nextBoss_ = 55.f;
        }
        return;
    }
    if (raid.phase == CarrierPhase::Approach)
    {
        if (raid.age < CarrierRaid::approachDuration)
            return;
        raid.phase = CarrierPhase::Battle;
        raid.age = 0.f;
        for (auto part :
             {CarrierPart::Catapult, CarrierPart::ForwardLight, CarrierPart::AftLight, CarrierPart::Hull})
        {
            const auto p = raid.mount(part);
            const auto kind = part == CarrierPart::Hull ? EnemyKind::CarrierHull : EnemyKind::CarrierPart;
            const float radius = part == CarrierPart::Catapult ? 14.f : 11.f;
            Enemy e{kind, p, {}, radius, 0.f, CarrierRaid::partHealth(part), p.y, 0.f, 0.f, 0, 0.f};
            e.carrierPart = part;
            enemies_.push_back(e);
        }
    }

    raid.position = {744.f - 70.f * std::sin(raid.age * 0.16f), 499.f + 1.4f * std::sin(raid.age * 1.65f)};
    if (carrierPart(CarrierPart::Catapult))
    {
        raid.launchTimer -= dt;
        if (raid.queuedFighters == 0 && raid.launchTimer <= 2.6f)
            raid.queuedFighters = 3;
        if (raid.launchTimer <= 0.f)
        {
            const auto launch = raid.readyFighter(0);
            Enemy fighter{EnemyKind::Striker, launch, {-178.f, -80.f}, 19.f, 0.f, 4, launch.y, 0.f, 1.65f, 0, 0.f};
            fighter.flight = FlightPath::DeckLaunch;
            enemies_.push_back(fighter);
            ++raid.launches;
            --raid.queuedFighters;
            raid.launchTimer = raid.queuedFighters == 0 ? 7.4f : 0.56f;
        }
    }
    const int lights = raid.lights();
    // With fewer spotters the gunners commit to an older bearing and need longer to reload.
    raid.salvoCharge += dt / (7.6f - 1.7f * lights);
    if (raid.salvoCharge < CarrierRaid::lockAt(lights))
        raid.salvoTarget = player;
    if (raid.salvoCharge < 1.f)
        return;
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto p = raid.deckGun(i);
        const auto aim = raid.salvoTarget - p;
        if ((player - p).lengthSquared() > 80.f * 80.f)
            fireEnemyFan(p, std::atan2(aim.y, aim.x), 1 + lights, 0.14f, 179.f + lights * 17.f, ShotKind::EnemyOrb,
                         5.f);
    }
    ++raid.salvos;
    raid.salvoCharge = 0.f;
}

void Combat::damageCarrier(Enemy& enemy, int amount, Vec hit, unsigned chainId)
{
    auto& raid = carrierRaid_;
    if (raid.phase != CarrierPhase::Battle || enemy.health <= 0)
        return;
    enemy.health = std::max(0, enemy.health - amount);
    enemy.hitFlash = 0.11f;
    const bool hull = enemy.kind == EnemyKind::CarrierHull;
    if (hull)
    {
        raid.health = enemy.health;
        if (raid.hitFlash <= 0.f)
        {
            raid.hitFlash = 0.10f;
            explode(hit, ExplosionKind::HullImpact);
        }
    }
    if (enemy.health > 0)
        return;
    creditEnemyKill(hull ? 2500 : 350, chainId);
    if (!hull)
    {
        raid.destroyed[static_cast<std::size_t>(enemy.carrierPart)] = true;
        explodeEnemy(enemy, chainId);
        if (enemy.carrierPart == CarrierPart::Catapult)
            raid.queuedFighters = 0;
        return;
    }
    raid.phase = CarrierPhase::Sinking;
    raid.age = 0.f;
    raid.queuedFighters = 0;
    for (auto& other : enemies_)
        if (other.kind == EnemyKind::CarrierPart)
            other.health = 0;
    explode(hit, ExplosionKind::Enemy);
    explode(raid.position, ExplosionKind::Gunship, chainId);
    explosions_.back().comboKills = chainId ? chains_.at(chainId).tally.kills : 1;
}
