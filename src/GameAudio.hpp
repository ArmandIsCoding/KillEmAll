#pragma once

#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>

#include <array>
#include <deque>
#include <filesystem>

class Combat;
enum class WeaponMode;

class GameAudio
{
public:
    explicit GameAudio(const std::filesystem::path& folder);
    void update(const Combat& combat, bool weaponUpgraded);
    void restart(const Combat& combat);
    void setPlaying(bool playing);
    void toggleMute();
    bool muted() const { return silent; }

private:
    void playWeapon(WeaponMode mode, unsigned leadEmitter);
    void playGunshipBlast();
    void stopBattleSounds();
    void setTimeScale(float scale);
    void updateMusic(const Combat& combat, float worldDt);

    sf::SoundBuffer musicData, cannonData, missileData, laserData, blastData, gunshipBlastData, impactData, powerData, comboData;
    sf::SoundBuffer fanfareFiveData, fanfareTenData;
    sf::SoundBuffer precisionReadyData, guidanceReadyData, shipHornData, lifeRecoveredData;
    sf::SoundBuffer lifeWarningData;
    sf::SoundBuffer arsenalData, droneData, hullImpactData;
    sf::SoundBuffer drumData, raidPulseData, victoryData, braidData;
    sf::Sound music, weapon, blast, wave, impact, upgrade, comboNote, fanfare;
    sf::Sound precisionReady, guidanceReady, shipHorn, lifeRecovered, lifeWarning;
    std::array<sf::Sound, 2> droneShots;
    sf::Sound hullImpact;
    sf::Sound drums, raidPulse, victory, braid;
    std::deque<sf::Sound> gunshipBlasts;
    std::deque<int> pendingComboNotes;
    std::deque<int> pendingFanfares;
    float timeScale_ = 1.f;
    float comboPitch_ = 1.f;
    float weaponPitch_ = 1.f;
    float drumVolume_ = 0.f;
    float lastWorldTime_ = 0.f;
    bool wasSeaOnly_ = false;
    unsigned lastBraid_ = 0;
    unsigned int lastVolley = 0;
    std::array<unsigned, 2> lastDroneShots{};
    unsigned int lastExplosion = 0;
    unsigned lastMilestone = 0;
    unsigned lastPrecisionRefill = 0;
    unsigned lastGuidanceRefill = 0;
    unsigned lastLifeRecovery = 0;
    unsigned lastLifeWarning = 0;
    unsigned pendingLifeNotices = 0;
    bool running = false;
    bool silent = false;
    bool bossPresent = false;
};
