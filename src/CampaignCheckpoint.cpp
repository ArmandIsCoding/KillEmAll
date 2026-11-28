#include "Combat.hpp"

#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>

namespace
{
// Keep the read and write order identical. Only the three PRNG seeds need special decoding.
template<class Entrance, class Visit>
void visitFields(Entrance& c, const Visit& visit)
{
    visit(c.level, c.elapsed, c.distance, c.playerX, c.playerY, c.score, c.enemiesDestroyed);
    visit(c.bestCombo, c.lives, c.recoveryKills, c.recoveryBlinkPhase, c.lifeRecoveries, c.lifeRecoveryWarnings);
    visit(c.weaponMode, c.weaponRank, c.droneUpgrades, c.pickupsCollected, c.fireRateMultiplier, c.nextPickup);
    visit(c.weaponSince, c.nextPulse, c.salvoShotSpacing, c.burstRemaining, c.salvoShotsLeft, c.nextEmitter);
    visit(c.volleysFired, c.braidsFired, c.explosionSerial);
    for (auto* meter : {&c.precision, &c.guidance})
        visit(meter->charge, meter->depleted, meter->refills, meter->lastRefill);
    for (auto& drone : c.drones)
        visit(drone.aimX, drone.aimY, drone.shotsFired);
}

bool validPower(const CampaignCheckpoint::Power& meter, double elapsed)
{
    return meter.charge >= 0.0 && meter.charge <= 1.0 && !(meter.depleted && meter.charge == 1.0) &&
           (meter.lastRefill == -1.f || (meter.lastRefill >= 0.f && meter.lastRefill <= static_cast<float>(elapsed)));
}
}

bool CampaignCheckpoint::valid() const
{
    if (level < 1 || level > Combat::levelCount || lives < 1 || lives > Combat::maxLives ||
        recoveryKills < 0 || recoveryKills >= Combat::killsPerLife || (lives == Combat::maxLives && recoveryKills != 0))
        return false;
    const bool blinking = recoveryKills >= Combat::killsPerLife - 5;
    if (blinking ? !(recoveryBlinkPhase >= 0.f && recoveryBlinkPhase < 1.f) : recoveryBlinkPhase != -1.f)
        return false;
    if (!(elapsed >= 0.0 && elapsed <= std::numeric_limits<float>::max()) ||
        !(distance >= 0.0 && distance < static_cast<double>(std::numeric_limits<std::int64_t>::max())) ||
        !(playerX >= 0.f && playerX <= 960.f && playerY >= 0.f && playerY <= 540.f))
        return false;
    if (bestCombo < 0 || static_cast<std::uint64_t>(bestCombo) > enemiesDestroyed ||
        weaponMode > static_cast<unsigned>(WeaponMode::Arsenal) || weaponRank > enemiesDestroyed ||
        droneUpgrades > Combat::maxDroneUpgrades || (droneUpgrades && weaponMode != static_cast<unsigned>(WeaponMode::Arsenal)))
        return false;
    if (!(weaponSince >= 0.0 && weaponSince <= elapsed) || !std::isfinite(fireRateMultiplier) ||
        fireRateMultiplier < 1.f || !std::isfinite(nextPickup) || !std::isfinite(nextPulse) ||
        nextPulse < -Combat::maxStep || !(salvoShotSpacing >= 0.f && salvoShotSpacing <= 1.f) ||
        burstRemaining < 0 || burstRemaining > 3 || nextEmitter > 1 ||
        salvoShotsLeft > 7 + 2ull * weaponRank || (salvoShotsLeft && salvoShotSpacing == 0.f))
        return false;
    for (const auto& drone : drones)
        if (!(std::abs(drone.aimX) <= 1.001f && std::abs(drone.aimY) <= 1.001f))
            return false;
    return validPower(precision, elapsed) && validPower(guidance, elapsed);
}

std::ostream& operator<<(std::ostream& out, const CampaignCheckpoint& entrance)
{
    const auto precision = out.precision();
    const auto flags = out.flags();
    out << std::dec << std::noboolalpha << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10);
    visitFields(entrance, [&](const auto&... values) { ((out << values << ' '), ...); });
    out << entrance.pickupRandom << ' ' << entrance.weaponRandom << ' ' << entrance.flightRandom;
    out.precision(precision);
    out.flags(flags);
    return out;
}

std::istream& operator>>(std::istream& in, CampaignCheckpoint& entrance)
{
    CampaignCheckpoint candidate;
    visitFields(candidate, [&](auto&... values) { (in >> ... >> values); });
    std::array<std::minstd_rand::result_type, 3> seeds{};
    for (auto& seed : seeds)
    {
        if (!(in >> seed) || seed < std::minstd_rand::min() || seed > std::minstd_rand::max())
        {
            in.setstate(std::ios::failbit);
            return in;
        }
    }
    if (!candidate.valid())
        in.setstate(std::ios::failbit);
    else
    {
        candidate.pickupRandom.seed(seeds[0]);
        candidate.weaponRandom.seed(seeds[1]);
        candidate.flightRandom.seed(seeds[2]);
        entrance = candidate;
    }
    return in;
}

Combat::Checkpoint Combat::checkpoint(sf::Vector2f playerPosition) const
{
    Checkpoint c;
    c.level = level_;
    c.elapsed = levelStartTime_;
    c.distance = levelStartDistance_;
    c.playerX = playerPosition.x;
    c.playerY = playerPosition.y;
    c.score = score_;
    c.enemiesDestroyed = enemiesDestroyed_;
    c.bestCombo = bestCombo_;
    c.lives = lives_;
    c.recoveryKills = recoveryKills_;
    c.recoveryBlinkPhase = recoveryBlinkPhase_.value_or(-1.f);
    c.lifeRecoveries = lifeRecoveries_;
    c.lifeRecoveryWarnings = lifeRecoveryWarnings_;
    c.weaponMode = static_cast<unsigned>(weaponMode_);
    c.weaponRank = weaponRank_;
    c.droneUpgrades = droneUpgrades_;
    c.pickupsCollected = pickupsCollected_;
    c.fireRateMultiplier = fireRateMultiplier_;
    c.nextPickup = nextPickup_;
    c.weaponSince = weaponSince_;
    c.nextPulse = nextPulse_;
    c.salvoShotSpacing = salvoShotSpacing_;
    c.burstRemaining = burstRemaining_;
    c.salvoShotsLeft = salvoShotsLeft_;
    c.nextEmitter = nextEmitter_;
    c.volleysFired = volleysFired_;
    c.braidsFired = braidsFired_;
    c.explosionSerial = explosionSerial_;
    const auto saveMeter = [](const PowerMeter& meter) -> Checkpoint::Power {
        return {meter.charge, meter.depleted, meter.refills, std::isfinite(meter.lastRefillTime) ? meter.lastRefillTime : -1.f};
    };
    c.precision = saveMeter(precision_);
    c.guidance = saveMeter(guidance_);
    for (std::size_t i = 0; i < drones_.size(); ++i)
        c.drones[i] = {drones_[i].direction.x, drones_[i].direction.y, drones_[i].shotsFired};
    c.pickupRandom = pickupRandom_;
    c.weaponRandom = weaponRandom_;
    c.flightRandom = flightRandom_;
    return c;
}

bool Combat::restoreCheckpoint(const Checkpoint& c)
{
    if (!c.valid())
        return false;
    reset();
    level_ = c.level;
    elapsed_ = levelStartTime_ = c.elapsed;
    distance_ = levelStartDistance_ = c.distance;
    score_ = c.score;
    enemiesDestroyed_ = c.enemiesDestroyed;
    bestCombo_ = c.bestCombo;
    lives_ = c.lives;
    recoveryKills_ = c.recoveryKills;
    if (c.recoveryBlinkPhase >= 0.f)
        recoveryBlinkPhase_ = c.recoveryBlinkPhase;
    lifeRecoveries_ = c.lifeRecoveries;
    lifeRecoveryWarnings_ = c.lifeRecoveryWarnings;
    weaponMode_ = static_cast<WeaponMode>(c.weaponMode);
    weaponRank_ = c.weaponRank;
    droneUpgrades_ = c.droneUpgrades;
    pickupsCollected_ = c.pickupsCollected;
    fireRateMultiplier_ = c.fireRateMultiplier;
    nextPickup_ = c.nextPickup;
    weaponSince_ = c.weaponSince;
    nextPulse_ = c.nextPulse;
    salvoShotSpacing_ = c.salvoShotSpacing;
    burstRemaining_ = c.burstRemaining;
    salvoShotsLeft_ = c.salvoShotsLeft;
    nextEmitter_ = c.nextEmitter;
    volleysFired_ = c.volleysFired;
    braidsFired_ = c.braidsFired;
    explosionSerial_ = c.explosionSerial;
    const auto restoreMeter = [](const Checkpoint::Power& saved, PowerMeter& meter) {
        meter.charge = saved.charge;
        meter.depleted = saved.depleted;
        meter.refills = saved.refills;
        if (saved.lastRefill >= 0.f)
            meter.lastRefillTime = saved.lastRefill;
    };
    restoreMeter(c.precision, precision_);
    restoreMeter(c.guidance, guidance_);
    pickupRandom_ = c.pickupRandom;
    weaponRandom_ = c.weaponRandom;
    flightRandom_ = c.flightRandom;
    if (level_ > 1)
    {
        carrierRaid_.phase = CarrierPhase::Finished;
        startLevelWaves();
    }
    if (weaponMode_ == WeaponMode::Arsenal)
    {
        // During the clear sequence the drones finish reloading. Their orbit follows the flight clock.
        drones_.resize(droneUpgrades_ >= secondDroneUpgrade ? 2 : 1);
        const sf::Vector2f position{c.playerX, c.playerY};
        for (std::size_t i = 0; i < drones_.size(); ++i)
        {
            drones_[i].direction = {c.drones[i].aimX, c.drones[i].aimY};
            drones_[i].shotsFired = c.drones[i].shotsFired;
        }
        arsenalMuzzle_ = position + sf::Vector2f{49.f, 0.f};
        updateDrones(0.f, position);
    }
    return true;
}
