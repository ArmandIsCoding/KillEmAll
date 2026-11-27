#include "Combat.hpp"
#include "HullGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
using Vec = sf::Vector2f;
constexpr float pi = 3.14159265f;

struct Blueprint
{
    const char *name;
    float period;
    std::array<hull::Plate, 3> armor;
    std::array<Vec, 2> hardpoints;
};

// Stepped sections cover each machine's solid central body; detached weapons have their own hitboxes.
constexpr std::array<Blueprint, 9> blueprints{
    {{"CIZALLA", 3.30f, {{{-101, -8, 94, 12}, {-85, 11, 80, 29}, {-34, -39, 42, -8}}}, {{{-64, -53}, {64, -70}}}},
     {"ALBATROS", 2.95f, {{{-77, -10, 70, 12}, {-40, -22, 32, 18}, {22, -16, 66, 0}}}, {{{-15, -45}, {30, 44}}}},
     {"NARVAL", 2.95f, {{{-106, -8, 97, 14}, {-79, -16, 79, 23}, {-10, -31, 29, -15}}}, {{{-57, -24}, {52, -26}}}},
     {"TALADRO", 2.85f, {{{-109, -12, 95, 17}, {-85, 16, 85, 31}, {-22, -38, 34, -12}}}, {{{-66, -36}, {67, -40}}}},
     {"ANACONDA", 2.60f, {{{-40, -14, 27, 15}, {-24, -24, 12, 24}, {24, -8, 45, 9}}}, {{{51, 0}, {98, 0}}}},
     {"BASILISCO", 2.55f, {{{-62, -27, 55, 23}, {-42, -38, 36, 32}, {-71, 19, -35, 40}}}, {{{-53, -39}, {45, -46}}}},
     {"PRISMA", 2.40f, {{{-45, -22, 42, 22}, {-22, -45, 22, 46}, {-61, -9, 64, 9}}}, {{{18, -63}, {18, 63}}}},
     {"ARCANGEL", 2.25f, {{{-111, -11, 100, 15}, {-75, -26, 62, 24}, {-11, -37, 34, -26}}}, {{{-54, 34}, {56, 36}}}},
     {"CORONA NEGRA", 2.10f, {{{-48, -29, 48, 29}, {-31, -47, 30, 47}, {-56, -13, 55, 12}}}, {{{0, 0}, {0, 0}}}}}};

const Blueprint &design(unsigned level)
{
    return blueprints[level - 2];
}

Vec atAngle(float a, float magnitude)
{
    return {std::cos(a) * magnitude, std::sin(a) * magnitude};
}
} // namespace

const char *StageBoss::name(unsigned level)
{
    return level >= 2 && level <= 10 ? design(level).name : "PORTAAVIONES";
}

float StageBoss::warning() const
{
    return std::clamp(1.f - reload / lockTime(), 0.f, 1.f);
}

float StageBoss::gapY() const
{
    constexpr std::array<float, 6> lanes{280, 154, 345, 218, 407, 218};
    const auto gateIndex = volleys / (level == 10 ? 3 : 2);
    return lanes[gateIndex % lanes.size()];
}

sf::Vector2f StageBoss::course() const
{
    const float t = age;
    Vec p{738.f, 266.f};
    switch (level)
    {
    case 2:
        p += Vec{45.f * std::sin(.25f * t), 210.f + 3.f * std::sin(1.5f * t)};
        break;
    case 3:
        p += Vec{-148.f * std::pow(std::max(0.f, std::sin(t * .85f)), 4.f), 135.f * std::sin(.83f * t)};
        break;
    case 4:
        p += Vec{70.f * std::sin(.41f * t), 196.f + 33.f * std::sin(.94f * t)};
        break;
    case 5:
        p += Vec{82.f * std::sin(.48f * t), 195.f + 1.8f * std::sin(11.f * t)};
        break;
    case 6:
        p += Vec{-96.f + 98.f * std::cos(.67f * t), 137.f * std::sin(.97f * t)};
        break;
    case 7:
        p += Vec{52.f * std::sin(.49f * t), 171.f + 8.f * std::abs(std::sin(1.96f * t))};
        break;
    case 8:
        p += Vec{47.f * std::cos(.76f * t), 108.f * std::sin(.76f * t)};
        break;
    case 9:
        p += Vec{66.f * std::sin(.34f * t), -30.f + 104.f * std::sin(.51f * t)};
        break;
    case 10:
        p += Vec{53.f * std::sin(t * .57f), 112.f * std::sin(t * .72f)};
        break;
    }
    // Slide in before firing. The wake and gun mounts travel with their host.
    const float approach = std::max(0.f, 1.f - t / 2.1f);
    p.x += 420.f * approach * approach;
    return p;
}

sf::Vector2f StageBoss::mount(unsigned index) const
{
    if (level == 10)
    {
        const auto angle = orbitPhase + index * (2.f * pi / 3.f);
        return position + Vec{92.f * std::cos(angle), 85.f * std::sin(angle)};
    }
    Vec offset = design(level).hardpoints[index];
    if (level == 2)
        offset.y += 12.f * std::sin(1.73f * age + pi * index);
    if (level == 6)
        offset.y = 37.f * (std::sin(.97f * age - (index + 1) * .82f) - std::sin(.97f * age));
    if (level == 8)
        offset.x += 20.f * std::sin(1.5f * age + pi * index);
    return position + offset;
}

sf::Vector2f StageBoss::nearestHullPoint(Vec point) const
{
    return position + hull::nearestPoint(design(level).armor, point - position);
}

std::optional<float> StageBoss::hullRayHit(Vec origin, Vec direction, float reach, float radius) const
{
    return hull::rayHit(design(level).armor, origin - position, direction, reach, radius);
}

sf::Vector2f Combat::hullContact(const Enemy &enemy, Vec position) const
{
    if (enemy.kind == EnemyKind::StageBossCore)
        return stageBoss_->nearestHullPoint(position);
    if (enemy.kind == EnemyKind::CarrierHull)
        return carrierRaid_.nearestHullPoint(position);
    return enemy.position;
}

void Combat::startStageBoss(unsigned level)
{
    if (stageBoss_ || level < 2 || level > 10)
        return;
    auto &boss = stageBoss_.emplace();
    boss.level = level;
    boss.maxHealth = weaponMode_ == WeaponMode::Arsenal
                         ? static_cast<int>(1600 + 150 * level + (95 + 13 * level) * weaponRank_ + 160 * droneUpgrades_)
                         : static_cast<int>(600 + 120 * level + 32 * weaponRank_);
    boss.health = boss.maxHealth;
    boss.mountMaxHealth = boss.maxHealth / 13;
    boss.position = boss.course();
    const auto add = [&](EnemyKind kind, Vec p, float radius, int hp, unsigned mount) {
        Enemy e{kind, p, {}, radius, 0.f, hp, p.y, 0.f, 0.f, 0, 0.f};
        e.bossMount = mount;
        enemies_.push_back(e);
    };
    for (unsigned i = 0; i < boss.mountCount(); ++i)
    {
        boss.mountHealth[i] = boss.mountMaxHealth;
        add(EnemyKind::StageBossMount, boss.mount(i), level == 10 ? 17.f : 15.f, boss.mountMaxHealth, i);
    }
    add(EnemyKind::StageBossCore, boss.position, 24.f, boss.maxHealth, 0);
}

void Combat::updateStageBoss(float dt, Vec player)
{
    auto &boss = *stageBoss_;
    if (boss.health <= 0)
        return;
    boss.age += dt;
    if (boss.level == 10)
        boss.orbitPhase = std::fmod(boss.orbitPhase + dt * (boss.critical() ? 1.07f : 0.66f), 2.f * pi);
    boss.position = boss.course();
    boss.hitFlash = std::max(0.f, boss.hitFlash - dt);
    for (auto &flash : boss.mountFlash)
        flash = std::max(0.f, flash - dt);
    if (boss.reload > boss.lockTime())
        boss.target = player;
    boss.reload -= dt;
    if (boss.reload > 0.f)
        return;
    fireStageBoss();
    ++boss.volleys;
    boss.reload += design(boss.level).period * (boss.critical() ? 0.77f : 1.f);
}

void Combat::fireStageBoss()
{
    const auto &b = *stageBoss_;
    const float speed = 164.f + 9.f * b.level + (b.critical() ? 15.f : 0.f);
    const auto aimed = [&](Vec p, Vec target, int pairs, float width, float scale = 1.f) {
        const Vec d = target - p;
        if (d.lengthSquared() > 70.f * 70.f)
            fireEnemyFan(p, std::atan2(d.y, d.x), pairs, width, speed * scale, ShotKind::EnemyOrb, 4.5f);
    };
    const auto emit = [&](Vec p, Vec v, float radius, Vec bend = {}) {
        Shot shot{ShotKind::EnemyOrb, p, v, radius, 0.f, 0.f};
        shot.enemyBend = bend;
        shots_.push_back(shot);
    };
    const auto spiral = [&](Vec p, unsigned n, float offset) {
        for (unsigned i = 0; i < n; ++i)
            emit(p, atAngle(offset + i * 2.f * pi / n, speed * .88f), 4.6f);
    };
    const auto wall = [&] {
        // A moving gap remains wide enough for the player even in the last battle.
        const float opening = b.level == 10 ? 39.f : 53.f;
        for (float y = 18.f; y < 533.f; y += 23.f)
            if (std::abs(y - b.gapY()) > opening)
                emit({b.position.x - 20.f, y}, {-speed * .84f, 0.f}, 5.f);
    };
    const Vec core = b.position - Vec{19.f, 0.f};
    if (b.rearPulse())
    {
        // The fortress projects a visible relay onto the opposite edge, behind the frontal shield.
        // The row is locked for the entire warning so moving vertically is always a way out.
        for (int lane = -1; lane <= 1; ++lane)
            emit({16.f, b.target.y + lane * 23.f}, {speed * 1.35f, 0.f}, lane == 0 ? 7.f : 4.f);
    }
    switch (b.level)
    {
    case 2:
        aimed(core, b.target, 0, .16f);
        break;
    case 3:
        aimed(core, b.target, 1, .20f, 1.05f);
        break;
    case 4:
        for (int i = -2; i <= 2; ++i)
            emit(core, {-134.f + 12.f * i, -161.f + 25.f * i}, 5.f, {-52.f, 94.f});
        break;
    case 5:
        aimed(core, b.target, 0, .15f);
        break;
    case 6:
        spiral(core, b.critical() ? 16 : 11, .46f * b.volleys);
        break;
    case 7:
        aimed(core, b.target, 1, .18f, .96f);
        break;
    case 8:
        if (b.volleys % 2 == 0)
            wall();
        else
            spiral(core, 18, .37f * b.volleys);
        break;
    case 9:
        aimed(core, b.target, 2, .14f, 1.04f);
        break;
    case 10:
        if (b.volleys % 3 == 0)
            wall();
        else if (b.volleys % 3 == 1)
            spiral(core, b.critical() ? 34 : 24, .44f * b.volleys);
        else
            aimed(core, b.target, 3, .17f, 1.10f);
        break;
    }
    for (unsigned i = 0; i < b.mountCount(); ++i)
    {
        if (b.mountHealth[i] <= 0)
            continue;
        const Vec p = b.mount(i);
        const float s = i == 0 ? -1.f : 1.f;
        switch (b.level)
        {
        case 2:
            aimed(p, b.target + Vec{0, s * 63.f}, 0, .19f);
            break;
        case 3:
            if (i == b.volleys % 2 || b.critical())
                aimed(p, b.target, 1, .15f, 1.16f);
            break;
        case 4:
            for (int j = 0; j < 3; ++j)
                emit(p, {-34.f - 28.f * j, -212.f + 21.f * j}, 5.f, {-130.f, 25.f + s * 20.f});
            break;
        case 5: {
            // Mortar rounds arc above the bearing, then fall back toward the locked lane.
            constexpr float flight = 1.55f;
            constexpr float gravity = 290.f;
            constexpr float bend = Shot::enemyBendDuration;
            const Vec v = (b.target - p - Vec{0, gravity * bend * (flight - .5f * bend)}) / flight;
            for (int j = -1; j <= 1; ++j)
                emit(p, v + Vec{12.f * j, 20.f * j}, 6.f, {0, gravity});
            break;
        }
        case 6:
            aimed(p, b.target + Vec{0, s * 32.f}, 0, .14f, 1.18f);
            break;
        case 7:
            for (int j = 0; j < (b.critical() ? 7 : 5); ++j)
                emit(p, {-54.f - j * 31.f, -240.f + j * 7.f}, 5.5f, {-23.f, 74.f});
            break;
        case 8:
            if (b.volleys % 2 == 1)
                aimed(p, b.target + Vec{0, s * 68.f}, 0, .12f);
            break;
        case 9:
            if (b.volleys % 3 == 0)
            {
                Enemy fighter{EnemyKind::Striker, p, {-172.f, 35.f * s}, 18.f, 0.f, 9, p.y, 0.f, 1.26f, 0, 0.f};
                fighter.flight = FlightPath::Pursuit;
                enemies_.push_back(fighter);
            }
            else
                aimed(p, b.target + Vec{0, 58.f * s}, 1, .19f);
            break;
        case 10:
            if (b.volleys % 3 == 0)
            {
                // Keep the marked corridor open during the wall. The satellites fire behind it.
                emit(p, {-speed * .65f, 34.f * (i - 1.f)}, 5.f);
            }
            else if (b.volleys % 3 == 1)
                spiral(p, 7, b.age + i);
            else
                aimed(p, b.target + Vec{0, 58.f * (i - 1.f)}, 1, .22f);
            break;
        }
    }
}

void Combat::damageStageBoss(Enemy &enemy, int amount, Vec hit, unsigned chainId)
{
    auto &boss = *stageBoss_;
    if (!boss.health || enemy.health <= 0)
        return;
    enemy.health = std::max(0, enemy.health - amount);
    enemy.hitFlash = 0.12f;
    float &flash = enemy.kind == EnemyKind::StageBossCore ? boss.hitFlash : boss.mountFlash[enemy.bossMount];
    if (flash <= 0.f)
    {
        explode(hit, ExplosionKind::HullImpact);
        flash = 0.11f;
    }
    if (enemy.kind == EnemyKind::StageBossMount)
    {
        boss.mountHealth[enemy.bossMount] = enemy.health;
        if (!enemy.health)
        {
            creditEnemyKill(300 + 50 * boss.level, chainId);
            explodeEnemy(enemy, chainId);
        }
        return;
    }
    boss.health = enemy.health;
    if (boss.health > 0)
        return;
    for (auto &e : enemies_)
        if (e.kind == EnemyKind::StageBossMount)
            e.health = 0;
    boss.mountHealth.fill(0);
    creditEnemyKill(1400 + 600 * boss.level, chainId);
    explode(boss.position + Vec{35, -10}, ExplosionKind::Enemy);
    explode(boss.position, ExplosionKind::Gunship, chainId);
    explosions_.back().comboKills = chainId ? chains_.at(chainId).tally.kills : 1;
}
