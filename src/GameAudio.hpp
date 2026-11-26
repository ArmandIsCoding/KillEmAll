#pragma once

#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>

#include <deque>
#include <filesystem>

class Combat;
enum class WeaponMode;

class GameAudio
{
public:
    explicit GameAudio(const std::filesystem::path& folder);
    void update(const Combat& combat, bool weaponUpgraded);
    void restart();
    void setPlaying(bool playing);
    void toggleMute();
    bool muted() const { return silent; }

private:
    void playWeapon(WeaponMode mode);
    void playGunshipBlast();
    void stopBattleSounds();
    void setTimeScale(float scale);

    sf::SoundBuffer musicData, cannonData, missileData, laserData, blastData, gunshipBlastData, impactData, powerData, comboData;
    sf::SoundBuffer fanfareFiveData, fanfareTenData;
    sf::SoundBuffer precisionReadyData, guidanceReadyData, shipHornData;
    sf::Sound music, weapon, blast, wave, impact, upgrade, comboNote, fanfare;
    sf::Sound precisionReady, guidanceReady, shipHorn;
    std::deque<sf::Sound> gunshipBlasts;
    std::deque<int> pendingComboNotes;
    float timeScale_ = 1.f;
    float comboPitch_ = 1.f;
    unsigned int lastVolley = 0;
    unsigned int lastExplosion = 0;
    unsigned lastMilestone = 0;
    unsigned lastPrecisionRefill = 0;
    unsigned lastGuidanceRefill = 0;
    bool running = false;
    bool silent = false;
    bool bossPresent = false;
};
