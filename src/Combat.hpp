#pragma once

#include <SFML/System/Vector2.hpp>

#include <optional>
#include <random>
#include <vector>

enum class EnemyKind { Scout, Striker, Gunship };
enum class ShotKind { Player, PlayerWave, Enemy, EnemyOrb };
enum class ExplosionKind { Enemy, Gunship, Player, Wave };

struct Enemy
{
    EnemyKind kind;
    sf::Vector2f position;
    sf::Vector2f velocity;
    float radius;
    float age;
    int health;
    float centerY;
    float phase;
    float fireTimer;
    unsigned volleys;
    float hitFlash;
};

struct Shot
{
    ShotKind kind;
    sf::Vector2f position;
    sf::Vector2f velocity;
    float radius;
    float age;
    float waveSign;
    float wavePhase = 0.f;
    float waveDriftY = 0.f;
    float verticalAcceleration = 0.f;
    float trailPower = 0.f;
    bool spent = false;

    bool hasTrail() const { return kind == ShotKind::Player && verticalAcceleration != 0.f; }
};

struct FadingTrail
{
    Shot shot;
    float age;
};

struct FireRatePickup
{
    sf::Vector2f position;
    float centerY;
    float age;
};

struct Explosion
{
    sf::Vector2f position;
    float age;
    float duration;
    ExplosionKind kind;
    unsigned seed;
};

class Combat
{
public:
    static constexpr float playerHitRadius = 6.f;
    static constexpr float pickupRadius = 14.f;
    static constexpr float waveBlastRadius = 39.f;
    static constexpr float trailLifetime = 0.38f;
    static constexpr float gunshipShakeAmplitude = 7.f;

    void update(float dt, sf::Vector2f playerPosition, sf::Vector2f muzzleOffset = {49.f, 0.f});
    void reset();

    const std::vector<Enemy>& enemies() const { return enemies_; }
    const std::vector<Shot>& shots() const { return shots_; }
    const std::vector<FadingTrail>& fadingTrails() const { return fadingTrails_; }
    const std::vector<Explosion>& explosions() const { return explosions_; }
    sf::Vector2f screenShakeOffset() const;
    const std::optional<FireRatePickup>& fireRatePickup() const { return fireRatePickup_; }
    unsigned pickupsCollected() const { return pickupsCollected_; }
    int score() const { return score_; }
    int lives() const { return lives_; }
    int power() const;
    unsigned int volleysFired() const { return volleysFired_; }
    float distance() const { return distance_; }
    float elapsed() const { return elapsed_; }
    bool gameOver() const { return lives_ == 0; }
    bool invulnerable() const { return grace_ > 0.f; }

private:
    void step(float dt, sf::Vector2f playerPosition, sf::Vector2f muzzleOffset);
    void spawnFormation();
    void updateEnemies(float dt, sf::Vector2f playerPosition);
    void fireEnemy(Enemy& enemy, sf::Vector2f playerPosition);
    float nextPickupDelay();
    void updatePickup(float dt, sf::Vector2f playerPosition);
    void updateWeapon(float dt, sf::Vector2f muzzle);
    void fireVolley(sf::Vector2f muzzle);
    void updateShots(float dt);
    void collide(sf::Vector2f playerPosition);
    void hitEnemies(Shot& shot);
    void damage(Enemy& enemy, int amount);
    void hitPlayer(sf::Vector2f position);
    void explodeEnemy(const Enemy& enemy);
    void explode(sf::Vector2f position, ExplosionKind kind);

    std::vector<Enemy> enemies_;
    std::vector<Shot> shots_;
    std::vector<FadingTrail> fadingTrails_;
    std::vector<Explosion> explosions_;
    std::optional<FireRatePickup> fireRatePickup_;
    std::minstd_rand pickupRandom_{std::random_device{}()};
    std::minstd_rand weaponRandom_{std::random_device{}()};
    unsigned pickupsCollected_ = 0;
    float fireRateMultiplier_ = 1.f;
    float nextPickup_ = nextPickupDelay();
    int score_ = 0;
    int lives_ = 3;
    float elapsed_ = 0.f;
    float distance_ = 0.f;
    float grace_ = 0.f;
    float nextFormation_ = 0.f;
    float nextPulse_ = 0.f;
    int burstRemaining_ = 0;
    unsigned formation_ = 0;
    unsigned explosionSerial_ = 0;
    unsigned volleysFired_ = 0;
};
