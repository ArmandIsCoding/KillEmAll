#include "Combat.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr float width = 960.f;
constexpr float height = 540.f;
constexpr float pi = 3.14159265f;

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
    }
    return {};
}

bool friendly(ShotKind kind)
{
    return kind == ShotKind::Player || kind == ShotKind::PlayerWave;
}

bool overlaps(sf::Vector2f a, float aRadius, sf::Vector2f b, float bRadius)
{
    const float range = aRadius + bRadius;
    return (a - b).lengthSquared() <= range * range;
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
}

void Combat::reset()
{
    *this = Combat{};
}

int Combat::power() const
{
    constexpr std::array<float, 4> upgrades{8.f, 18.f, 30.f, 45.f};
    return 1 + static_cast<int>(std::upper_bound(upgrades.begin(), upgrades.end(), elapsed_) - upgrades.begin());
}

void Combat::update(float dt, sf::Vector2f playerPosition, sf::Vector2f muzzleOffset)
{
    if (!std::isfinite(dt) || dt <= 0.f)
        return;

    // Substeps keep small targets hittable even if a frame takes longer.
    while (dt > 0.f)
    {
        const float slice = std::min(dt, 1.f / 120.f);
        for (auto& explosion : explosions_)
            explosion.age += slice;
        explosions_.erase(std::remove_if(explosions_.begin(), explosions_.end(),
            [](const Explosion& effect) { return effect.age >= effect.duration; }), explosions_.end());

        if (!gameOver())
            step(slice, playerPosition, muzzleOffset);
        else if (explosions_.empty())
            break;

        dt -= slice;
    }
}

void Combat::step(float dt, sf::Vector2f playerPosition, sf::Vector2f muzzleOffset)
{
    elapsed_ += dt;
    distance_ += dt * (105.f + std::min(55.f, elapsed_ * 0.5f));
    grace_ = std::max(0.f, grace_ - dt);
    nextFormation_ -= dt;
    if (nextFormation_ <= 0.f)
    {
        spawnFormation();
        nextFormation_ += std::max(1.55f, 2.95f - elapsed_ * 0.022f);
    }
    updateWeapon(dt, playerPosition + muzzleOffset);
    updateEnemies(dt, playerPosition);
    updateShots(dt);
    collide(playerPosition);
}

void Combat::spawnFormation()
{
    // Alternating entry lanes and silhouettes leave readable routes through each wave.
    constexpr std::array<float, 8> lanes{270.f, 144.f, 375.f, 190.f, 348.f, 230.f, 397.f, 150.f};
    const float lane = lanes[formation_ % lanes.size()];
    const unsigned pattern = formation_ % 4;
    const float mirror = (formation_ / 4) % 2 == 0 ? 1.f : -1.f;
    auto add = [&](EnemyKind kind, float delayX, float offsetY, float phase) {
        const auto type = stats(kind);
        const float center = std::clamp(lane + offsetY, 78.f, height - 78.f);
        sf::Vector2f pos{width + type.radius + delayX, center + type.weave * std::sin(phase)};
        enemies_.push_back({kind, pos, {-type.speed, 0.f}, type.radius, 0.f, type.health,
                            center, phase, 0.72f + delayX * 0.002f, 0, 0.f});
    };

    switch (pattern)
    {
        case 0: // echelon
            for (int i = 0; i < 4; ++i)
                add(EnemyKind::Scout, i * 58.f, mirror * i * 31.f, i * 0.55f);
            break;
        case 1: // pincer
            add(EnemyKind::Striker, 0.f, -58.f, 0.f);
            add(EnemyKind::Striker, 22.f, 58.f, pi);
            add(EnemyKind::Scout, 110.f, 0.f, 0.f);
            break;
        case 2: // chevron
            add(EnemyKind::Striker, 0.f, 0.f, 0.f);
            for (int i = 1; i <= 2; ++i)
            {
                add(EnemyKind::Scout, 54.f * i, -40.f * i, 0.f);
                add(EnemyKind::Scout, 54.f * i, 40.f * i, pi);
            }
            break;
        case 3: // escorted heavy
            add(EnemyKind::Gunship, 0.f, 0.f, 0.f);
            add(EnemyKind::Scout, 85.f, -82.f, 0.f);
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
        const auto type = stats(enemy.kind);
        const float phase = enemy.phase + type.frequency * enemy.age;
        enemy.velocity.y = type.weave * type.frequency * std::cos(phase);
        enemy.position.x += enemy.velocity.x * dt;
        enemy.position.y = enemy.centerY + type.weave * std::sin(phase);

        if (enemy.position.x > width - enemy.radius)
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
    const auto muzzle = enemy.position - sf::Vector2f{enemy.radius, 0.f};
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
    }
    ++enemy.volleys;
}

void Combat::updateWeapon(float dt, sf::Vector2f muzzle)
{
    nextPulse_ -= dt;
    if (nextPulse_ > 0.f)
        return;

    ++volleysFired_;
    const int level = power();
    if (burstRemaining_ == 0)
        burstRemaining_ = level >= 5 ? 3 : (level >= 3 ? 2 : 1);
    const int spread = level >= 5 ? 2 : level >= 2 ? 1 : 0;
    for (int i = -spread; i <= spread; ++i)
        shots_.push_back({ShotKind::Player, muzzle, velocityAt(i * (level >= 5 ? 0.18f : 0.12f), 630.f),
                          3.f, 0.f, 0.f});

    if (level >= 4 && burstRemaining_ == 1)
    {
        for (float sign : {-1.f, 1.f})
        {
            const float phase = static_cast<float>(volleysFired_ % 13) * 2.4f + sign;
            shots_.push_back({ShotKind::PlayerWave, muzzle, {545.f, 0.f}, 3.5f, 0.f, sign, phase});
        }
    }
    --burstRemaining_;
    nextPulse_ += burstRemaining_ > 0 ? 0.105f : (level >= 3 ? 0.31f : 0.23f);
}

void Combat::updateShots(float dt)
{
    for (auto& shot : shots_)
    {
        shot.age += dt;
        if (shot.kind == ShotKind::PlayerWave)
        {
            const float before = waveOffset(shot.age - dt, shot.wavePhase);
            const float after = waveOffset(shot.age, shot.wavePhase);
            shot.velocity.y = shot.waveSign * (after - before) / dt;
        }
        shot.position += shot.velocity * dt;
    }
}

void Combat::explode(sf::Vector2f position, ExplosionKind kind)
{
    const float duration = kind == ExplosionKind::Wave ? 0.27f : kind == ExplosionKind::Player ? 0.75f : 0.45f;
    explosions_.push_back({position, 0.f, duration, kind, ++explosionSerial_});
}

void Combat::hitPlayer(sf::Vector2f position)
{
    if (invulnerable())
        return;
    --lives_;
    grace_ = 2.f;
    explode(position, ExplosionKind::Player);
}

void Combat::damage(Enemy& enemy, int amount)
{
    enemy.hitFlash = 0.09f;
    enemy.health = std::max(0, enemy.health - amount);
    if (enemy.health == 0)
    {
        score_ += stats(enemy.kind).points;
        explode(enemy.position, ExplosionKind::Enemy);
    }
}

void Combat::hitEnemies(Shot& shot)
{
    const auto touches = [&](const Enemy& enemy, float radius) {
        return enemy.health > 0 && enemy.position.x <= width &&
               overlaps(shot.position, radius, enemy.position, enemy.radius);
    };
    const auto hit = std::find_if(enemies_.begin(), enemies_.end(),
                                  [&](const Enemy& enemy) { return touches(enemy, shot.radius); });
    if (shot.kind == ShotKind::PlayerWave)
    {
        if (hit == enemies_.end() && shot.age < 0.95f)
            return;
        shot.age = -1.f;
        const bool onScreen = shot.position.x >= 0.f && shot.position.x <= width &&
                              shot.position.y >= 0.f && shot.position.y <= height;
        if (hit == enemies_.end() && !onScreen)
            return;
        explode(shot.position, ExplosionKind::Wave);
        for (Enemy& enemy : enemies_)
            if (touches(enemy, waveBlastRadius))
                damage(enemy, 2);
    }
    else if (hit != enemies_.end())
    {
        shot.age = -1.f;
        damage(*hit, 1);
    }
}

void Combat::collide(sf::Vector2f playerPosition)
{
    for (auto& shot : shots_)
    {
        if (friendly(shot.kind))
            hitEnemies(shot);
        else if (overlaps(shot.position, shot.radius, playerPosition, playerHitRadius))
        {
            shot.age = -1.f;
            hitPlayer(playerPosition);
        }
    }
    for (auto& enemy : enemies_)
    {
        if (enemy.health > 0 && !invulnerable() &&
            overlaps(enemy.position, enemy.radius, playerPosition, playerHitRadius))
        {
            enemy.health = 0;
            explode(enemy.position, ExplosionKind::Enemy);
            hitPlayer(playerPosition);
        }
    }
    shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const Shot& shot) {
        return shot.age < 0.f || shot.position.x < -28.f || shot.position.x > width + 28.f ||
               shot.position.y < -28.f || shot.position.y > height + 28.f;
    }), shots_.end());
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& enemy) {
        return enemy.health <= 0 || enemy.position.x < -enemy.radius;
    }), enemies_.end());
}
