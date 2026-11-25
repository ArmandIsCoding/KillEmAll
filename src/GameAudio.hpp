#pragma once

#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>

#include <filesystem>

class Combat;

class GameAudio
{
public:
    explicit GameAudio(const std::filesystem::path& folder);
    void update(const Combat& combat);
    void restart();
    void setPlaying(bool playing);
    void toggleMute();
    bool muted() const { return silent; }

private:
    sf::SoundBuffer musicData, cannonData, blastData, impactData, powerData;
    sf::Sound music, cannon, blast, wave, impact, upgrade;
    unsigned int lastVolley = 0;
    unsigned int lastExplosion = 0;
    int lastLives = 3;
    int lastPower = 1;
    bool running = false;
    bool silent = false;
};
