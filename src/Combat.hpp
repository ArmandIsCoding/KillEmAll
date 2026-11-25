#pragma once

#include <SFML/System/Vector2.hpp>

#include <deque>
#include <optional>
#include <random>
#include <unordered_map>
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
    std::vector<unsigned> shockwavesHit{};
};

struct Shot
{
    struct TrailSample
    {
        float age;
        sf::Vector2f position;
        sf::Vector2f velocity;
    };

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
    unsigned sparkSeed = 0;
    float lastGuidedAge = -1.f;
    float waveDriftX = 0.f;
    std::vector<TrailSample> trailHistory{};

    bool hasTrail() const { return kind == ShotKind::Player && (verticalAcceleration != 0.f || !trailHistory.empty()); }
    TrailSample trailSample(float lookback) const;
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
    float sweptRadius = 0.f;
    unsigned chainId = 0;
    int comboKills = 0;

    float shockwaveRadius() const;
};

struct Combo
{
    int kills = 0;
    int bonus = 0;
};

class Combat
{
public:
    static constexpr float playerHitRadius = 6.f;
    static constexpr float pickupRadius = 14.f;
    static constexpr float waveBlastRadius = 39.f;
    static constexpr float trailLifetime = 0.38f;
    static constexpr float gunshipShakeAmplitude = 7.f;
    static constexpr float maxStep = 1.f / 120.f;

    void setPowerInput(bool precisionHeld, bool guidanceHeld);
    float precisionCharge() const { return static_cast<float>(precision_.charge); }
    float guidanceCharge() const { return static_cast<float>(guidance_.charge); }
    bool precisionActive() const { return !gameOver() && precision_.active(); }
    bool guidanceActive() const { return !gameOver() && guidance_.active(); }
    const Enemy* guidanceTarget() const;
    bool precisionRecharging() const { return precision_.depleted; }
    bool guidanceRecharging() const { return guidance_.depleted; }
    unsigned precisionRefills() const { return precision_.refills; }
    unsigned guidanceRefills() const { return guidance_.refills; }
    float timeScale() const { return precisionActive() ? 0.45f : 1.f; }
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
    Combo combo() const { return !gameOver() && elapsed_ < comboUntil_ ? lastCombo_ : Combo{}; }
    int comboMilestone() const { return gameOver() || milestones_.empty() ? 0 : milestones_.front(); }
    unsigned comboMilestoneSerial() const { return comboMilestone() ? finishedMilestones_ + 1 : 0; }
    int bestCombo() const { return bestCombo_; }
    int lives() const { return lives_; }
    int power() const;
    unsigned int volleysFired() const { return volleysFired_; }
    float distance() const { return distance_; }
    float elapsed() const { return elapsed_; }
    bool gameOver() const { return lives_ == 0; }
    bool invulnerable() const { return grace_ > 0.f; }

private:
    struct PowerMeter
    {
        double charge = 1.0;
        unsigned refills = 0;
        bool held = false;
        bool depleted = false;

        bool active() const { return held && !depleted; }
        void advance(float realDt, float useSeconds, float refillSeconds);
    };

    struct Chain
    {
        Combo tally{1, 0}; // The gunship whose explosion started the chain.
        unsigned liveWaves = 0;
    };

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
    void sweepGunshipShockwaves();
    void hitEnemies(Shot& shot);
    void damage(Enemy& enemy, int amount, unsigned chainId = 0);
    void hitPlayer(sf::Vector2f position);
    void explodeEnemy(const Enemy& enemy, unsigned chainId = 0);
    void explode(sf::Vector2f position, ExplosionKind kind, unsigned chainId = 0);

    PowerMeter precision_;
    PowerMeter guidance_;
    std::vector<Enemy> enemies_;
    std::vector<Shot> shots_;
    std::vector<FadingTrail> fadingTrails_;
    std::vector<Explosion> explosions_;
    std::unordered_map<unsigned, Chain> chains_;
    Combo lastCombo_;
    float comboUntil_ = 0.f;
    int bestCombo_ = 0;
    std::deque<int> milestones_;
    unsigned finishedMilestones_ = 0;
    float milestoneUntil_ = 0.f;
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
