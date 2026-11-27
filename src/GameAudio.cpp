#include "GameAudio.hpp"
#include "Combat.hpp"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float secondDronePitch = 1.22f;

void mixLoop(sf::Sound& voice, float volume, float clock)
{
    if (volume <= 0.f)
    {
        if (voice.getStatus() != sf::Sound::Status::Stopped)
            voice.stop();
        return;
    }
    voice.setVolume(volume);
    if (voice.getStatus() != sf::Sound::Status::Playing)
    {
        const float duration = voice.getBuffer().getDuration().asSeconds();
        voice.setPlayingOffset(sf::seconds(std::fmod(clock, duration)));
        voice.play();
    }
}
}

GameAudio::GameAudio(const std::filesystem::path& folder)
    : musicData(folder / "coast-patrol.wav"), cannonData(folder / "cannon.wav"),
      missileData(folder / "missile.wav"), laserData(folder / "laser.wav"),
      blastData(folder / "blast.wav"), gunshipBlastData(folder / "gunship-blast.wav"), impactData(folder / "impact.wav"),
      powerData(folder / "power.wav"), comboData(folder / "combo-note.wav"),
      fanfareFiveData(folder / "combo-five.wav"), fanfareTenData(folder / "combo-ten.wav"),
      precisionReadyData(folder / "precision-ready.wav"), guidanceReadyData(folder / "guidance-ready.wav"),
      shipHornData(folder / "ship-horn.wav"), lifeRecoveredData(folder / "life-recovered.wav"),
      lifeWarningData(folder / "life-warning.wav"),
      arsenalData(folder / "arsenal.wav"), droneData(folder / "drone-shot.wav"), hullImpactData(folder / "hull-impact.wav"),
      drumData(folder / "combo-drums.wav"), raidPulseData(folder / "raid-pulse.wav"),
      victoryData(folder / "carrier-victory.wav"), braidData(folder / "braid-release.wav"),
      music(musicData), weapon(cannonData), blast(blastData), wave(blastData),
      impact(impactData), upgrade(powerData), comboNote(comboData), fanfare(fanfareFiveData),
      precisionReady(precisionReadyData), guidanceReady(guidanceReadyData), shipHorn(shipHornData),
      lifeRecovered(lifeRecoveredData), lifeWarning(lifeWarningData),
      droneShots{sf::Sound(droneData), sf::Sound(droneData)}, hullImpact(hullImpactData),
      drums(drumData), raidPulse(raidPulseData), victory(victoryData), braid(braidData)
{
    music.setLooping(true);
    drums.setLooping(true);
    raidPulse.setLooping(true);
    weapon.setSpatializationEnabled(false);
    victory.setSpatializationEnabled(false);
    victory.setVolume(65.f);
    braid.setVolume(42.f);
    music.setVolume(24.f);
    weapon.setVolume(13.f);
    blast.setVolume(45.f);
    wave.setVolume(23.f);
    wave.setPitch(1.65f);
    impact.setVolume(59.f);
    upgrade.setVolume(42.f);
    comboNote.setVolume(50.f);
    fanfare.setVolume(55.f);
    precisionReady.setVolume(46.f);
    guidanceReady.setVolume(46.f);
    shipHorn.setVolume(48.f);
    lifeRecovered.setVolume(65.f);
    lifeWarning.setVolume(58.f);
    droneShots[0].setVolume(15.f);
    droneShots[1].setVolume(13.f);
    droneShots[1].setPitch(secondDronePitch);
    hullImpact.setVolume(32.f);
}

void GameAudio::restart()
{
    setPlaying(false);
    setTimeScale(1.f);
    music.stop();
    lastVolley = 0;
    lastDroneShots = {};
    lastExplosion = 0;
    lastMilestone = 0;
    lastPrecisionRefill = 0;
    lastGuidanceRefill = 0;
    lastLifeRecovery = 0;
    lastLifeWarning = 0;
    lastBraid_ = 0;
    drumVolume_ = 0.f;
    lastWorldTime_ = 0.f;
    wasSeaOnly_ = false;
    bossPresent = false;
    setPlaying(true);
}

void GameAudio::setPlaying(bool playing)
{
    running = playing;
    if (!running || silent)
    {
        stopBattleSounds();
        comboNote.stop();
        pendingComboNotes.clear();
        fanfare.stop();
        pendingFanfares.clear();
    }
}

void GameAudio::stopBattleSounds()
{
    music.pause();
    drums.stop();
    raidPulse.stop();
    victory.pause();
    braid.stop();
    weapon.stop();
    for (auto& shot : droneShots)
        shot.stop();
    hullImpact.stop();
    blast.stop();
    gunshipBlasts.clear();
    wave.stop();
    impact.stop();
    upgrade.stop();
    precisionReady.stop();
    guidanceReady.stop();
    shipHorn.stop();
    lifeRecovered.stop();
    lifeWarning.stop();
    pendingLifeNotices = 0;
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
    weapon.setPitch(weaponPitch_ * scale);
    drums.setPitch(scale);
    raidPulse.setPitch(scale);
    victory.setPitch(scale);
    braid.setPitch(scale);
    droneShots[0].setPitch(scale);
    droneShots[1].setPitch(secondDronePitch * scale);
    hullImpact.setPitch(scale);
    blast.setPitch(scale);
    wave.setPitch(1.65f * scale);
    impact.setPitch(scale);
    upgrade.setPitch(scale);
    comboNote.setPitch(comboPitch_ * scale);
    fanfare.setPitch(scale);
    precisionReady.setPitch(scale);
    guidanceReady.setPitch(scale);
    shipHorn.setPitch(scale);
    lifeRecovered.setPitch(scale);
    lifeWarning.setPitch(scale);
    for (auto& voice : gunshipBlasts)
        voice.setPitch(scale);
}

void GameAudio::playWeapon(WeaponMode mode, unsigned leadEmitter)
{
    const sf::SoundBuffer* buffer = &cannonData;
    if (mode == WeaponMode::Missiles)
        buffer = &missileData;
    else if (mode == WeaponMode::Laser)
        buffer = &laserData;
    else if (mode == WeaponMode::Arsenal)
        buffer = &arsenalData;
    if (&weapon.getBuffer() != buffer)
        weapon.setBuffer(*buffer);
    // One accent per firing cycle leaves room for the drones and combo notes.
    weaponPitch_ = mode == WeaponMode::Arsenal && leadEmitter == 1 ? 1.5f : 1.f;
    weapon.setPitch(weaponPitch_ * timeScale_);
    weapon.setPan(mode == WeaponMode::Arsenal ? (leadEmitter == 0 ? -0.78f : 0.78f) : 0.f);
    weapon.setVolume(mode == WeaponMode::Arsenal ? 26.f : 13.f);
    weapon.play();
}

void GameAudio::updateMusic(const Combat& combat, float worldDt)
{
    const auto& raid = combat.carrierRaid();
    float patrol = 1.f;
    if (raid.phase == CarrierPhase::Approach)
        patrol = std::clamp(1.f - raid.age / 1.2f, 0.f, 1.f);
    else if (raid.phase == CarrierPhase::Battle || raid.phase == CarrierPhase::Aftermath)
        patrol = std::clamp(raid.age / 0.9f, 0.f, 1.f);
    else if (raid.phase == CarrierPhase::Sinking)
        patrol = 0.f;

    const float tension = raid.phase == CarrierPhase::Approach || raid.phase == CarrierPhase::Battle
        ? 1.f - patrol : 0.f;
    mixLoop(music, 24.f * patrol, combat.elapsed());
    mixLoop(raidPulse, 52.f * tension, raid.age);

    const int kills = combat.combo().kills;
    const float target = kills >= 2 ? std::min(45.f, 25.f + 2.f * kills) : 0.f;
    drumVolume_ += std::clamp(target - drumVolume_, -45.f * worldDt, 180.f * worldDt);
    // Start the four-bar percussion layer on the same beat as the rhythm guitars.
    mixLoop(drums, drumVolume_ * patrol, music.getPlayingOffset().asSeconds());

    // The stereo victory cue begins with exactly one second of surf before the brass enters.
    if (raid.phase == CarrierPhase::Sinking && raid.age < victoryData.getDuration().asSeconds())
    {
        const bool playing = victory.getStatus() == sf::Sound::Status::Playing;
        if (!playing || std::abs(victory.getPlayingOffset().asSeconds() - raid.age) > 0.12f)
            victory.setPlayingOffset(sf::seconds(raid.age));
        if (!playing)
            victory.play();
    }
    else if (victory.getStatus() != sf::Sound::Status::Stopped)
        victory.stop();
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
    if (combat.campaignWon())
    {
        if (running)
        {
            setPlaying(false);
            if (!silent)
            {
                fanfare.setBuffer(fanfareTenData);
                fanfare.setPitch(1.f);
                fanfare.play();
            }
        }
        return;
    }
    setTimeScale(combat.timeScale());
    const bool audibleFrame = running && !silent;
    const float worldDt = std::max(0.f, combat.elapsed() - lastWorldTime_);
    lastWorldTime_ = combat.elapsed();
    const auto& raid = combat.carrierRaid();
    const bool seaOnly = raid.phase == CarrierPhase::Sinking && raid.age < 1.f;
    if (audibleFrame && seaOnly && !wasSeaOnly_)
    {
        stopBattleSounds();
        comboNote.stop();
        fanfare.stop();
        pendingComboNotes.clear();
    }
    wasSeaOnly_ = seaOnly;
    if (running && combat.gameOver())
    {
        running = false;
        stopBattleSounds();
        fanfare.stop();
        if (!silent && raid.phase != CarrierPhase::Sinking)
            impact.play();
    }
    // A last-life collision on the fatal pass still gets the sinking ship's coda.
    if (!silent && (running || (combat.gameOver() && raid.phase == CarrierPhase::Sinking)))
        updateMusic(combat, worldDt);
    if (running && !silent && combat.lifeRecoveries() > lastLifeRecovery)
    {
        lifeWarning.stop();
        pendingLifeNotices += combat.lifeRecoveries() - lastLifeRecovery;
    }
    if (audibleFrame && !seaOnly)
    {
        if (running && combat.battleship() && !bossPresent)
            shipHorn.play();
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
            else if (running && effect.kind == ExplosionKind::HullImpact)
                hullImpact.play();
            else if (running && effect.kind == ExplosionKind::Player)
                impact.play();
            if (effect.comboKills > 0)
                pendingComboNotes.push_back(effect.comboKills);
        }
        if (running && combat.volleysFired() != lastVolley)
            playWeapon(combat.weaponMode(), combat.arsenalLeadEmitter());
        if (running && combat.braidsFired() > lastBraid_)
            braid.play();
        if (running && weaponUpgraded)
            upgrade.play();
        if (running && combat.precisionRefills() > lastPrecisionRefill)
            precisionReady.play();
        if (running && combat.guidanceRefills() > lastGuidanceRefill)
            guidanceReady.play();
        if (running && combat.lifeRecoveryWarnings() > lastLifeWarning && combat.oneKillFromLife())
            lifeWarning.play();
        if (pendingLifeNotices > 0 && lifeRecovered.getStatus() == sf::Sound::Status::Stopped)
        {
            lifeRecovered.play();
            --pendingLifeNotices;
        }
    }
    // Separate the attacks so even kills in one frame form an audible run of notes.
    if (!seaOnly && !pendingComboNotes.empty() && comboNote.getStatus() == sf::Sound::Status::Stopped)
    {
        const float steps = static_cast<float>(pendingComboNotes.front() - 1);
        pendingComboNotes.pop_front();
        comboPitch_ = 1.f + steps / (12.f + steps);
        comboNote.setPitch(comboPitch_ * timeScale_);
        comboNote.play();
    }
    const unsigned milestone = combat.comboMilestoneSerial();
    if (running && !silent && milestone && milestone != lastMilestone)
        pendingFanfares.push_back(combat.comboMilestone());
    // Let the carrier's fanfare finish before announcing combo milestones won on the final pass.
    if (running && !silent && raid.phase != CarrierPhase::Sinking &&
        !pendingFanfares.empty() && fanfare.getStatus() == sf::Sound::Status::Stopped)
    {
        fanfare.setBuffer(pendingFanfares.front() == 5 ? fanfareFiveData : fanfareTenData);
        pendingFanfares.pop_front();
        fanfare.play();
    }
    lastMilestone = milestone;
    lastBraid_ = combat.braidsFired();
    bossPresent = combat.battleship().has_value();
    lastPrecisionRefill = combat.precisionRefills();
    lastGuidanceRefill = combat.guidanceRefills();
    lastLifeRecovery = combat.lifeRecoveries();
    lastLifeWarning = combat.lifeRecoveryWarnings();
    lastVolley = combat.volleysFired();
    const auto& drones = combat.drones();
    for (std::size_t i = 0; i < droneShots.size(); ++i)
    {
        const unsigned fired = i < drones.size() ? drones[i].shotsFired : 0;
        if (running && !silent && !seaOnly && fired > lastDroneShots[i])
            droneShots[i].play();
        lastDroneShots[i] = fired;
    }
    if (!combat.explosions().empty())
        lastExplosion = combat.explosions().back().seed;
}
