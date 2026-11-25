#include "GameAudio.hpp"
#include "Combat.hpp"

#include <algorithm>

GameAudio::GameAudio(const std::filesystem::path& folder)
    : musicData(folder / "coast-patrol.wav"), cannonData(folder / "cannon.wav"),
      blastData(folder / "blast.wav"), gunshipBlastData(folder / "gunship-blast.wav"), impactData(folder / "impact.wav"),
      powerData(folder / "power.wav"), music(musicData), cannon(cannonData),
      blast(blastData), wave(blastData), impact(impactData), upgrade(powerData)
{
    music.setLooping(true);
    music.setVolume(24.f);
    cannon.setVolume(13.f);
    blast.setVolume(45.f);
    wave.setVolume(23.f);
    wave.setPitch(1.65f);
    impact.setVolume(59.f);
    upgrade.setVolume(42.f);
}

void GameAudio::restart()
{
    music.stop();
    impact.stop();
    gunshipBlasts.clear();
    lastVolley = 0;
    lastExplosion = 0;
    lastLives = 3;
    setPlaying(true);
}

void GameAudio::setPlaying(bool playing)
{
    running = playing;
    if (running && !silent)
        music.play();
    else
    {
        music.pause();
        cannon.stop();
        blast.stop();
        gunshipBlasts.clear();
        wave.stop();
        impact.stop();
        upgrade.stop();
    }
}

void GameAudio::toggleMute()
{
    silent = !silent;
    setPlaying(running);
}

void GameAudio::playGunshipBlast()
{
    const auto freeVoice = std::find_if(gunshipBlasts.begin(), gunshipBlasts.end(), [](const sf::Sound& sound) {
        return sound.getStatus() == sf::Sound::Status::Stopped;
    });
    auto& voice = freeVoice == gunshipBlasts.end() ? gunshipBlasts.emplace_back(gunshipBlastData) : *freeVoice;
    voice.setVolume(53.f);
    voice.play();
}

void GameAudio::update(const Combat& combat, bool weaponUpgraded)
{
    const bool audibleFrame = running && !silent;
    if (running && combat.gameOver())
    {
        setPlaying(false);
        if (!silent)
            impact.play();
    }
    if (audibleFrame)
    {
        for (const auto& effect : combat.explosions())
        {
            if (effect.seed <= lastExplosion)
                continue;
            if (effect.kind == ExplosionKind::Gunship)
                playGunshipBlast();
            else if (running && effect.kind == ExplosionKind::Enemy)
                blast.play();
            else if (running && effect.kind == ExplosionKind::Wave)
                wave.play();
        }
        if (running && combat.volleysFired() != lastVolley)
            cannon.play();
        if (running && combat.lives() < lastLives)
            impact.play();
        if (running && weaponUpgraded)
            upgrade.play();
    }
    lastVolley = combat.volleysFired();
    if (!combat.explosions().empty())
        lastExplosion = combat.explosions().back().seed;
    lastLives = combat.lives();
}
