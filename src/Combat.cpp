#include "Combat.hpp"

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
constexpr float salvoOpeningTime = 0.24f;
constexpr float burstSpacing = 0.105f;
constexpr float singleReload = 0.23f;

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
    }
    return {};
}

void moveAircraft(Enemy& enemy, const EnemyStats& type, float dt, sf::Vector2f playerPosition)
{
    enemy.position.x += enemy.velocity.x * dt;
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

struct HullSection
{
    float left, top, right, bottom;
};

// Solid decks, bridge, funnel and mast, relative to the waterline of the hull sprite.
constexpr std::array<HullSection, 12> hullSections{{
    {-127.f, -8.f, 124.f, 2.f}, {-119.f, 2.f, 124.f, 10.f}, {-108.f, 10.f, 119.f, 16.f},
    {-95.f, 16.f, 107.f, 20.f}, {-62.f, -21.f, 112.f, -8.f}, {-39.f, -32.f, 100.f, -21.f},
    {-24.f, -58.f, 20.f, -32.f}, {30.f, -54.f, 46.f, -32.f}, {49.f, -43.f, 61.f, -32.f},
    {53.f, -83.f, 56.f, -43.f}, {44.f, -74.f, 64.f, -71.f}, {42.f, -65.f, 67.f, -62.f}
}};

bool clipRayAxis(float origin, float direction, float low, float high, float& enter, float& leave)
{
    if (direction == 0.f)
        return origin >= low && origin <= high;
    const float a = (low - origin) / direction;
    const float b = (high - origin) / direction;
    enter = std::max(enter, std::min(a, b));
    leave = std::min(leave, std::max(a, b));
    return enter <= leave;
}

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
    if (shot.kind == ShotKind::PlayerWave)
    {
        shot.lastGuidedAge = shot.age + dt;
        shot.waveDriftX = shot.velocity.x;
        shot.waveDriftY = shot.velocity.y;
    }
}
}

int Enemy::maxHealth() const
{
    return stats(kind).health;
}

sf::Vector2f Battleship::mountPosition(TurretKind kind) const
{
    return position + battery(kind).mount;
}

sf::Vector2f Battleship::nearestHullPoint(sf::Vector2f point) const
{
    const auto local = point - position;
    sf::Vector2f nearest{};
    float best = std::numeric_limits<float>::infinity();
    for (const auto& plate : hullSections)
    {
        const sf::Vector2f candidate{std::clamp(local.x, plate.left, plate.right),
                                     std::clamp(local.y, plate.top, plate.bottom)};
        const float distance = (local - candidate).lengthSquared();
        if (distance < best)
        {
            nearest = candidate;
            best = distance;
        }
    }
    return position + nearest;
}

bool Battleship::touchesHull(sf::Vector2f point, float radius) const
{
    return !sinking && (point - nearestHullPoint(point)).lengthSquared() <= radius * radius;
}

std::optional<float> Battleship::hullRayHit(sf::Vector2f origin, sf::Vector2f direction, float reach, float radius) const
{
    if (sinking)
        return {};
    const auto local = origin - position;
    std::optional<float> nearest;
    for (const auto& plate : hullSections)
    {
        float enter = 0.f, leave = nearest.value_or(reach);
        if (clipRayAxis(local.x, direction.x, plate.left - radius, plate.right + radius, enter, leave) &&
            clipRayAxis(local.y, direction.y, plate.top - radius, plate.bottom + radius, enter, leave))
            nearest = enter;
    }
    return nearest;
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
    precision_.held = precisionHeld;
    guidance_.held = guidanceHeld;
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
        if (!best || enemy.maxHealth() > best->maxHealth())
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
    if (!std::isfinite(dt) || dt <= 0.f)
        return;

    const bool hadMilestone = !milestones_.empty();
    // Substeps keep small targets hittable even if a frame takes longer.
    while (dt > 0.f)
    {
        const float realSlice = std::min(dt, maxStep);
        const float slice = realSlice * timeScale();
        if (laserPulse_)
        {
            laserPulse_->age += slice;
            if (laserPulse_->age >= LaserPulse::duration)
                laserPulse_.reset();
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
            precision_.advance(realSlice, elapsed_, 4.f, 8.f);
            guidance_.advance(realSlice, elapsed_, 5.f, 10.f);
        }
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
        if (gameOver() && explosions_.empty() && fadingTrails_.empty() && !(battleship_ && battleship_->sinking))
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
    distance_ += dt * (105.f + std::min(55.f, elapsed_ * 0.5f));
    grace_ = std::max(0.f, grace_ - dt);
    updateBattleship(dt);
    // Keep a few aerial escorts during the encounter and give the battleship the water lane.
    nextFormation_ -= battleship_ ? 0.35f * dt : dt;
    if (nextFormation_ <= 0.f)
    {
        spawnFormation();
        nextFormation_ += std::max(1.55f, 2.95f - elapsed_ * 0.022f);
    }
    if (!battleship_)
    {
        nextBoat_ -= dt;
        if (nextBoat_ <= 0.f)
        {
            spawnBoat();
            nextBoat_ += 13.5f - std::min(4.f, elapsed_ * 0.055f);
        }
    }
    updatePickup(dt, playerPosition);
    updateEnemies(dt, playerPosition);
    updateWeapon(dt, playerPosition + muzzleOffset);
    updateShots(dt);
    collide(playerPosition);
    if (weaponMode_ == WeaponMode::Arsenal && !gameOver())
        updateDrones(dt, playerPosition);
}

void Combat::spawnFormation()
{
    // Alternating entry lanes and silhouettes leave readable routes through each wave.
    constexpr std::array<float, 8> lanes{270.f, 144.f, 375.f, 190.f, 348.f, 230.f, 397.f, 150.f};
    const float lane = lanes[formation_ % lanes.size()];
    const unsigned pattern = formation_ % 4;
    const float mirror = (formation_ / 4) % 2 == 0 ? 1.f : -1.f;
    auto add = [&](EnemyKind kind, float delayX, float offsetY, float phase,
                   FlightPath flight = FlightPath::Weave, bool rear = false) {
        const auto type = stats(kind);
        const float center = std::clamp(lane + offsetY, 78.f, height - 78.f);
        const float x = rear ? -type.radius - delayX : width + type.radius + delayX;
        const float y = flight == FlightPath::Weave ? center + type.weave * std::sin(phase) : center;
        // The heavy convoy holds formation so its escorts stay within shockwave range.
        const float speedScale = pattern == 3 ? 1.f : std::uniform_real_distribution<float>{0.82f, 1.22f}(flightRandom_);
        const float speed = flight == FlightPath::Pursuit ? 181.f : type.speed;
        enemies_.push_back({kind, {x, y}, {(rear ? 1.f : -1.f) * speed * speedScale, 0.f}, type.radius, 0.f, type.health,
                            center, phase, 0.72f + delayX * 0.002f, 0, 0.f});
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
                            escort.velocity.x = -stats(EnemyKind::Gunship).speed;
                            escort.fireTimer = 3.2f + 0.45f * (column * 2 + row);
                        }
                    }
                }
            }
            else
                add(EnemyKind::Scout, 85.f, 82.f, pi);
            break;
    }
    ++formation_;
}

void Combat::updateEnemies(float dt, sf::Vector2f playerPosition)
{
    for (auto& enemy : enemies_)
    {
        enemy.age += dt;
        enemy.hitFlash = std::max(0.f, enemy.hitFlash - dt);
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
            enemy.fireTimer += type.reload / (1.f + std::min(0.35f, elapsed_ * 0.004f));
        }
    }
}

void Combat::fireEnemy(Enemy& enemy, sf::Vector2f playerPosition)
{
    const auto muzzle = enemy.position + sf::Vector2f{enemy.velocity.x > 0.f ? enemy.radius : -enemy.radius, 0.f};
    const auto aim = playerPosition - muzzle;
    const float angle = std::atan2(aim.y, aim.x);
    const float speed = 130.f + std::min(74.f, elapsed_ * 0.95f);
    auto shoot = [&](float heading, float velocity, ShotKind kind) {
        shots_.push_back({kind, muzzle, velocityAt(heading, velocity),
                          kind == ShotKind::EnemyOrb ? 5.f : 4.f, 0.f, 0.f});
    };
    switch (enemy.kind)
    {
        case EnemyKind::Scout:
            shoot(angle, speed, ShotKind::Enemy);
            break;
        case EnemyKind::Striker:
            for (int i = -1; i <= 1; ++i)
                shoot(angle + 0.23f * i, speed * 0.92f, ShotKind::Enemy);
            break;
        case EnemyKind::Gunship:
        {
            // Offset alternating fans by half a gap rather than sealing the lanes.
            const float offset = enemy.volleys % 2 ? 0.125f : 0.f;
            for (int i = -3; i <= 3; ++i)
                shoot(angle + 0.25f * i + offset, speed * 0.77f, ShotKind::EnemyOrb);
            break;
        }
        case EnemyKind::PatrolBoat:
        {
            const float shellSpeed = 190.f + std::min(48.f, elapsed_ * 0.6f);
            shots_.push_back({ShotKind::EnemyOrb, enemy.turretMuzzle(), enemy.turretDirection() * shellSpeed,
                              5.f, 0.f, 0.f});
            break;
        }
        case EnemyKind::BattleshipTurret:
            return; // The deck batteries have their own aim and burst timers.
    }
    ++enemy.volleys;
}

void Combat::spawnBoat()
{
    const auto type = stats(EnemyKind::PatrolBoat);
    const float lane = 506.f + 6.f * std::sin(elapsed_);
    const float speed = type.speed + 15.f * std::sin(elapsed_ * 0.47f);
    enemies_.push_back({EnemyKind::PatrolBoat, {width + 36.f, lane}, {-speed, 0.f},
                        type.radius, 0.f, type.health, lane, 0.f, 1.2f, 0, 0.f});
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
    boat.fireTimer += type.reload;
}

void Combat::updateBattleship(float dt)
{
    if (!battleship_)
    {
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
        ship.position.x += speed * dt;
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
    switch (turret.turretKind)
    {
        case TurretKind::Cannon:
            shots_.push_back({ShotKind::EnemyOrb, muzzle, direction * 178.f, 7.f, 0.f, 0.f});
            break;
        case TurretKind::Flak:
        {
            const float aim = std::atan2(direction.y, direction.x);
            for (int i = -2; i <= 2; ++i)
                shots_.push_back({ShotKind::Enemy, muzzle, velocityAt(aim + 0.24f * i, 147.f), 4.f, 0.f, 0.f});
            break;
        }
        case TurretKind::Burst:
            shots_.push_back({ShotKind::Enemy, muzzle, direction * 265.f, 3.5f, 0.f, 0.f});
            break;
    }
    ++turret.volleys;
    turret.fireTimer += (turret.turretKind == TurretKind::Burst && turret.volleys % 3 != 0) ? 0.15f : gun.reload;
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
    const float flightBonus = std::clamp((elapsed_ - 45.f) * 0.6f, 0.f, 34.f);
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

    ++volleysFired_;
    if (weaponMode_ == WeaponMode::Cannon)
    {
        fireVolley(muzzle);
    }
    else if (weaponMode_ == WeaponMode::Missiles)
    {
        const float side = volleysFired_ % 2 ? 1.f : -1.f;
        shots_.push_back({ShotKind::Missile, muzzle + sf::Vector2f{0.f, side * 4.f},
                          velocityAt(side * 0.030f, 310.f), 5.f, 0.f, 0.f});
        const float strength = std::clamp((rate - missileThreshold) / (laserThreshold - missileThreshold), 0.f, 1.f);
        nextPulse_ += 0.35f - 0.13f * strength;
    }
    else if (weaponMode_ == WeaponMode::Laser)
    {
        fireLaser(muzzle);
        const float strength = std::clamp((rate - laserThreshold) / (fullLaserRate - laserThreshold), 0.f, 1.f);
        nextPulse_ += 0.36f - 0.14f * strength;
    }
    else
    {
        fireSalvo(muzzle);
        const float strength = std::clamp((rate - laserThreshold) / (fullArsenalRate - laserThreshold), 0.f, 1.f);
        nextPulse_ += 0.38f - 0.12f * strength;
    }
}

void Combat::fireLaser(sf::Vector2f muzzle)
{
    sf::Vector2f direction{1.f, 0.f};
    if (const auto* target = guidanceTarget())
    {
        const auto aim = target->position - muzzle;
        if (aim.lengthSquared() > 0.f)
            direction = aim.normalized();
    }
    float reach = width + height;
    if (direction.x != 0.f)
        reach = std::min(reach, ((direction.x > 0.f ? width : 0.f) - muzzle.x) / direction.x);
    if (direction.y != 0.f)
        reach = std::min(reach, ((direction.y > 0.f ? height : 0.f) - muzzle.y) / direction.y);
    if (reach <= 0.f)
        return;
    const auto hullContact = battleship_ ? battleship_->hullRayHit(muzzle, direction, reach, LaserPulse::radius)
                                         : std::nullopt;
    reach = hullContact.value_or(reach);
    Enemy* turretHit = nullptr;
    for (auto& enemy : enemies_)
    {
        if (enemy.kind != EnemyKind::BattleshipTurret || enemy.health <= 0)
            continue;
        const auto contact = rayCircleHit(muzzle, direction, reach, enemy.position, enemy.radius + LaserPulse::radius);
        if (contact)
        {
            reach = *contact;
            turretHit = &enemy;
        }
    }
    laserPulse_ = LaserPulse{muzzle, muzzle + direction * reach};
    for (auto& enemy : enemies_)
    {
        if (enemy.health <= 0 || enemy.kind == EnemyKind::BattleshipTurret ||
            enemy.position.x < 0.f || enemy.position.x > width)
            continue;
        const float along = std::clamp((enemy.position - muzzle).dot(direction), 0.f, reach);
        if (overlaps(muzzle + direction * along, LaserPulse::radius, enemy.position, enemy.radius))
            damage(enemy, 8);
    }
    if (turretHit)
        damage(*turretHit, 8, 0, laserPulse_->end);
    else if (hullContact)
        damageHull(8, laserPulse_->end);
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
    const float extraRate = relativeRate - 1.f;
    const float intensity = extraRate / (extraRate + 5.f);
    const float deviation = maxDeviation * intensity;
    const float curveChance = 0.16f + 0.42f * intensity;
    const float curveAcceleration = 60.f + 430.f * intensity;
    std::uniform_real_distribution<float> jitter(-deviation, deviation);
    std::uniform_real_distribution<float> curveRoll(-1.f, 1.f);
    for (int i = -pattern.spread; i <= pattern.spread; ++i)
    {
        const float angle = i * (level >= 5 ? 0.18f : 0.12f) + jitter(weaponRandom_);
        Shot shot{ShotKind::Player, muzzle, velocityAt(angle, 630.f), 3.f, 0.f, 0.f};
        shot.trailPower = intensity;
        shot.sparkSeed = static_cast<unsigned>(std::abs(shot.velocity.x) * 1024.f);
        const float roll = curveRoll(weaponRandom_);
        if (std::abs(roll) < curveChance)
            shot.verticalAcceleration = std::copysign(curveAcceleration, roll);
        shots_.push_back(shot);
    }

    if (pattern.waves > 0 && burstRemaining_ == 1)
    {
        for (float sign : {-1.f, 1.f})
        {
            const auto velocity = velocityAt(jitter(weaponRandom_), 545.f);
            const float phase = std::uniform_real_distribution<float>{0.f, 2.f * pi}(weaponRandom_);
            shots_.push_back({ShotKind::PlayerWave, muzzle, velocity, 3.5f, 0.f, sign, phase, velocity.y});
        }
    }
    --burstRemaining_;
    nextPulse_ += (burstRemaining_ > 0 ? burstSpacing : pattern.reload) / fireRateMultiplier_;
}

void Combat::fireSalvo(sf::Vector2f muzzle)
{
    for (int lane = -3; lane <= 3; ++lane)
    {
        const float spread = lane / 3.f;
        Shot shot{ShotKind::Salvo, muzzle + sf::Vector2f{0.f, 3.f * spread},
                  {690.f, 20.f * spread}, 2.8f, 0.f, 0.f};
        shot.verticalAcceleration = 270.f * spread;
        shots_.push_back(shot);
    }
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
    const float angle = 2.7f * (elapsed_ - weaponSince_);
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
        shots_.push_back({ShotKind::Drone, drone.muzzle(), drone.direction * 570.f, 2.4f, 0.f, 0.f});
        shots_.back().droneIndex = i;
        ++drone.shotsFired;
        drone.muzzleFlash = 1.f;
        drone.reload = droneReloads[droneUpgrades_];
    }
}

void Combat::updateShots(float dt)
{
    const auto* primary = guidanceTarget();
    const auto* secondary = primary && drones_.size() > 1 ? guidanceTarget(primary) : nullptr;
    for (auto& shot : shots_)
    {
        if (shot.kind == ShotKind::Missile)
        {
            recordTrail(shot);
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
        if (!steering && shot.kind == ShotKind::PlayerWave)
        {
            const float before = waveOffset(shot.age - dt, shot.wavePhase);
            const float after = waveOffset(shot.age, shot.wavePhase);
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
        if (!steering && shot.verticalAcceleration != 0.f)
        {
            // The arsenal's outer lanes bend only near the muzzle, then hold a narrow fan.
            const float bendTime = shot.kind == ShotKind::Salvo
                ? std::clamp(salvoOpeningTime - (shot.age - dt), 0.f, dt) : dt;
            const float changeInSpeedY = shot.verticalAcceleration * bendTime;
            shot.position.y += changeInSpeedY * (dt - 0.5f * bendTime);
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
    if (invulnerable() || gameOver())
        return;
    --lives_;
    if (gameOver())
    {
        recoveryKills_ = 0;
        recoveryBlinkPhase_.reset();
        lastLifeRecoveryTime_ = -std::numeric_limits<float>::infinity();
    }
    grace_ = 2.f;
    explode(position, ExplosionKind::Player);
}

float Combat::lifeRecoveryFlash() const
{
    return std::max(0.f, 1.f - (elapsed_ - lastLifeRecoveryTime_) / lifeNoticeDuration);
}

float Combat::lifeRecoveryGlow() const
{
    return std::max(0.f, 1.f - (elapsed_ - lastLifeRecoveryTime_) / lifeGlowDuration);
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
        const int directDamage = shot.kind == ShotKind::Player ? 1 : 2;
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
            damage(*enemy, shot.kind == ShotKind::Missile && enemy == hit ? 5 : 2, 0, enemy->position);
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
            if (enemy.health <= 0 || enemy.position.x < 0.f || enemy.position.x > width ||
                !touchesFront(enemy.position, enemy.radius))
                continue;
            auto& hits = enemy.shockwavesHit;
            if (std::find(hits.begin(), hits.end(), wave.seed) != hits.end())
                continue;
            const bool inCore = overlaps(wave.position, coreRadius, enemy.position, enemy.radius);
            damage(enemy, inCore ? stats(EnemyKind::Gunship).health : 3, wave.chainId);
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

void Combat::collide(sf::Vector2f playerPosition)
{
    for (auto& shot : shots_)
        if (shot.isFriendly())
            hitEnemies(shot);

    sweepGunshipShockwaves();
    for (auto& shot : shots_)
    {
        if (!shot.isFriendly() && !shot.spent &&
            overlaps(shot.position, shot.radius, playerPosition, playerHitRadius))
        {
            shot.spent = true;
            hitPlayer(playerPosition);
        }
    }
    if (battleship_ && battleship_->touchesHull(playerPosition, playerHitRadius))
        hitPlayer(playerPosition);
    for (auto& enemy : enemies_)
    {
        if (enemy.health > 0 && !invulnerable() &&
            overlaps(enemy.position, enemy.radius, playerPosition, playerHitRadius))
        {
            if (enemy.kind != EnemyKind::BattleshipTurret)
            {
                enemy.health = 0;
                explodeEnemy(enemy);
            }
            hitPlayer(playerPosition);
        }
    }
    shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [this](const Shot& shot) {
        const bool remove = shot.spent || shot.position.x < -28.f || shot.position.x > width + 28.f ||
                            shot.position.y < -28.f || shot.position.y > height + 28.f;
        if ((remove || gameOver()) && shot.hasTrail())
            fadingTrails_.push_back({shot, 0.f});
        return remove;
    }), shots_.end());
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& enemy) {
        const bool leftField = enemy.velocity.x <= 0.f ? enemy.position.x < -enemy.radius
                                                      : enemy.position.x > width + enemy.radius;
        return enemy.health <= 0 || (enemy.kind != EnemyKind::BattleshipTurret && leftField);
    }), enemies_.end());
}
