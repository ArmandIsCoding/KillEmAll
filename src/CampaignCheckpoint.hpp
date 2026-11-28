#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <random>

// A level entrance, after the previous encounter and its projectiles have cleared.
struct CampaignCheckpoint
{
    struct Power
    {
        double charge = 1.0;
        bool depleted = false;
        unsigned refills = 0;
        float lastRefill = -1.f;
    };
    struct Drone
    {
        float aimX = 1.f, aimY = 0.f;
        unsigned shotsFired = 0;
    };

    unsigned level = 1;
    double elapsed = 0.0, distance = 0.0;
    float playerX = 190.f, playerY = 270.f;
    std::uint64_t score = 0, enemiesDestroyed = 0;
    int bestCombo = 0, lives = 3, recoveryKills = 0;
    float recoveryBlinkPhase = -1.f;
    unsigned lifeRecoveries = 0, lifeRecoveryWarnings = 0;
    unsigned weaponMode = 0, weaponRank = 0, droneUpgrades = 0, pickupsCollected = 0;
    float fireRateMultiplier = 1.f, nextPickup = 0.f;
    double weaponSince = 0.0;
    float nextPulse = 0.f, salvoShotSpacing = 0.f;
    int burstRemaining = 0;
    unsigned salvoShotsLeft = 0, nextEmitter = 0, volleysFired = 0, braidsFired = 0;
    unsigned explosionSerial = 0;
    Power precision, guidance;
    std::array<Drone, 2> drones{};
    std::minstd_rand pickupRandom, weaponRandom, flightRandom;

    bool valid() const;
};

std::ostream& operator<<(std::ostream& out, const CampaignCheckpoint& entrance);
std::istream& operator>>(std::istream& in, CampaignCheckpoint& entrance);
