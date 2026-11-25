#include "GameAudio.hpp"
#include "Combat.hpp"

#include <algorithm>

GameAudio::GameAudio(const std::filesystem::path& folder)
    : musicData(folder / "coast-patrol.wav"), cannonData(folder / "cannon.wav"),
      blastData(folder / "blast.wav"), gunshipBlastData(folder / "gunship-blast.wav"), impactData(folder / "impact.wav"),
      powerData(folder / "power.wav"), comboData(folder / "combo-note.wav"),
      fanfareFiveData(folder / "combo-five.wav"), fanfareTenData(folder / "combo-ten.wav"),
      music(musicData), cannon(cannonData), blast(blastData), wave(blastData),
      impact(impactData), upgrade(powerData), comboNote(comboData), fanfare(fanfareFiveData)
{
    music.setLooping(true);
    music.setVolume(24.f);
    cannon.setVolume(13.f);
    blast.setVolume(45.f);
    wave.setVolume(23.f);
    wave.setPitch(1.65f);
    impact.setVolume(59.f);
    upgrade.setVolume(42.f);
    comboNote.setVolume(50.f);
    fanfare.setVolume(55.f);
}

void GameAudio::restart()
{
    setPlaying(false);
    setTimeScale(1.f);
    music.stop();
    lastVolley = 0;
    lastExplosion = 0;
    lastMilestone = 0;
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
        stopBattleSounds();
        comboNote.stop();
        pendingComboNotes.clear();
        fanfare.stop();
    }
}

void GameAudio::stopBattleSounds()
{
    music.pause();
    cannon.stop();
    blast.stop();
    gunshipBlasts.clear();
    wave.stop();
    impact.stop();
    upgrade.stop();
}

void GameAudio::toggleMute()
{
    silent = !silent;
    setPlaying(running);
}

void GameAudio::setTimeScale(float scale)
{
    if (timeScale_ == scale)
        return;
    timeScale_ = scale;
    music.setPitch(scale);
    cannon.setPitch(scale);
    blast.setPitch(scale);
    wave.setPitch(1.65f * scale);
    impact.setPitch(scale);
    upgrade.setPitch(scale);
    comboNote.setPitch(comboPitch_ * scale);
    fanfare.setPitch(scale);
    for (auto& voice : gunshipBlasts)
        voice.setPitch(scale);
}

void GameAudio::playGunshipBlast()
{
    const auto freeVoice = std::find_if(gunshipBlasts.begin(), gunshipBlasts.end(), [](const sf::Sound& sound) {
        return sound.getStatus() == sf::Sound::Status::Stopped;
    });
    auto& voice = freeVoice == gunshipBlasts.end() ? gunshipBlasts.emplace_back(gunshipBlastData) : *freeVoice;
    voice.setVolume(53.f);
    voice.setPitch(timeScale_);
    voice.play();
}

void GameAudio::update(const Combat& combat, bool weaponUpgraded)
{
    setTimeScale(combat.timeScale());
    const bool audibleFrame = running && !silent;
    if (running && combat.gameOver())
    {
        running = false;
        stopBattleSounds();
        fanfare.stop();
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
            if (effect.comboKills > 0)
                pendingComboNotes.push_back(effect.comboKills);
        }
        if (running && combat.volleysFired() != lastVolley)
            cannon.play();
        if (running && combat.lives() < lastLives)
            impact.play();
        if (running && weaponUpgraded)
            upgrade.play();
    }
    // Separate the attacks so even kills in one frame form an audible run of notes.
    if (!pendingComboNotes.empty() && comboNote.getStatus() == sf::Sound::Status::Stopped)
    {
        const float steps = static_cast<float>(pendingComboNotes.front() - 1);
        pendingComboNotes.pop_front();
        comboPitch_ = 1.f + steps / (12.f + steps);
        comboNote.setPitch(comboPitch_ * timeScale_);
        comboNote.play();
    }
    const unsigned milestone = combat.comboMilestoneSerial();
    if (running && !silent && milestone && milestone != lastMilestone)
    {
        fanfare.setBuffer(combat.comboMilestone() == 5 ? fanfareFiveData : fanfareTenData);
        fanfare.play();
    }
    lastMilestone = milestone;
    lastVolley = combat.volleysFired();
    if (!combat.explosions().empty())
        lastExplosion = combat.explosions().back().seed;
    lastLives = combat.lives();
}
