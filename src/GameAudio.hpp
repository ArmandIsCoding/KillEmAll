#pragma once

#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>

#include <deque>
#include <filesystem>

class Combat;

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
    void playGunshipBlast();
    void stopBattleSounds();
    void setTimeScale(float scale);

    sf::SoundBuffer musicData, cannonData, blastData, gunshipBlastData, impactData, powerData, comboData;
    sf::SoundBuffer fanfareFiveData, fanfareTenData;
    sf::Sound music, cannon, blast, wave, impact, upgrade, comboNote, fanfare;
    std::deque<sf::Sound> gunshipBlasts;
    std::deque<int> pendingComboNotes;
    float timeScale_ = 1.f;
    float comboPitch_ = 1.f;
    unsigned int lastVolley = 0;
    unsigned int lastExplosion = 0;
    unsigned lastMilestone = 0;
    int lastLives = 3;
    bool running = false;
    bool silent = false;
};
