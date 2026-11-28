#include "Combat.hpp"
#include "HullGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr float width = 960.f;
constexpr float height = 540.f;
constexpr float pi = 3.14159265f;
constexpr float milestoneDuration = 1.4f;
constexpr float lifeNoticeDuration = 1.6f;
constexpr float lifeGlowDuration = 0.42f;
// A full blink cycle in world seconds, indexed by the number of kills still needed minus one.
constexpr std::array<float, 5> recoveryBlinkPeriods{0.36f, 0.48f, 0.60f, 0.72f, 0.84f};
constexpr float missileThreshold = 30.f;
constexpr float laserThreshold = 65.f;
constexpr float fullLaserRate = 130.f;
constexpr float fullArsenalRate = 180.f;
constexpr std::array<float, Combat::maxDroneUpgrades + 1> droneReloads{0.42f, 0.36f, 0.30f, 0.24f, 0.20f};
constexpr float emitterTravel = 34.f;
constexpr float shieldTipWindow = 1.8f;
constexpr float braidWindupSeconds = 0.18f;
constexpr float burstSpacing = 0.105f;
constexpr float singleReload = 0.23f;
constexpr std::array<unsigned, 10> weaponRankKills{6, 18, 36, 64, 100, 150, 220, 320, 460, 660};

// Count keeps growing with rank. Keep direction changes within a forward firing arc.
float spreadStrength(unsigned rank)
{
    return rank / (rank + 18.f);
}

float rankedReload(float base, unsigned rank)
{
    if (rank <= weaponRankKills.size())
        return base * (1.f - 0.015f * rank);
    return base * (0.85f - 0.17f * spreadStrength(rank - static_cast<unsigned>(weaponRankKills.size())));
}

struct BatteryStats
{
    sf::Vector2f mount;
    float barrel;
    float warning;
    float firstShot;
    float reload;
};

constexpr BatteryStats battery(TurretKind kind)
{
    switch (kind)
    {
        case TurretKind::Cannon: return {{-85.f, -13.f}, 23.f, 0.85f, 1.5f, 3.4f};
        case TurretKind::Flak:   return {{ -8.f, -65.f}, 16.f, 0.65f, 2.3f, 3.7f};
        case TurretKind::Burst:  return {{ 77.f, -39.f}, 19.f, 0.55f, 1.8f, 2.9f};
    }
    return {};
}

// Keep the opening approach, then tighten the schedule gradually throughout the sortie.
constexpr float openingDuration = 24.f;

float flightPressure(float elapsed)
{
    const float time = std::max(0.f, elapsed - openingDuration);
    return time / (time + 150.f);
}

int barrageStage(double elapsed)
{
    constexpr std::array<float, 5> thresholds{40.f, 90.f, 160.f, 260.f, 420.f};
    return static_cast<int>(std::upper_bound(thresholds.begin(), thresholds.end(), elapsed) - thresholds.begin());
}

float frontIntensity(unsigned level)
{
    return static_cast<float>(level - 1) / (Combat::levelCount - 1);
}

int squadronHealth(int base, unsigned level)
{
    return base + (base * static_cast<int>(level - 1) + 3) / 6;
}

struct CannonPattern
{
    int spread;
    int burst;
    int waves;
    float reload;

    float roundsPerSecond() const
    {
        const int rounds = burst * (2 * spread + 1) + waves;
        return rounds / (reload + (burst - 1) * burstSpacing);
    }
};

CannonPattern cannonPattern(int level)
{
    return {level >= 5 ? 2 : level >= 2 ? 1 : 0, level >= 5 ? 3 : level >= 3 ? 2 : 1,
            level >= 4 ? 2 : 0, level >= 3 ? 0.31f : singleReload};
}

struct EnemyStats
{
    float radius;
    int health;
    float speed;
    float weave;
    float frequency;
    float reload;
    int points;
};

constexpr EnemyStats stats(EnemyKind kind)
{
    switch (kind)
    {
        case EnemyKind::Scout:   return {15.f, 2, 145.f, 27.f, 2.3f, 1.65f, 100};
        case EnemyKind::Striker: return {19.f, 4, 106.f, 47.f, 1.8f, 2.1f, 200};
        case EnemyKind::Gunship: return {27.f, 11, 68.f, 32.f, 0.9f, 2.2f, 500};
        case EnemyKind::PatrolBoat: return {22.f, 5, 86.f, 1.8f, 3.f, 2.6f, 300};
        case EnemyKind::BattleshipTurret: return {15.f, Battleship::turretHealth, 0.f, 0.f, 0.f, 0.f, 250};
        case EnemyKind::CarrierPart: return {12.f, 40, 0.f, 0.f, 0.f, 0.f, 350};
        case EnemyKind::CarrierHull: return {16.f, CarrierRaid::baseHealth, 0.f, 0.f, 0.f, 0.f, 2500};
        case EnemyKind::StageBossCore:
        case EnemyKind::StageBossMount: return {}; // The encounter owns armor, movement and salvos.
    }
    return {};
}

void moveAircraft(Enemy& enemy, const EnemyStats& type, float dt, sf::Vector2f playerPosition)
{
    enemy.position.x += enemy.velocity.x * dt;
    if (enemy.flight == FlightPath::DeckLaunch)
    {
        enemy.velocity.y = -80.f - 75.f * enemy.age;
        enemy.position.y += enemy.velocity.y * dt;
        if (enemy.age >= 1.35f)
            enemy.flight = FlightPath::Pursuit;
        return;
    }
    if (enemy.flight == FlightPath::Weave)
    {
        const float phase = enemy.phase + type.frequency * enemy.age;
        enemy.position.y = enemy.centerY + type.weave * std::sin(phase);
        enemy.velocity.y = type.weave * type.frequency * std::cos(phase);
        return;
    }
    if (enemy.flight == FlightPath::CrossLane)
    {
        // Level out at each end of the dive so the pilot can read the next pass.
        const float distance = enemy.centerY < height * 0.5f ? 180.f : -180.f;
        const float startX = width - 55.f - 26.f * enemy.phase;
        const float t = std::clamp((startX - enemy.position.x) / 340.f, 0.f, 1.f);
        enemy.position.y = enemy.centerY + distance * t * t * (3.f - 2.f * t);
        enemy.velocity.y = distance * 6.f * t * (1.f - t) * (-enemy.velocity.x / 340.f);
        return;
    }

    // A hunter steers toward the pilot on approach, then commits to its current heading.
    const float ahead = (playerPosition.x - enemy.position.x) * (enemy.velocity.x > 0.f ? 1.f : -1.f);
    if (enemy.flight == FlightPath::Pursuit && enemy.position.x >= 0.f && enemy.position.x <= width)
    {
        if (ahead <= 140.f)
            enemy.flight = FlightPath::Breakaway;
        else
        {
            const float desired = std::clamp((playerPosition.y - enemy.position.y) * std::abs(enemy.velocity.x) / ahead,
                                             -175.f, 175.f);
            enemy.velocity.y += std::clamp(desired - enemy.velocity.y, -310.f * dt, 310.f * dt);
        }
    }
    const float y = enemy.position.y + enemy.velocity.y * dt;
    enemy.position.y = std::clamp(y, 32.f, height - 42.f);
    if (enemy.position.y != y)
        enemy.velocity.y = 0.f;
}

bool overlaps(sf::Vector2f a, float aRadius, sf::Vector2f b, float bRadius)
{
    const float range = aRadius + bRadius;
    return (a - b).lengthSquared() <= range * range;
}

bool isVisibleTarget(const Enemy& enemy)
{
    return enemy.health > 0 && enemy.position.x >= 0.f && enemy.position.x <= width &&
           enemy.position.y >= 0.f && enemy.position.y <= height;
}

using hull::clipRayAxis;

// Solid decks, bridge, funnel and mast, relative to the waterline of the hull sprite.
constexpr std::array<hull::Plate, 12> hullSections{{
    {-127.f, -8.f, 124.f, 2.f}, {-119.f, 2.f, 124.f, 10.f}, {-108.f, 10.f, 119.f, 16.f},
    {-95.f, 16.f, 107.f, 20.f}, {-62.f, -21.f, 112.f, -8.f}, {-39.f, -32.f, 100.f, -21.f},
    {-24.f, -58.f, 20.f, -32.f}, {30.f, -54.f, 46.f, -32.f}, {49.f, -43.f, 61.f, -32.f},
    {53.f, -83.f, 56.f, -43.f}, {44.f, -74.f, 64.f, -71.f}, {42.f, -65.f, 67.f, -62.f}
}};

std::optional<float> rayCircleHit(sf::Vector2f origin, sf::Vector2f direction, float reach,
                                  sf::Vector2f center, float radius)
{
    const auto toCenter = center - origin;
    const float along = toCenter.dot(direction);
    const float across = toCenter.cross(direction);
    const float chordSquared = radius * radius - across * across;
    if (chordSquared < 0.f)
        return {};
    const float halfChord = std::sqrt(chordSquared);
    if (along + halfChord < 0.f || along - halfChord > reach)
        return {};
    return std::max(0.f, along - halfChord);
}

constexpr float shieldTipX = 8.f;
constexpr float shieldCrownX = 23.f;

// Sweep in muzzle coordinates so a moving shield can also catch a glancing shot.
std::optional<float> shieldImpactY(sf::Vector2f from, sf::Vector2f to, float shotRadius)
{
    constexpr float height = Combat::arsenalShieldHeight;
    const float radius = shotRadius + 1.5f;
    const auto travel = to - from;
    float first = 0.f, last = 1.f;
    if (!clipRayAxis(from.x, travel.x, shieldTipX - radius, shieldCrownX + radius, first, last) ||
        !clipRayAxis(from.y, travel.y, -height - radius, height + radius, first, last))
        return {};

    // Each short piece of the arc has a rectangular face and round joints.
    // Keep the first contact along the shot's path for the spark on the shield.
    static const auto arc = [] {
        std::array<sf::Vector2f, 9> points;
        for (std::size_t i = 0; i < points.size(); ++i)
            points[i] = Combat::arsenalShieldPoint({}, -1.f + 2.f * i / (points.size() - 1));
        return points;
    }();
    const float distance = travel.length();
    const auto direction = distance > 0.f ? travel / distance : sf::Vector2f{1.f, 0.f};
    float nearest = std::numeric_limits<float>::infinity();
    float hitY = 0.f;
    for (std::size_t i = 0; i < arc.size(); ++i)
    {
        const auto joint = arc[i];
        if (const auto hit = rayCircleHit(from, direction, distance, joint, radius); hit && *hit < nearest)
        {
            nearest = *hit;
            hitY = joint.y;
        }
        if (i + 1 == arc.size())
            break;
        const auto edge = arc[i + 1] - joint;
        const float length = edge.length();
        const auto tangent = edge / length;
        const auto local = from - joint;
        float enter = 0.f, leave = std::min(distance, nearest);
        if (clipRayAxis(local.dot(tangent), direction.dot(tangent), 0.f, length, enter, leave) &&
            clipRayAxis(local.cross(tangent), direction.cross(tangent), -radius, radius, enter, leave) &&
            enter < nearest)
        {
            nearest = enter;
            hitY = joint.y + (local + direction * enter).dot(tangent) * tangent.y;
        }
    }
    if (!std::isfinite(nearest))
        return {};
    return std::clamp(hitY / height, -1.f, 1.f);
}

sf::Vector2f velocityAt(float angle, float speed)
{
    return {speed * std::cos(angle), speed * std::sin(angle)};
}

float waveOffset(float age, float phase)
{
    // Vary the length of each leg. The offset stays bounded even when the turns are sharp.
    const float cycle = 2.35f * age + 0.23f + 0.11f * std::sin(9.f * age + phase);
    const float triangle = 1.f - 4.f * std::abs(cycle - std::floor(cycle) - 0.5f);
    return 31.f * triangle + 8.f * std::sin(17.f * age + phase);
}

float salvoOffset(float age, const Shot& shot)
{
    // Start exactly on the moving emitter, then widen the two crossing streams downrange.
    const float amplitude = emitterTravel + (24.f + 162.f * spreadStrength(shot.weaponRank)) * std::min(age, 1.65f);
    return amplitude * std::sin(shot.wavePhase - 7.4f * shot.waveTempo * age);
}

void recordTrail(Shot& shot)
{
    auto& history = shot.trailHistory;
    if (!history.empty() && shot.age - history.back().age < 1.f / 30.f)
        return;
    if (history.size() > 1 && history[1].age < shot.age - Combat::trailLifetime)
        history.erase(history.begin());
    history.push_back({shot.age, shot.position, shot.velocity});
}

void steerShot(Shot& shot, const Enemy& target, float dt)
{
    const auto offset = target.position - shot.position;
    const float speed = shot.velocity.length();
    const float angle = std::atan2(shot.velocity.cross(offset), shot.velocity.dot(offset));
    // Tighten the turning circle at close range so a shot can catch a target it overshot.
    const float turn = dt * std::max(10.f, 1.6f * speed / std::max(28.f, offset.length()));
    const float change = std::clamp(angle, -turn, turn);
    if (shot.kind == ShotKind::Player && (shot.hasTrail() || std::abs(angle) > 0.0001f))
        recordTrail(shot);
    const float heading = std::atan2(shot.velocity.y, shot.velocity.x) + change;
    const float cruiseSpeed = shot.kind == ShotKind::PlayerWave ? 545.f : 630.f;
    shot.velocity = velocityAt(heading, speed + (cruiseSpeed - speed) * (6.f * dt));
    if (shot.kind == ShotKind::Salvo || shot.kind == ShotKind::PlayerWave)
        shot.lastGuidedAge = shot.age + dt;
    if (shot.kind == ShotKind::PlayerWave)
    {
        shot.waveDriftX = shot.velocity.x;
        shot.waveDriftY = shot.velocity.y;
    }
}
}

int Enemy::maxHealth() const
{
    return spawnHealth;
}

sf::Vector2f Battleship::mountPosition(TurretKind kind) const
{
    return position + battery(kind).mount;
}

sf::Vector2f Battleship::nearestHullPoint(sf::Vector2f point) const
{
    return position + hull::nearestPoint(hullSections, point - position);
}

bool Battleship::touchesHull(sf::Vector2f point, float radius) const
{
    return !sinking && (point - nearestHullPoint(point)).lengthSquared() <= radius * radius;
}

std::optional<float> Battleship::hullRayHit(sf::Vector2f origin, sf::Vector2f direction, float reach, float radius) const
{
    if (sinking)
        return {};
    return hull::rayHit(hullSections, origin - position, direction, reach, radius);
}

sf::Vector2f Enemy::turretPivot() const
{
    return kind == EnemyKind::BattleshipTurret ? position : position + sf::Vector2f{-10.f, -5.f};
}

sf::Vector2f Enemy::turretMuzzle() const
{
    const float barrel = kind == EnemyKind::BattleshipTurret ? battery(turretKind).barrel : 15.f;
    return turretPivot() + turretDirection() * barrel;
}

float Enemy::aimWarning() const
{
    if (kind != EnemyKind::BattleshipTurret || (turretKind == TurretKind::Burst && volleys % 3 != 0))
        return 0.f;
    return std::clamp(1.f - fireTimer / battery(turretKind).warning, 0.f, 1.f);
}

sf::Vector2f Enemy::turretDirection() const
{
    const auto offset = turretTarget - turretPivot();
    return offset.lengthSquared() > 0.f ? offset.normalized() : sf::Vector2f{0.f, -1.f};
}

sf::Vector2f Shot::enemyPastPosition(float lookback) const
{
    const float back = std::clamp(lookback, 0.f, age);
    const float bent = std::clamp(enemyBendDuration - (age - back), 0.f, back);
    return position - velocity * back + enemyBend * (0.5f * bent * bent);
}

int Shot::directDamage() const
{
    if (kind == ShotKind::Braid)
        return 16;
    return kind == ShotKind::Player ? 1 : 2;
}

Shot::TrailSample Shot::trailSample(float lookback) const
{
    const float when = age - lookback;
    const auto after = std::lower_bound(trailHistory.begin(), trailHistory.end(), when,
        [](const TrailSample& sample, float time) { return sample.age < time; });
    const auto next = after == trailHistory.end() ? TrailSample{age, position, velocity} : *after;
    if (after == trailHistory.begin())
    {
        // Before the first guided turn the path is exactly the original parabola.
        const float dt = next.age - when;
        const sf::Vector2f pastVelocity = next.velocity - sf::Vector2f{0.f, verticalAcceleration * dt};
        return {when, next.position - (next.velocity + pastVelocity) * (0.5f * dt), pastVelocity};
    }
    const auto& prev = *(after - 1);
    const float t = (when - prev.age) / (next.age - prev.age);
    return {when, prev.position + (next.position - prev.position) * t,
            prev.velocity + (next.velocity - prev.velocity) * t};
}

float Explosion::shockwaveRadius() const
{
    const float remaining = 1.f - std::clamp(age / duration, 0.f, 1.f);
    return 14.f + 112.f * (1.f - remaining * remaining);
}

void Combat::reset()
{
    *this = Combat{};
}

void Combat::setPowerInput(bool precisionHeld, bool guidanceHeld)
{
    const bool flying = !finished() && !levelCleared();
    precision_.held = flying && precisionHeld;
    guidance_.held = flying && guidanceHeld;
}

bool Combat::levelCleared() const
{
    if (level_ > 1)
        return stageBoss_ && stageBoss_->health == 0;
    return carrierRaid_.phase == CarrierPhase::Sinking || carrierRaid_.phase == CarrierPhase::Aftermath ||
           carrierRaid_.phase == CarrierPhase::Finished;
}

void Combat::clearEncounters()
{
    enemies_.clear();
    shots_.clear();
    fadingTrails_.clear();
    laserPulses_.clear();
    fireRatePickup_.reset();
    chargedTip_ = 0;
    tipTimeLeft_ = braidTimeLeft_ = 0.f;
}

void Combat::finishLevel()
{
    if (level_ == levelCount)
    {
        campaignWon_ = true;
        return;
    }
    ++level_;
    levelStartTime_ = elapsed_;
    levelStartDistance_ = distance_;
    clearAge_ = 0.f;
    stageBoss_.reset();
    startLevelWaves();
}

void Combat::startLevelWaves()
{
    nextFormation_ = 2.6f;
    nextPatrol_ = 8.f;
    formation_ = 0;
}

const Enemy* Combat::bossTurret(TurretKind kind) const
{
    const auto match = std::find_if(enemies_.begin(), enemies_.end(), [kind](const Enemy& enemy) {
        return enemy.kind == EnemyKind::BattleshipTurret && enemy.turretKind == kind && enemy.health > 0;
    });
    return match == enemies_.end() ? nullptr : &*match;
}

int Combat::bossHealth() const
{
    return battleship_ ? battleship_->health : 0;
}

const Enemy* Combat::guidanceTarget(const Enemy* excluded) const
{
    if (!guidanceActive())
        return nullptr;

    const Enemy* best = nullptr;
    for (const auto& enemy : enemies_)
    {
        if (&enemy == excluded || !isVisibleTarget(enemy))
            continue;
        // Equal classes retain formation order instead of switching on every hit.
        const auto priority = [this](const Enemy& target) {
            if (target.kind == EnemyKind::StageBossMount)
                return stageBoss_->maxHealth + 1;
            return target.kind == EnemyKind::CarrierPart ? carrierRaid_.maxHealth + 1 : target.maxHealth();
        };
        if (!best || priority(enemy) > priority(*best))
            best = &enemy;
    }
    return best;
}

void Combat::PowerMeter::advance(float realDt, float worldTime, float useSeconds, float refillSeconds)
{
    if (active())
    {
        charge -= static_cast<double>(realDt) / useSeconds;
        if (charge <= 1e-7)
        {
            charge = 0.0;
            depleted = true;
        }
    }
    else if (charge < 1.0)
    {
        charge += static_cast<double>(realDt) / refillSeconds;
        if (charge >= 1.0 - 1e-7)
        {
            charge = 1.0;
            depleted = false;
            ++refills;
            lastRefillTime = worldTime;
        }
    }
}

int Combat::power() const
{
    constexpr std::array<float, 4> upgrades{8.f, 18.f, 30.f, 45.f};
    return 1 + static_cast<int>(std::upper_bound(upgrades.begin(), upgrades.end(), elapsed_) - upgrades.begin());
}

std::uint64_t Combat::nextWeaponRankKills() const
{
    if (weaponRank_ < weaponRankKills.size())
        return weaponRankKills[weaponRank_];
    // R11 costs 200 kills, R12 costs 220, then each step costs 20 more.
    const std::uint64_t n = weaponRank_ + std::uint64_t{1} - weaponRankKills.size();
    const std::uint64_t cost = 190 + 10 * n;
    constexpr auto maxKills = std::numeric_limits<std::uint64_t>::max();
    if (n > (maxKills - weaponRankKills.back()) / cost)
        return maxKills;
    return weaponRankKills.back() + n * cost;
}

sf::Vector2f Combat::arsenalShieldPoint(sf::Vector2f muzzle, float normalizedY)
{
    const float y = std::clamp(normalizedY, -1.f, 1.f);
    return muzzle + sf::Vector2f{shieldCrownX - (shieldCrownX - shieldTipX) * y * y,
                                 arsenalShieldHeight * y};
}

float Combat::shieldTipGlow(int side) const
{
    if (braidTimeLeft_ > 0.f)
        return 1.f;
    return side == chargedTip_ ? std::min(1.f, tipTimeLeft_ / 0.55f) : 0.f;
}

float Combat::braidWindup() const
{
    return braidTimeLeft_ / braidWindupSeconds;
}

void Combat::catchShieldShot(float normalizedY)
{
    if (std::abs(normalizedY) < shieldTipStart || braidTimeLeft_ > 0.f || gameOver())
        return;
    const int side = normalizedY < 0.f ? -1 : 1;
    if (tipTimeLeft_ <= 0.f)
    {
        chargedTip_ = side;
        tipTimeLeft_ = shieldTipWindow;
    }
    else if (side != chargedTip_)
    {
        chargedTip_ = 0;
        tipTimeLeft_ = 0.f;
        braidTimeLeft_ = braidWindupSeconds;
    }
}

void Combat::updateBraid(float dt)
{
    tipTimeLeft_ = std::max(0.f, tipTimeLeft_ - dt);
    if (braidTimeLeft_ <= 0.f)
        return;
    braidTimeLeft_ = std::max(0.f, braidTimeLeft_ - dt);
    if (braidTimeLeft_ == 0.f)
    {
        shots_.push_back({ShotKind::Braid, braidFocus(), {805.f, 0.f}, 9.f, 0.f, 0.f});
        ++braidsFired_;
    }
}

sf::Vector2f Combat::screenShakeOffset() const
{
    constexpr float duration = 0.36f;
    // The newest heavy blast supplies one kick, even when several ships fall together.
    const auto blast = std::find_if(explosions_.rbegin(), explosions_.rend(),
        [](const Explosion& effect) { return effect.kind == ExplosionKind::Gunship; });
    if (blast == explosions_.rend() || blast->age >= duration)
        return {};

    const float remaining = 1.f - blast->age / duration;
    const float side = blast->seed % 2 ? 1.f : -1.f;
    const float phase = 2.f * pi * blast->age;
    return sf::Vector2f{side * std::cos(13.f * phase), 0.6f * std::sin(16.f * phase)}
           * (gunshipShakeAmplitude * remaining * remaining);
}

void Combat::update(float dt, sf::Vector2f playerPosition, sf::Vector2f muzzleOffset)
{
    if (!std::isfinite(dt) || dt <= 0.f || campaignWon_)
        return;

    const bool hadMilestone = !milestones_.empty();
    // Substeps keep small targets hittable even if a frame takes longer.
    while (dt > 0.f)
    {
        const float realSlice = std::min(dt, maxStep);
        const float slice = realSlice * timeScale();
        if (!laserPulses_.empty())
        {
            for (auto& ray : laserPulses_)
                ray.age += slice;
            if (laserPulses_.front().age >= LaserPulse::duration)
                laserPulses_.clear();
        }
        for (auto& explosion : explosions_)
            explosion.age += slice;
        for (auto& trail : fadingTrails_)
            trail.age += slice;
        fadingTrails_.erase(std::remove_if(fadingTrails_.begin(), fadingTrails_.end(),
            [](const FadingTrail& trail) { return trail.age >= trailLifetime; }), fadingTrails_.end());

        if (!gameOver())
        {
            step(slice, playerPosition, muzzleOffset);
            if (campaignWon_)
                break;
            precision_.advance(realSlice, elapsed_, 4.f, 8.f);
            guidance_.advance(realSlice, elapsed_, 5.f, 10.f);
        }
        else if (carrierRaid_.phase == CarrierPhase::Sinking || carrierRaid_.phase == CarrierPhase::Aftermath)
            updateCarrier(slice, playerPosition);
        else if (battleship_ && battleship_->sinking)
            updateBattleship(slice);
        explosions_.erase(std::remove_if(explosions_.begin(), explosions_.end(), [this](const Explosion& effect) {
            if (effect.age < effect.duration)
                return false;
            if (effect.chainId != 0)
            {
                auto chain = chains_.find(effect.chainId);
                if (--chain->second.liveWaves == 0)
                    chains_.erase(chain);
            }
            return true;
        }), explosions_.end());
        if (gameOver() && explosions_.empty() && fadingTrails_.empty() && !(battleship_ && battleship_->sinking) &&
            carrierRaid_.phase != CarrierPhase::Sinking && carrierRaid_.phase != CarrierPhase::Aftermath)
            break;

        dt -= realSlice;
    }
    // Start the display timer at frame end so each notice stays readable after a long update.
    if (gameOver())
        milestones_.clear();
    else if (!milestones_.empty() && (!hadMilestone || elapsed_ >= milestoneUntil_))
    {
        if (hadMilestone)
        {
            milestones_.pop_front();
            ++finishedMilestones_;
        }
        milestoneUntil_ = elapsed_ + milestoneDuration;
    }
}

void Combat::step(float dt, sf::Vector2f playerPosition, sf::Vector2f muzzleOffset)
{
    elapsed_ += dt;
    if (recoveryBlinkPhase_)
    {
        const float period = recoveryBlinkPeriods[killsPerLife - recoveryKills_ - 1];
        *recoveryBlinkPhase_ = std::fmod(*recoveryBlinkPhase_ + dt / period, 1.f);
    }
    const float pressure = flightPressure(elapsed_);
    const float opening = std::min(elapsed(), openingDuration);
    const float front = frontIntensity(level());
    distance_ += dt * (105.f + 0.5f * opening + 220.f * pressure) * (1.f + 0.16f * front);
    grace_ = std::max(0.f, grace_ - dt);
    if (levelCleared())
    {
        clearAge_ += dt;
        arsenalMuzzle_ = playerPosition + muzzleOffset;
        if (!drones_.empty())
            updateDrones(dt, playerPosition);
        if (level_ == 1)
            updateCarrier(dt, playerPosition);
        if (level_ == 1 ? carrierRaid_.phase == CarrierPhase::Finished : clearAge_ >= 2.6f)
            finishLevel();
        return;
    }
    if (level_ == 1)
    {
        updateCarrier(dt, playerPosition);
        if (!carrierRaid_.occupiesWater())
            updateBattleship(dt);
    }
    else
    {
        if (!stageBoss_ && levelTime() >= bossArrival())
        {
            clearEncounters();
            explosions_.clear();
            chains_.clear();
            startStageBoss(level_);
        }
        if (stageBoss_)
            updateStageBoss(dt, playerPosition);
    }
    // Keep a few aerial escorts during the encounter and give the battleship the water lane.
    const float aerialRate = carrierRaid_.phase == CarrierPhase::Approach ? 0.f :
                             carrierRaid_.occupiesWater() ? 0.18f : battleship_ ? 0.35f : 1.f;
    nextFormation_ -= aerialRate * dt;
    if (!stageBoss_ && nextFormation_ <= 0.f)
    {
        spawnFormation();
        nextFormation_ += (2.95f - 0.022f * opening) / ((1.f + 2.65f * pressure) * (1.f + 0.24f * front));
    }
    if (!stageBoss_ && !battleship_ && !carrierRaid_.occupiesWater())
    {
        nextPatrol_ -= dt;
        if (nextPatrol_ <= 0.f)
        {
            spawnPatrol();
            nextPatrol_ += (13.5f - 0.055f * opening) / ((1.f + 0.94f * pressure) * (1.f + 0.30f * front));
        }
    }
    updatePickup(dt, playerPosition);
    updateEnemies(dt, playerPosition);
    const bool hadShield = weaponMode_ == WeaponMode::Arsenal;
    const auto previousMuzzle = arsenalMuzzle_;
    arsenalMuzzle_ = playerPosition + muzzleOffset;
    arsenalShieldFlash_ = std::max(0.f, arsenalShieldFlash_ - dt / 0.18f);
    updateWeapon(dt, arsenalMuzzle_);
    if (weaponMode_ == WeaponMode::Arsenal)
        updateBraid(dt);
    updateShots(dt);
    collide(playerPosition, hadShield ? previousMuzzle : arsenalMuzzle_, dt);
    if (levelCleared())
    {
        clearEncounters();
        setPowerInput(false, false);
    }
    if (weaponMode_ == WeaponMode::Arsenal && !gameOver())
        updateDrones(dt, playerPosition);
}

void Combat::spawnFormation()
{
    // Alternating entry lanes and silhouettes leave readable routes through each wave.
    constexpr std::array<float, 8> lanes{270.f, 144.f, 375.f, 190.f, 348.f, 230.f, 397.f, 150.f};
    const float lane = lanes[formation_ % lanes.size()];
    const unsigned slot = formation_ % 8;
    unsigned pattern = slot % 4;
    if (level() >= 2 && slot == 4) pattern = 4;
    if (level() >= 5 && slot == 1) pattern = 5;
    if (level() >= 8 && slot == 6) pattern = 6;
    const float mirror = (formation_ / 4) % 2 == 0 ? 1.f : -1.f;
    const float front = frontIntensity(level());
    const float flightSpeed = (1.f + 0.7f * flightPressure(elapsed_)) * (1.f + 0.16f * front);
    auto add = [&](EnemyKind kind, float delayX, float offsetY, float phase,
                   FlightPath flight = FlightPath::Weave, bool rear = false) {
        const auto type = stats(kind);
        const float center = std::clamp(lane + offsetY, 78.f, height - 78.f);
        const float x = rear ? -type.radius - delayX : width + type.radius + delayX;
        const float y = flight == FlightPath::Weave ? center + type.weave * std::sin(phase) : center;
        // The heavy convoy holds formation so its escorts stay within shockwave range.
        const float speedScale = pattern == 3 ? 1.f : std::uniform_real_distribution<float>{0.82f, 1.22f}(flightRandom_);
        const float speed = (flight == FlightPath::Pursuit ? 181.f : type.speed) * flightSpeed;
        // Delayed aircraft fire after entry; stagger the first volleys within a fast formation.
        const float ready = 0.72f + delayX * 0.002f;
        enemies_.push_back({kind, {x, y}, {(rear ? 1.f : -1.f) * speed * speedScale, 0.f}, type.radius, 0.f,
                            squadronHealth(type.health, level()), center, phase, ready, 0, 0.f});
        enemies_.back().flight = flight;
    };

    switch (pattern)
    {
        case 0: // echelon
            for (int i = 0; i < 4; ++i)
                add(EnemyKind::Scout, i * 58.f, mirror * i * 31.f, i * 0.55f,
                    i % 2 ? FlightPath::CrossLane : FlightPath::Weave);
            break;
        case 1: // pincer
            add(EnemyKind::Striker, 0.f, -58.f, 0.f);
            add(EnemyKind::Striker, 152.f, 58.f, pi, FlightPath::Pursuit, formation_ % 8 == 5);
            add(EnemyKind::Scout, 110.f, 0.f, 0.f);
            break;
        case 2: // chevron
            add(EnemyKind::Striker, 0.f, 0.f, 0.f);
            for (int i = 1; i <= 2; ++i)
            {
                add(EnemyKind::Scout, 54.f * i, -40.f * i, 0.f);
                add(EnemyKind::Scout, 54.f * i, 40.f * i, pi, FlightPath::CrossLane);
            }
            break;
        case 3: // escorted heavy
            add(EnemyKind::Gunship, 0.f, 0.f, 0.f);
            add(EnemyKind::Scout, 85.f, -82.f, 0.f);
            if (elapsed_ >= 30.f && (formation_ / 4) % 2 == 1)
            {
                add(EnemyKind::Gunship, 70.f, 64.f, 0.f);
                if (power() == 5 && formation_ % 16 == 7)
                {
                    // Two compact escort rows fly at convoy speed and leave the lower lane open.
                    for (int column = 0; column < 4; ++column)
                    {
                        for (int row = 0; row < 2; ++row)
                        {
                            const float x = 42.f + 34.f * column + 12.f * row;
                            const float y = row == 0 ? -46.f + 16.f * (column % 2) : 108.f - 16.f * (column % 2);
                            add(EnemyKind::Scout, x, y, 0.6f * column + row * pi);
                            auto& escort = enemies_.back();
                            escort.velocity.x = -stats(EnemyKind::Gunship).speed * flightSpeed;
                            escort.fireTimer = 3.2f + 0.45f * (column * 2 + row);
                        }
                    }
                }
            }
            else
                add(EnemyKind::Scout, 85.f, 82.f, pi);
            break;
        case 4: // Two pairs screen a high or low corridor; neither pair crosses it.
        {
            const float opening = (formation_ / 8) % 2 ? 180.f : 345.f;
            for (const float side : {-1.f, 1.f})
            {
                const float flank = opening + side * 123.f - lane;
                add(EnemyKind::Striker, 20.f, flank, side * 0.8f);
                add(EnemyKind::Scout, 140.f, flank + side * 28.f, side * 1.4f);
            }
            break;
        }
        case 5: // Two marked hunters approach from opposite sides on different beats.
            add(EnemyKind::Scout, 0.f, 62.f, 0.f, FlightPath::CrossLane);
            add(EnemyKind::Striker, 88.f, -56.f, 0.5f, FlightPath::Pursuit, true);
            add(EnemyKind::Striker, 295.f, 108.f, 1.6f, FlightPath::Pursuit);
            break;
        case 6: // A forward heavy draws fire before a second heavy enters the other lane.
        {
            const float wing = lane < height * 0.5f ? 367.f : 163.f;
            add(EnemyKind::Gunship, 0.f, 0.f, 0.f);
            add(EnemyKind::Scout, 76.f, 46.f, 0.7f);
            add(EnemyKind::Gunship, 190.f, wing - lane, pi);
            add(EnemyKind::Striker, 305.f, wing - lane + 45.f, 0.5f, FlightPath::CrossLane);
            break;
        }
    }
    const int stage = barrageStage(elapsed_);
    if (pattern < 3 && stage >= 2)
    {
        const float farLane = lane < height * 0.5f ? 388.f : 146.f;
        add(EnemyKind::Striker, 156.f, farLane - lane, 0.8f, FlightPath::CrossLane);
        if (stage >= 4)
            add(EnemyKind::Scout, 224.f, farLane - lane + 45.f * mirror, 1.7f);
    }
    ++formation_;
}

void Combat::updateEnemies(float dt, sf::Vector2f playerPosition)
{
    const float fireRate = (1.f + 0.004f * std::min(elapsed(), openingDuration) + 0.82f * flightPressure(elapsed_)) *
                           (1.f + 0.28f * frontIntensity(level()));
    for (auto& enemy : enemies_)
    {
        enemy.age += dt;
        enemy.hitFlash = std::max(0.f, enemy.hitFlash - dt);
        if (enemy.stageBossPart())
        {
            const auto next = enemy.kind == EnemyKind::StageBossCore ? stageBoss_->position : stageBoss_->mount(enemy.bossMount);
            enemy.velocity = (next - enemy.position) / dt;
            enemy.position = next;
            continue;
        }
        if (enemy.kind == EnemyKind::CarrierPart || enemy.kind == EnemyKind::CarrierHull)
        {
            const auto mount = carrierRaid_.mount(enemy.carrierPart);
            enemy.velocity = (mount - enemy.position) / dt;
            enemy.position = mount;
            continue;
        }
        if (enemy.kind == EnemyKind::BattleshipTurret)
        {
            updateBossTurret(enemy, dt, playerPosition);
            continue;
        }
        if (enemy.kind == EnemyKind::PatrolBoat)
        {
            updateBoat(enemy, dt, playerPosition);
            continue;
        }
        const auto type = stats(enemy.kind);
        moveAircraft(enemy, type, dt, playerPosition);

        if (enemy.position.x < enemy.radius || enemy.position.x > width - enemy.radius)
            continue;
        enemy.fireTimer -= dt;
        if (enemy.fireTimer <= 0.f && enemy.position.x > 65.f)
        {
            // A plane passing close to the player never fires at point-blank range.
            if ((enemy.position - playerPosition).lengthSquared() > 125.f * 125.f)
                fireEnemy(enemy, playerPosition);
            enemy.fireTimer += type.reload / fireRate;
        }
    }
}

void Combat::fireEnemyFan(sf::Vector2f muzzle, float angle, int pairs, float spacing, float speed, ShotKind kind, float radius)
{
    // Every naval and airborne gun adds another pair at 75, 180, 315, 480 ... world seconds.
    pairs += static_cast<int>(2.0 * (std::sqrt(1.0 + elapsed_ / 60.0) - 1.0));
    const float laneSpacing = pairs > 0 ? std::min(spacing, 1.35f / pairs) : 0.f;
    const float maturity = static_cast<float>(elapsed_ / (elapsed_ + 150.0));
    const float variation = maturity * (0.48f + 0.34f * pairs / (pairs + 3.f) + 0.18f * projectileDensity());
    std::uniform_real_distribution<float> jitter(-1.f, 1.f);
    for (int i = -pairs; i <= pairs; ++i)
    {
        if (i == 0)
        {
            // Keep the aimed round on the bearing shown by a naval gun's warning.
            shots_.push_back({kind, muzzle, velocityAt(angle, speed), radius, 0.f, 0.f});
            continue;
        }
        const float heading = angle + laneSpacing * i + jitter(flightRandom_) * variation * (0.08f + 0.28f * spacing);
        // Stagger neighbouring lanes in depth so dense fans still leave gaps to fly through.
        const float speedBand = i % 2 == 0 ? -0.14f : 0.14f;
        const auto velocity = velocityAt(heading, speed * (1.f + variation * (speedBand + 0.12f * jitter(flightRandom_))));
        Shot shot{kind, muzzle, velocity, radius, 0.f, 0.f};
        shot.enemyBend = velocity.perpendicular() * (0.82f * variation * jitter(flightRandom_));
        shots_.push_back(shot);
    }
}

void Combat::fireEnemy(Enemy& enemy, sf::Vector2f playerPosition)
{
    const auto muzzle = enemy.position + sf::Vector2f{enemy.velocity.x > 0.f ? enemy.radius : -enemy.radius, 0.f};
    const auto aim = playerPosition - muzzle;
    const float angle = std::atan2(aim.y, aim.x);
    const float opening = std::min(elapsed(), openingDuration);
    const float pressure = flightPressure(elapsed_);
    const float shotSpeed = 1.f + 0.22f * frontIntensity(level());
    const float speed = (130.f + 0.95f * opening + 104.f * pressure) * shotSpeed;
    const int stage = barrageStage(elapsed_) + static_cast<int>((level() - 1) / 2);
    const int extraPairs = (stage + static_cast<int>(enemy.volleys % 2)) / 2;
    switch (enemy.kind)
    {
        case EnemyKind::Scout:
            fireEnemyFan(muzzle, angle, (stage + static_cast<int>(enemy.volleys % 3)) / 3, 0.22f, speed, ShotKind::Enemy, 4.f);
            break;
        case EnemyKind::Striker:
            fireEnemyFan(muzzle, angle, 1 + extraPairs, 0.23f, speed * 0.92f, ShotKind::Enemy, 4.f);
            break;
        case EnemyKind::Gunship:
        {
            const float offset = enemy.volleys % 2 ? 0.125f : 0.f;
            fireEnemyFan(muzzle, angle + offset, 3 + extraPairs, 0.25f, speed * 0.77f, ShotKind::EnemyOrb, 5.f);
            break;
        }
        case EnemyKind::PatrolBoat:
        {
            const auto direction = enemy.turretDirection();
            const float shellSpeed = (190.f + 0.6f * opening + 70.f * pressure) * shotSpeed;
            fireEnemyFan(enemy.turretMuzzle(), std::atan2(direction.y, direction.x), (stage + 1) / 3, 0.26f,
                         shellSpeed, ShotKind::EnemyOrb, 5.f);
            break;
        }
        case EnemyKind::CarrierPart:
        case EnemyKind::CarrierHull:
        case EnemyKind::StageBossCore:
        case EnemyKind::StageBossMount:
        case EnemyKind::BattleshipTurret:
            return; // The deck batteries have their own aim and burst timers.
    }
    ++enemy.volleys;
}

void Combat::spawnPatrol()
{
    const float pace = (1.f + 0.7f * flightPressure(elapsed_)) * (1.f + 0.16f * frontIntensity(level()));
    if (level() == 9)
    {
        // Over the cloud sea, two low-flying scouts climb toward the middle in succession.
        const auto type = stats(EnemyKind::Scout);
        for (int i = 0; i < 2; ++i)
        {
            const float lane = 402.f + i * 46.f;
            enemies_.push_back({EnemyKind::Scout, {width + type.radius + 90.f * i, lane}, {-type.speed * pace, 0.f},
                                type.radius, 0.f, squadronHealth(type.health, level()), lane, 0.f, 0.85f + i * 0.45f, 0, 0.f});
            enemies_.back().flight = FlightPath::CrossLane;
        }
        return;
    }
    const auto type = stats(EnemyKind::PatrolBoat);
    const float lane = 506.f + 6.f * std::sin(elapsed_);
    const float speed = (type.speed + 15.f * std::sin(elapsed_ * 0.47f)) * pace;
    enemies_.push_back({EnemyKind::PatrolBoat, {width + 36.f, lane}, {-speed, 0.f},
                        type.radius, 0.f, squadronHealth(type.health, level()), lane, 0.f, 1.2f, 0, 0.f});
}

void Combat::updateBoat(Enemy& boat, float dt, sf::Vector2f playerPosition)
{
    const auto type = stats(EnemyKind::PatrolBoat);
    boat.position.x += boat.velocity.x * dt;
    boat.position.y = boat.centerY + type.weave * std::sin(boat.age * type.frequency);
    const auto aim = playerPosition - boat.turretPivot();
    if (boat.fireTimer > Enemy::boatAimWarning)
        boat.turretTarget = playerPosition;
    if (boat.position.x > width - 36.f || boat.position.x < 26.f)
        return;

    // The turret tracks the locked point during the warning, so a late dodge clears the shell.
    boat.fireTimer -= dt;
    if (boat.fireTimer > 0.f)
        return;
    if (aim.lengthSquared() >= 125.f * 125.f)
        fireEnemy(boat, playerPosition);
    boat.fireTimer += type.reload / ((1.f + 0.56f * flightPressure(elapsed_)) * (1.f + 0.28f * frontIntensity(level())));
}

void Combat::updateBattleship(float dt)
{
    if (!battleship_)
    {
        // Reserve the water for the first-night arrival, even if the first battleship fell early.
        if (carrierRaid_.phase == CarrierPhase::Waiting && elapsed_ >= CarrierRaid::arrival - 49.f)
            return;
        nextBoss_ -= dt;
        if (nextBoss_ > 0.f)
            return;

        battleship_.emplace();
        const auto type = stats(EnemyKind::BattleshipTurret);
        for (auto kind : {TurretKind::Cannon, TurretKind::Flak, TurretKind::Burst})
        {
            const auto mount = battleship_->mountPosition(kind);
            Enemy turret{EnemyKind::BattleshipTurret, mount, {}, type.radius, 0.f, type.health,
                         mount.y, 0.f, battery(kind).firstShot, 0, 0.f};
            turret.turretKind = kind;
            enemies_.push_back(turret);
        }
    }
    auto& ship = *battleship_;
    ship.hitFlash = std::max(0.f, ship.hitFlash - dt);
    if (ship.sinking)
    {
        ship.sinkAge += dt;
        if (ship.sinkAge < Battleship::sinkDuration)
            return;
    }
    else
    {
        ship.age += dt;
        const float station = 665.f + 65.f * std::sin(0.22f * ship.age);
        const float speed = ship.age < 34.f ? std::clamp((station - ship.position.x) * 0.8f, -85.f, 25.f) : -100.f;
        ship.position.x += speed * (1.f + 0.25f * flightPressure(elapsed_)) * dt;
        ship.position.y = 509.f + 1.7f * std::sin(1.8f * ship.age);
        if (ship.position.x > -130.f)
            return;
    }
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
        [](const Enemy& enemy) { return enemy.kind == EnemyKind::BattleshipTurret; }), enemies_.end());
    battleship_.reset();
    nextBoss_ = 55.f;
}

void Combat::updateBossTurret(Enemy& turret, float dt, sf::Vector2f playerPosition)
{
    const auto mount = battleship_->mountPosition(turret.turretKind);
    turret.velocity = (mount - turret.position) / dt;
    turret.position = mount;
    const auto gun = battery(turret.turretKind);
    const bool inBurst = turret.turretKind == TurretKind::Burst && turret.volleys % 3 != 0;
    if (!inBurst && turret.fireTimer > gun.warning)
        turret.turretTarget = playerPosition;
    if (mount.x < 30.f || mount.x > width - 30.f)
        return;

    turret.fireTimer -= dt;
    if (turret.fireTimer > 0.f)
        return;
    if ((playerPosition - mount).lengthSquared() < 95.f * 95.f)
    {
        turret.fireTimer = gun.warning + 0.1f;
        return;
    }

    const auto muzzle = turret.turretMuzzle();
    const auto direction = turret.turretDirection();
    const float angle = std::atan2(direction.y, direction.x);
    const float pressure = flightPressure(elapsed_);
    const float shellSpeed = 1.f + 0.22f * pressure;
    const int stage = barrageStage(elapsed_);
    switch (turret.turretKind)
    {
        case TurretKind::Cannon:
            fireEnemyFan(muzzle, angle, stage / 3, 0.28f, 178.f * shellSpeed, ShotKind::EnemyOrb, 7.f);
            break;
        case TurretKind::Flak:
        {
            const int extraPairs = (stage + static_cast<int>(turret.volleys % 2)) / 2;
            fireEnemyFan(muzzle, angle, 2 + extraPairs, 0.24f, 147.f * shellSpeed, ShotKind::Enemy, 4.f);
            break;
        }
        case TurretKind::Burst:
            fireEnemyFan(muzzle, angle, stage >= 4 ? 1 : 0, 0.20f, 265.f * shellSpeed, ShotKind::Enemy, 3.5f);
            break;
    }
    ++turret.volleys;
    const float reload = gun.reload / (1.f + 0.5f * pressure);
    turret.fireTimer += (turret.turretKind == TurretKind::Burst && turret.volleys % 3 != 0) ? 0.15f : reload;
}

void Combat::damageHull(int amount, sf::Vector2f impact, unsigned chainId, bool countedKill)
{
    if (!battleship_ || battleship_->sinking || amount <= 0)
        return;
    auto& ship = *battleship_;
    ship.health = std::max(0, ship.health - amount);
    if (ship.health == 0)
    {
        if (!countedKill)
            creditEnemyKill(0, chainId);
        sinkBattleship(chainId);
        if (!countedKill && chainId != 0)
            explosions_.back().comboKills = chains_.at(chainId).tally.kills;
    }
    else if (ship.hitFlash <= 0.f)
    {
        ship.hitFlash = Battleship::impactInterval;
        explode(impact, ExplosionKind::HullImpact);
    }
}

void Combat::sinkBattleship(unsigned chainId)
{
    auto& ship = *battleship_;
    ship.sinking = true;
    for (auto& enemy : enemies_)
        if (enemy.kind == EnemyKind::BattleshipTurret)
            enemy.health = 0;
    score_ += 1500;
    explode(ship.position + sf::Vector2f{-67.f, -9.f}, ExplosionKind::Enemy);
    explode(ship.position + sf::Vector2f{60.f, -12.f}, ExplosionKind::Enemy);
    explode(ship.position, ExplosionKind::Gunship, chainId);
    if (chainId == 0)
        explosions_.back().comboKills = 1;
}

float Combat::nextPickupDelay()
{
    if (weaponMode_ == WeaponMode::Arsenal && droneUpgrades_ < maxDroneUpgrades)
        return std::uniform_real_distribution<float>{8.f, 14.f}(pickupRandom_);
    // Space out successive upgrades as their effect compounds.
    return std::uniform_real_distribution<float>{5.f, 9.f}(pickupRandom_) * fireRateMultiplier_;
}

void Combat::updatePickup(float dt, sf::Vector2f playerPosition)
{
    if (!fireRatePickup_)
    {
        if (weaponMode_ == WeaponMode::Arsenal && droneUpgrades_ == maxDroneUpgrades &&
            effectiveFireRate() >= fullArsenalRate)
            return;
        nextPickup_ -= dt;
        if (nextPickup_ > 0.f)
            return;

        const float lane = std::uniform_real_distribution<float>{72.f, height - 72.f}(pickupRandom_);
        fireRatePickup_ = FireRatePickup{{width + pickupRadius, lane}, lane, 0.f};
    }

    auto& pickup = *fireRatePickup_;
    pickup.age += dt;
    pickup.position.x -= 145.f * dt;
    pickup.position.y = pickup.centerY + 20.f * std::sin(2.5f * pickup.age);

    // The whole fuselage can collect a pickup, including the nose and tail.
    const float dx = std::clamp(pickup.position.x - playerPosition.x, -36.f, 36.f);
    if (overlaps(playerPosition + sf::Vector2f{dx, 0.f}, 14.f, pickup.position, pickupRadius))
    {
        ++pickupsCollected_;
        fireRateMultiplier_ *= 1.5f;
        if (weaponMode_ == WeaponMode::Cannon)
            nextPulse_ /= 1.5f;
        if (weaponMode_ == WeaponMode::Arsenal && droneUpgrades_ < maxDroneUpgrades)
        {
            const float previousReload = droneReloads[droneUpgrades_];
            ++droneUpgrades_;
            for (auto& drone : drones_)
                drone.reload *= droneReloads[droneUpgrades_] / previousReload;
            if (droneUpgrades_ == secondDroneUpgrade)
                drones_.emplace_back();
        }
    }
    else if (pickup.position.x >= -pickupRadius)
        return;

    fireRatePickup_.reset();
    nextPickup_ = nextPickupDelay();
}

float Combat::effectiveFireRate() const
{
    // Long flights reach the laser even if the pilot misses every pickup.
    const float flightBonus = std::clamp((elapsed() - 45.f) * 0.6f, 0.f, 34.f);
    return cannonPattern(power()).roundsPerSecond() * fireRateMultiplier_ + flightBonus;
}

void Combat::updateWeapon(float dt, sf::Vector2f muzzle)
{
    const float rate = effectiveFireRate();
    if (weaponMode_ == WeaponMode::Cannon && rate >= missileThreshold)
    {
        weaponMode_ = WeaponMode::Missiles;
        weaponSince_ = elapsed_;
        nextPulse_ = 0.f;
    }
    else if (weaponMode_ == WeaponMode::Missiles && rate >= laserThreshold && elapsed_ - weaponSince_ >= 12.f)
    {
        weaponMode_ = WeaponMode::Laser;
        weaponSince_ = elapsed_;
        nextPulse_ = 0.f;
    }
    else if (weaponMode_ == WeaponMode::Laser)
    {
        const float laserDuration = std::clamp(55.f - 0.5f * (rate - laserThreshold), 22.f, 55.f);
        if (elapsed_ - weaponSince_ >= laserDuration)
        {
            weaponMode_ = WeaponMode::Arsenal;
            weaponSince_ = elapsed_;
            drones_.emplace_back();
            nextPickup_ = std::min(nextPickup_, nextPickupDelay());
            nextPulse_ = 0.f;
        }
    }

    nextPulse_ -= dt;
    if (nextPulse_ > 0.f)
        return;

    if (weaponMode_ == WeaponMode::Arsenal)
    {
        // Interleave both muzzles across the same reload that used to fire the entire salvo.
        do
        {
            if (salvoShotsLeft_ == 0)
            {
                ++volleysFired_;
                salvoShotsLeft_ = 7 + 2 * weaponRank_;
                const float strength = std::clamp((rate - laserThreshold) / (fullArsenalRate - laserThreshold), 0.f, 1.f);
                salvoShotSpacing_ = rankedReload(0.38f - 0.12f * strength, weaponRank_) / salvoShotsLeft_;
            }
            fireSalvoBolt(muzzle);
            nextEmitter_ = 1 - nextEmitter_;
            --salvoShotsLeft_;
            nextPulse_ += salvoShotSpacing_;
        } while (nextPulse_ <= 0.f);
        return;
    }

    ++volleysFired_;
    if (weaponMode_ == WeaponMode::Cannon)
    {
        fireVolley(muzzle);
    }
    else if (weaponMode_ == WeaponMode::Missiles)
    {
        fireMissileRack(muzzle);
        const float strength = std::clamp((rate - missileThreshold) / (laserThreshold - missileThreshold), 0.f, 1.f);
        nextPulse_ += rankedReload(0.35f - 0.13f * strength, weaponRank_);
    }
    else
    {
        fireLaser(muzzle);
        const float strength = std::clamp((rate - laserThreshold) / (fullLaserRate - laserThreshold), 0.f, 1.f);
        nextPulse_ += rankedReload(0.36f - 0.14f * strength, weaponRank_);
    }
}

float Combat::projectileDensity() const
{
    const float inFlight = static_cast<float>(shots_.size() + laserPulses_.size());
    return inFlight / (inFlight + 60.f);
}

float Combat::weaponScatter(float base) const
{
    return base + (1.f - base) * 0.18f * projectileDensity();
}

float Combat::weaponNoise()
{
    return std::uniform_real_distribution<float>(-1.f, 1.f)(weaponRandom_);
}

void Combat::fireMissileRack(sf::Vector2f muzzle)
{
    const unsigned count = 1 + (weaponRank_ + 1) / 2;
    const float alternatingSide = volleysFired_ % 2 ? 1.f : -1.f;
    const float scatter = weaponScatter(spreadStrength(weaponRank_));
    const float opening = 0.030f + 0.75f * scatter;
    for (unsigned i = 0; i < count; ++i)
    {
        const float lane = count == 1 ? alternatingSide : 2.f * i / (count - 1) - 1.f;
        const float angle = lane * opening + 0.46f * scatter * weaponNoise();
        const float speed = 310.f + 80.f * scatter * weaponNoise();
        Shot missile{ShotKind::Missile, muzzle + sf::Vector2f{0.f, lane * (4.f + 14.f * scatter)},
                     velocityAt(angle, speed), 5.f, 0.f, 0.f};
        missile.verticalAcceleration = 235.f * scatter * weaponNoise();
        missile.weaponRank = weaponRank_;
        shots_.push_back(missile);
    }
}

void Combat::fireLaser(sf::Vector2f muzzle)
{
    const unsigned rank = weaponRank_;
    sf::Vector2f aim{1.f, 0.f};
    const float scatter = weaponScatter(spreadStrength(rank));
    float opening = 0.045f + 0.70f * scatter;
    if (const auto* target = guidanceTarget())
    {
        const auto offset = target->position - muzzle;
        if (offset.lengthSquared() > 0.f)
        {
            aim = offset.normalized();
            // Under guidance even the outer beams sight the marked enemy.
            opening = std::min(opening, std::atan2(0.8f * target->radius, offset.length()));
        }
    }

    laserPulses_.clear();
    std::optional<sf::Vector2f> hullHit;
    std::array<std::optional<sf::Vector2f>, 11> bossHits{};
    const auto bossPartIndex = [](const Enemy& enemy) {
        if (enemy.stageBossPart())
            return std::size_t{7} + (enemy.kind == EnemyKind::StageBossCore ? 0 : 1 + enemy.bossMount);
        return enemy.kind == EnemyKind::BattleshipTurret ? static_cast<std::size_t>(enemy.turretKind) :
                                                         3 + static_cast<std::size_t>(enemy.carrierPart);
    };
    const auto emitRay = [&](sf::Vector2f direction)
    {
        float reach = width + height;
        if (direction.x != 0.f)
            reach = std::min(reach, ((direction.x > 0.f ? width : 0.f) - muzzle.x) / direction.x);
        if (direction.y != 0.f)
            reach = std::min(reach, ((direction.y > 0.f ? height : 0.f) - muzzle.y) / direction.y);
        if (reach <= 0.f)
            return;

        const auto hull = battleship_ ? battleship_->hullRayHit(muzzle, direction, reach, LaserPulse::radius)
                                      : std::nullopt;
        reach = hull.value_or(reach);
        const Enemy* blocker = nullptr;
        for (const auto& enemy : enemies_)
        {
            if (enemy.health <= 0 || !enemy.bossPart())
                continue;
            std::optional<float> hit;
            if (enemy.kind == EnemyKind::StageBossCore)
                hit = stageBoss_->hullRayHit(muzzle, direction, reach, LaserPulse::radius);
            else if (enemy.kind == EnemyKind::CarrierHull)
                hit = carrierRaid_.hullRayHit(muzzle, direction, reach, LaserPulse::radius);
            else
                hit = rayCircleHit(muzzle, direction, reach, enemy.position, enemy.radius + LaserPulse::radius);
            if (hit)
            {
                reach = *hit;
                blocker = &enemy;
            }
        }
        const auto end = muzzle + direction * reach;
        laserPulses_.push_back({muzzle, end, 0.f, rank});
        if (blocker)
            bossHits[bossPartIndex(*blocker)] = end;
        else if (hull)
            hullHit = end;
    };

    emitRay(aim);
    const unsigned pairs = (rank + 2) / 3;
    const float heading = std::atan2(aim.y, aim.x);
    for (unsigned lane = 1; lane <= pairs; ++lane)
    {
        // Scatter the side rays independently inside the cone allowed by the guidance sight.
        for (float side : {-1.f, 1.f})
        {
            const float nominal = static_cast<float>(lane) / pairs;
            const float randomLane = 0.5f + 0.5f * weaponNoise();
            const float angle = opening * (nominal + scatter * (randomLane - nominal));
            emitRay(velocityAt(heading + side * angle, 1.f));
        }
    }

    // Trace the whole fan first: a ship destroyed by the central ray still blocks the side rays of that burst.
    const int pulseDamage = 8 + rank / 2;
    const int healthBefore = bossHealth();
    for (auto& enemy : enemies_)
    {
        if (enemy.health <= 0)
            continue;
        if (enemy.bossPart())
        {
            if (const auto& hit = bossHits[bossPartIndex(enemy)])
                damage(enemy, pulseDamage, 0, *hit);
            continue;
        }
        if (enemy.position.x < 0.f || enemy.position.x > width)
            continue;
        for (const auto& ray : laserPulses_)
        {
            const auto segment = ray.end - ray.origin;
            const float lengthSquared = segment.lengthSquared();
            const float t = lengthSquared > 0.f
                ? std::clamp((enemy.position - ray.origin).dot(segment) / lengthSquared, 0.f, 1.f) : 0.f;
            if (overlaps(ray.origin + segment * t, LaserPulse::radius, enemy.position, enemy.radius))
            {
                damage(enemy, pulseDamage);
                break;
            }
        }
    }
    if (hullHit)
        damageHull(pulseDamage - (healthBefore - bossHealth()), *hullHit);
}

void Combat::fireVolley(sf::Vector2f muzzle)
{
    constexpr float maxDeviation = 32.f * pi / 180.f;
    const int level = power();
    const auto pattern = cannonPattern(level);
    if (burstRemaining_ == 0)
        burstRemaining_ = pattern.burst;

    // Relative to the starting cannon, count every round in a full burst and reload.
    const float relativeRate = fireRateMultiplier_ * pattern.roundsPerSecond() * singleReload;
    const float extraRate = relativeRate - 1.f + 0.35f * weaponRank_;
    const float intensity = weaponScatter(extraRate / (extraRate + 5.f));
    const float deviation = maxDeviation * intensity;
    const float scatter = weaponScatter(spreadStrength(weaponRank_));
    const float curveChance = 0.16f + 0.42f * intensity;
    const float curveAcceleration = 60.f + 430.f * intensity;
    std::uniform_real_distribution<float> jitter(-deviation, deviation);
    std::uniform_real_distribution<float> curveRoll(-1.f, 1.f);
    const int lanes = pattern.spread + static_cast<int>((weaponRank_ + 1) / 2);
    const float baseOpening = pattern.spread * (level >= 5 ? 0.18f : 0.12f);
    const float opening = baseOpening + (1.45f - deviation - baseOpening) * scatter;
    const float stepAngle = lanes > 0 ? opening / lanes : 0.f;
    for (int i = -lanes; i <= lanes; ++i)
    {
        const float angle = i * stepAngle + jitter(weaponRandom_);
        Shot shot{ShotKind::Player, muzzle, velocityAt(angle, 630.f + 110.f * scatter * weaponNoise()), 3.f, 0.f, 0.f};
        shot.weaponRank = weaponRank_;
        shot.trailPower = intensity;
        shot.sparkSeed = static_cast<unsigned>(std::abs(shot.velocity.x) * 1024.f);
        const float roll = curveRoll(weaponRandom_);
        if (std::abs(roll) < curveChance)
            shot.verticalAcceleration = std::copysign(curveAcceleration, roll);
        shots_.push_back(shot);
    }

    if (pattern.waves > 0 && burstRemaining_ == 1)
    {
        const unsigned pairs = 1 + weaponRank_ / 5;
        for (unsigned lane = 0; lane < pairs; ++lane)
        {
            for (float sign : {-1.f, 1.f})
            {
                const float sideAngle = 0.68f * scatter * lane / pairs;
                const auto velocity = velocityAt(sign * sideAngle + jitter(weaponRandom_), 545.f);
                const float phase = std::uniform_real_distribution<float>{0.f, 2.f * pi}(weaponRandom_);
                Shot shot{ShotKind::PlayerWave, muzzle, velocity, 3.5f, 0.f, sign, phase, velocity.y};
                shot.weaponRank = weaponRank_;
                shot.waveTempo = 1.f + 0.50f * scatter * weaponNoise();
                shots_.push_back(shot);
            }
        }
    }
    --burstRemaining_;
    const float reload = burstRemaining_ > 0 ? burstSpacing : pattern.reload;
    nextPulse_ += rankedReload(reload / fireRateMultiplier_, weaponRank_);
}

float Combat::arsenalPhase() const
{
    return static_cast<float>(std::fmod(0.5 * pi + 5.4 * (elapsed_ - weaponSince_), 2.0 * pi));
}

sf::Vector2f Combat::arsenalEmitterOffset(unsigned index) const
{
    const float phase = arsenalPhase();
    const float sign = index == 0 ? 1.f : -1.f;
    return {36.f + sign * 7.f * std::cos(phase), sign * emitterTravel * std::sin(phase)};
}

void Combat::fireSalvoBolt(sf::Vector2f muzzle)
{
    const auto origin = muzzle + arsenalEmitterOffset(nextEmitter_) + sf::Vector2f{4.f, 0.f};
    const float scatter = weaponScatter(spreadStrength(weaponRank_));
    const float speed = 690.f + (5.f + 108.f * scatter) * weaponNoise();
    Shot shot{ShotKind::Salvo, origin, {speed, 0.f}, 2.8f, 0.f, nextEmitter_ == 0 ? 1.f : -1.f};
    shot.wavePhase = arsenalPhase();
    shot.weaponRank = weaponRank_;
    shot.waveTempo = 1.f + 0.38f * scatter * weaponNoise();
    shot.waveDriftY = (4.f + 90.f * scatter) * weaponNoise();
    shots_.push_back(shot);
}

unsigned Combat::droneShotsFired() const
{
    unsigned total = 0;
    for (const auto& drone : drones_)
        total += drone.shotsFired;
    return total;
}

const Enemy* Combat::droneTarget(sf::Vector2f position, const Enemy* claimed) const
{
    if (guidanceActive())
        return guidanceTarget(claimed);
    const Enemy* closest = nullptr;
    float closestDistance = std::numeric_limits<float>::infinity();
    for (const auto& enemy : enemies_)
    {
        if (&enemy == claimed || !isVisibleTarget(enemy))
            continue;
        const float distance = (enemy.position - position).lengthSquared();
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closest = &enemy;
        }
    }
    return closest;
}

void Combat::updateDrones(float dt, sf::Vector2f playerPosition)
{
    const float angle = static_cast<float>(std::fmod(2.7 * (elapsed_ - weaponSince_), 2.0 * pi));
    // Compress the orbit near a screen edge so the drones stay opposite each other.
    constexpr float margin = 12.f;
    const sf::Vector2f center{std::clamp(playerPosition.x, margin, width - margin),
                              std::clamp(playerPosition.y, margin, height - margin)};
    const float radiusX = std::min({50.f, center.x - margin, width - margin - center.x});
    const float radiusY = std::min({32.f, center.y - margin, height - margin - center.y});
    const sf::Vector2f offset{radiusX * std::cos(angle), radiusY * std::sin(angle)};
    const Enemy* claimed = nullptr;
    for (unsigned i = 0; i < drones_.size(); ++i)
    {
        auto& drone = drones_[i];
        drone.position = center + (i == 0 ? offset : -offset);
        drone.muzzleFlash = std::max(0.f, drone.muzzleFlash - dt / 0.08f);
        drone.reload = std::max(0.f, drone.reload - dt);

        const auto* target = droneTarget(drone.position, claimed);
        claimed = target;
        drone.tracking = target && target->position != drone.position;
        if (!drone.tracking)
            continue;
        drone.direction = (target->position - drone.position).normalized();
        if (drone.reload > 0.f)
            continue;
        const unsigned count = 1 + (weaponRank_ + 2) / 4;
        const auto side = drone.direction.perpendicular();
        const auto muzzle = drone.muzzle();
        // Each bolt sights a different point inside the claimed target's silhouette.
        const float scatter = weaponScatter(spreadStrength(weaponRank_));
        const float aperture = target->radius * (0.45f + 0.52f * scatter);
        for (unsigned bolt = 0; bolt < count; ++bolt)
        {
            const float lane = count == 1 ? 0.f : 2.f * bolt / (count - 1) - 1.f;
            const auto start = muzzle + side * (lane * (2.f + 8.f * scatter));
            const float aimLane = lane + scatter * (weaponNoise() - lane);
            const auto offsetToAim = target->position + side * (aimLane * aperture) - start;
            const auto direction = offsetToAim.lengthSquared() > 0.f ? offsetToAim.normalized() : drone.direction;
            Shot shot{ShotKind::Drone, start, direction * (570.f + 105.f * scatter * weaponNoise()), 2.4f, 0.f, 0.f};
            shot.droneIndex = i;
            shot.weaponRank = weaponRank_;
            shots_.push_back(shot);
        }
        drone.shotsFired += count;
        drone.muzzleFlash = 1.f;
        drone.reload = rankedReload(droneReloads[droneUpgrades_], weaponRank_);
    }
}

void Combat::updateShots(float dt)
{
    const auto* primary = guidanceTarget();
    const auto* secondary = primary && drones_.size() > 1 ? guidanceTarget(primary) : nullptr;
    for (auto& shot : shots_)
    {
        if (shot.kind == ShotKind::Missile || shot.kind == ShotKind::Braid)
            recordTrail(shot);
        if (shot.kind == ShotKind::Missile)
        {
            const float speed = shot.velocity.length();
            shot.velocity *= std::min(630.f, speed + 760.f * dt) / speed;
        }
        // Leave a short run out of the muzzle so the spread and wave patterns remain visible.
        const auto* target = shot.droneIndex == 1 ? secondary : primary;
        const bool steering = target && shot.isFriendly() && shot.age >= 0.05f;
        if (steering)
            steerShot(shot, *target, dt);
        else if (shot.kind == ShotKind::Player && !shot.trailHistory.empty())
            recordTrail(shot);
        shot.age += dt;
        if (shot.kind == ShotKind::Salvo && shot.lastGuidedAge < 0.f)
        {
            // After guidance ends a dart keeps its last heading, without snapping back into the wave.
            const float before = salvoOffset(shot.age - dt, shot);
            const float after = salvoOffset(shot.age, shot);
            shot.velocity.y = shot.waveDriftY + shot.waveSign * (after - before) / dt;
        }
        else if (!steering && shot.kind == ShotKind::PlayerWave)
        {
            const float before = waveOffset((shot.age - dt) * shot.waveTempo, shot.wavePhase);
            const float after = waveOffset(shot.age * shot.waveTempo, shot.wavePhase);
            const float swing = shot.waveSign * (after - before) / dt;
            if (shot.lastGuidedAge < 0.f)
                shot.velocity.y = shot.waveDriftY + swing;
            else
            {
                // Reintroduce the zigzag around the course held when guidance ended.
                const sf::Vector2f drift{shot.waveDriftX, shot.waveDriftY};
                const float blend = std::min(1.f, (shot.age - shot.lastGuidedAge) / 0.14f);
                shot.velocity = drift + drift.perpendicular().normalized() * (swing * blend);
            }
        }
        shot.position += shot.velocity * dt;
        if (shot.enemyBend != sf::Vector2f{} && shot.age - dt < Shot::enemyBendDuration)
        {
            const float bendTime = std::clamp(Shot::enemyBendDuration - (shot.age - dt), 0.f, dt);
            shot.position += shot.enemyBend * (bendTime * (dt - 0.5f * bendTime));
            shot.velocity += shot.enemyBend * bendTime;
        }
        if (!steering && shot.verticalAcceleration != 0.f)
        {
            const float changeInSpeedY = shot.verticalAcceleration * dt;
            shot.position.y += 0.5f * changeInSpeedY * dt;
            shot.velocity.y += changeInSpeedY;
        }
    }
}

void Combat::explode(sf::Vector2f position, ExplosionKind kind, unsigned chainId)
{
    const float duration = kind == ExplosionKind::HullImpact ? 0.23f :
                           kind == ExplosionKind::Wave ? 0.27f : kind == ExplosionKind::Player ? 0.75f : 0.45f;
    explosions_.push_back({position, 0.f, duration, kind, ++explosionSerial_});
    if (kind == ExplosionKind::Gunship)
    {
        auto& wave = explosions_.back();
        wave.chainId = chainId == 0 ? wave.seed : chainId;
        ++chains_[wave.chainId].liveWaves;
    }
}

void Combat::explodeEnemy(const Enemy& enemy, unsigned chainId)
{
    explode(enemy.position, enemy.kind == EnemyKind::Gunship ? ExplosionKind::Gunship : ExplosionKind::Enemy, chainId);
    if (chainId != 0)
        explosions_.back().comboKills = chains_.at(chainId).tally.kills;
    else if (enemy.kind == EnemyKind::Gunship)
        explosions_.back().comboKills = 1;
}

void Combat::hitPlayer(sf::Vector2f position)
{
    if (invulnerable() || finished() || levelCleared())
        return;
    --lives_;
    chargedTip_ = 0;
    tipTimeLeft_ = 0.f;
    braidTimeLeft_ = 0.f;
    if (gameOver())
    {
        salvoShotsLeft_ = 0;
        recoveryKills_ = 0;
        recoveryBlinkPhase_.reset();
        lastLifeRecoveryTime_ = -std::numeric_limits<float>::infinity();
    }
    grace_ = 2.f;
    explode(position, ExplosionKind::Player);
}

float Combat::lifeRecoveryFlash() const
{
    return std::max(0.f, 1.f - (elapsed() - lastLifeRecoveryTime_) / lifeNoticeDuration);
}

float Combat::lifeRecoveryGlow() const
{
    return std::max(0.f, 1.f - (elapsed() - lastLifeRecoveryTime_) / lifeGlowDuration);
}

void Combat::creditRecoveryKill()
{
    if (lives_ == maxLives || gameOver())
        return;
    ++recoveryKills_;
    const int remaining = killsPerLife - recoveryKills_;
    if (remaining == static_cast<int>(recoveryBlinkPeriods.size()))
        recoveryBlinkPhase_ = 0.f;
    else if (oneKillFromLife())
        ++lifeRecoveryWarnings_;
    else if (remaining == 0)
    {
        ++lives_;
        ++lifeRecoveries_;
        recoveryKills_ = 0;
        recoveryBlinkPhase_.reset();
        lastLifeRecoveryTime_ = elapsed_;
    }
}

void Combat::creditEnemyKill(int points, unsigned chainId)
{
    if (enemiesDestroyed_ < std::numeric_limits<std::uint64_t>::max())
    {
        ++enemiesDestroyed_;
        if (enemiesDestroyed_ == nextWeaponRankKills())
            ++weaponRank_;
    }
    score_ += points;
    creditRecoveryKill();
    if (chainId == 0)
        return;
    auto& tally = chains_.at(chainId).tally;
    const int extra = 100 * tally.kills;
    ++tally.kills;
    tally.bonus += extra;
    score_ += extra;
    lastCombo_ = tally;
    comboUntil_ = elapsed_ + 2.f;
    bestCombo_ = std::max(bestCombo_, tally.kills);
    if (tally.kills == 5 || tally.kills == 10)
        milestones_.push_back(tally.kills);
}

void Combat::damage(Enemy& enemy, int amount, unsigned chainId, std::optional<sf::Vector2f> impact)
{
    if (enemy.health <= 0)
        return;
    if (enemy.stageBossPart())
    {
        damageStageBoss(enemy, amount, impact.value_or(enemy.position), chainId);
        return;
    }
    if (enemy.kind == EnemyKind::CarrierPart || enemy.kind == EnemyKind::CarrierHull)
    {
        damageCarrier(enemy, amount, impact.value_or(enemy.position), chainId);
        return;
    }
    enemy.hitFlash = 0.09f;
    const int dealt = std::min(enemy.health, amount);
    enemy.health -= dealt;
    if (enemy.health == 0)
    {
        creditEnemyKill(stats(enemy.kind).points, chainId);
        explodeEnemy(enemy, chainId);
    }
    if (enemy.kind == EnemyKind::BattleshipTurret)
        damageHull(dealt, impact.value_or(enemy.position), chainId, enemy.health == 0);
}

void Combat::hitEnemies(Shot& shot)
{
    const auto touches = [&](const Enemy& enemy, float radius) {
        if (enemy.solidHull())
            return enemy.health > 0 && (hullContact(enemy, shot.position) - shot.position).lengthSquared() <= radius * radius;
        return enemy.health > 0 && enemy.position.x >= 0.f && enemy.position.x <= width &&
               overlaps(shot.position, radius, enemy.position, enemy.radius);
    };
    const auto hit = std::find_if(enemies_.begin(), enemies_.end(),
                                  [&](const Enemy& enemy) { return touches(enemy, shot.radius); });
    const bool onScreen = shot.position.x >= 0.f && shot.position.x <= width &&
                          shot.position.y >= 0.f && shot.position.y <= height;
    const bool hullHit = battleship_ && onScreen && battleship_->touchesHull(shot.position, shot.radius);
    const bool timedOut = shot.kind == ShotKind::PlayerWave && shot.age >= 0.95f;
    if (hit == enemies_.end() && !hullHit && !timedOut)
        return;
    shot.spent = true;
    if (shot.kind != ShotKind::PlayerWave && shot.kind != ShotKind::Missile)
    {
        const int directDamage = shot.directDamage();
        if (hit != enemies_.end())
            damage(*hit, directDamage, 0, shot.position);
        else if (hullHit)
            damageHull(directDamage, shot.position);
        return;
    }
    if (!onScreen && hit == enemies_.end())
        return;
    explode(shot.position, ExplosionKind::Wave);
    const int healthBefore = bossHealth();
    for (auto enemy = enemies_.begin(); enemy != enemies_.end(); ++enemy)
        if (touches(*enemy, waveBlastRadius))
        {
            const auto impact = hullContact(*enemy, shot.position);
            damage(*enemy, shot.kind == ShotKind::Missile && enemy == hit ? 5 : 2, 0, impact);
        }
    if (battleship_ && battleship_->touchesHull(shot.position, waveBlastRadius))
    {
        const int structuralDamage = shot.kind == ShotKind::Missile && hullHit && hit == enemies_.end() ? 5 : 2;
        // Nearby turrets already transferred their share of this blast to the ship.
        damageHull(structuralDamage - (healthBefore - bossHealth()), battleship_->nearestHullPoint(shot.position));
    }
}

void Combat::sweepGunshipShockwaves()
{
    constexpr float coreRadius = 90.f;
    // New waves can reallocate the vector. Process their initial pulse in this same sweep.
    for (std::size_t i = 0; i < explosions_.size(); ++i)
    {
        if (explosions_[i].kind != ExplosionKind::Gunship)
            continue;
        const Explosion wave = explosions_[i];
        const float front = wave.shockwaveRadius();
        explosions_[i].sweptRadius = front;

        const auto touchesFront = [&](sf::Vector2f position, float radius) {
            const float distanceSquared = (position - wave.position).lengthSquared();
            const float inner = std::max(0.f, wave.sweptRadius - radius);
            const float outer = front + radius;
            return distanceSquared >= inner * inner && distanceSquared <= outer * outer;
        };
        for (auto& shot : shots_)
            if (!shot.isFriendly() && touchesFront(shot.position, shot.radius))
                shot.spent = true;

        const int healthBefore = bossHealth();
        for (auto& enemy : enemies_)
        {
            const bool solidHull = enemy.solidHull();
            const auto point = hullContact(enemy, wave.position);
            if (enemy.health <= 0 || enemy.position.x < 0.f || enemy.position.x > width ||
                !touchesFront(point, solidHull ? 0.f : enemy.radius))
                continue;
            auto& hits = enemy.shockwavesHit;
            if (std::find(hits.begin(), hits.end(), wave.seed) != hits.end())
                continue;
            const bool inCore = overlaps(wave.position, coreRadius, point, solidHull ? 0.f : enemy.radius);
            // A close shockwave can still set off the reinforced gunships in later sectors.
            const int coreDamage = enemy.kind == EnemyKind::Gunship ? enemy.maxHealth() : stats(EnemyKind::Gunship).health;
            damage(enemy, inCore ? coreDamage : 3, wave.chainId, point);
            if (enemy.health > 0)
                hits.push_back(wave.seed);
        }
        if (battleship_ && !battleship_->sinking && !wave.hitBattleship)
        {
            const auto impact = battleship_->nearestHullPoint(wave.position);
            if (impact.x >= 0.f && impact.x <= width && touchesFront(impact, 0.f))
            {
                explosions_[i].hitBattleship = true;
                const int amount = overlaps(wave.position, coreRadius, impact, 0.f) ? stats(EnemyKind::Gunship).health : 3;
                damageHull(amount - (healthBefore - bossHealth()), impact, wave.chainId);
            }
        }
    }
}

void Combat::collide(sf::Vector2f playerPosition, sf::Vector2f previousMuzzle, float dt)
{
    for (auto& shot : shots_)
        if (shot.isFriendly())
            hitEnemies(shot);

    sweepGunshipShockwaves();
    for (auto& shot : shots_)
    {
        if (shot.isFriendly() || shot.spent)
            continue;
        if (weaponMode_ == WeaponMode::Arsenal)
        {
            const auto from = shot.enemyPastPosition(dt) - previousMuzzle;
            if (const auto hit = shieldImpactY(from, shot.position - arsenalMuzzle_, shot.radius))
            {
                shot.spent = true;
                arsenalShieldHitY_ = *hit;
                arsenalShieldFlash_ = 1.f;
                catchShieldShot(*hit);
                continue;
            }
        }
        if (overlaps(shot.position, shot.radius, playerPosition, playerHitRadius))
        {
            shot.spent = true;
            hitPlayer(playerPosition);
        }
    }
    if (battleship_ && battleship_->touchesHull(playerPosition, playerHitRadius))
        hitPlayer(playerPosition);
    for (auto& enemy : enemies_)
    {
        const bool solid = enemy.solidHull();
        const bool collision = solid
            ? (hullContact(enemy, playerPosition) - playerPosition).lengthSquared() <= playerHitRadius * playerHitRadius
            : overlaps(enemy.position, enemy.radius, playerPosition, playerHitRadius);
        if (enemy.health > 0 && !invulnerable() && collision)
        {
            if (!enemy.bossPart())
            {
                enemy.health = 0;
                explodeEnemy(enemy);
            }
            hitPlayer(playerPosition);
        }
    }
    const bool guidingSalvo = weaponMode_ == WeaponMode::Arsenal && guidanceTarget();
    shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [this, guidingSalvo](const Shot& shot) {
        // At the right edge an emitter can launch offscreen. Give a newly guided dart room to turn back.
        float margin = 28.f;
        if (guidingSalvo && shot.age < 0.75f)
        {
            if (shot.kind == ShotKind::Salvo) margin = 176.f;
            if (shot.kind == ShotKind::Braid) margin = 230.f;
        }
        const bool remove = shot.spent || shot.position.x < -margin || shot.position.x > width + margin ||
                            shot.position.y < -margin || shot.position.y > height + margin;
        if ((remove || gameOver()) && shot.hasTrail())
            fadingTrails_.push_back({shot, 0.f});
        return remove;
    }), shots_.end());
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& enemy) {
        const bool leftField = enemy.velocity.x <= 0.f ? enemy.position.x < -enemy.radius
                                                      : enemy.position.x > width + enemy.radius;
        return enemy.health <= 0 || (enemy.kind != EnemyKind::BattleshipTurret && !enemy.stageBossPart() && leftField);
    }), enemies_.end());
}
