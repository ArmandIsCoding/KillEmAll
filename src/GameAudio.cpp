#include "GameAudio.hpp"
#include "Combat.hpp"

GameAudio::GameAudio(const std::filesystem::path& folder)
    : musicData(folder / "coast-patrol.wav"), cannonData(folder / "cannon.wav"),
      blastData(folder / "blast.wav"), impactData(folder / "impact.wav"),
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
    lastVolley = 0;
    lastExplosion = 0;
    lastLives = 3;
    lastPower = 1;
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

void GameAudio::update(const Combat& combat)
{
    if (running && combat.gameOver())
    {
        setPlaying(false);
        if (!silent)
            impact.play();
    }
    else if (!silent && running)
    {
        if (combat.volleysFired() != lastVolley)
            cannon.play();
        for (const auto& effect : combat.explosions())
        {
            if (effect.seed > lastExplosion && effect.kind == ExplosionKind::Enemy)
                blast.play();
            if (effect.seed > lastExplosion && effect.kind == ExplosionKind::Wave)
                wave.play();
        }
        if (combat.lives() < lastLives)
            impact.play();
        if (combat.power() > lastPower)
            upgrade.play();
    }
    lastVolley = combat.volleysFired();
    if (!combat.explosions().empty())
        lastExplosion = combat.explosions().back().seed;
    lastLives = combat.lives();
    lastPower = combat.power();
}
