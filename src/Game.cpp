#include "Game.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
constexpr sf::Vector2f fieldSize{960.f, 540.f};
constexpr sf::Vector2f screenSize{960.f, 600.f};
constexpr sf::Vector2f launchPosition{190.f, 270.f};
constexpr float headerHeight = screenSize.y - fieldSize.y;
}

Game::Game(const std::filesystem::path& arcadeDirectory, bool startMuted)
    : window(sf::VideoMode(sf::Vector2u(screenSize)), "Kill 'Em All"),
      fieldView(sf::FloatRect({0.f, 0.f}, fieldSize)), hudView(sf::FloatRect({0.f, 0.f}, screenSize)),
      textures(arcadeDirectory / "player"), player(textures, launchPosition),
      battle(arcadeDirectory), hud(arcadeDirectory), audioDirectory(arcadeDirectory / "audio")
{
    window.setVerticalSyncEnabled(true);
    window.setKeyRepeatEnabled(false);
    resize(window.getSize());
    if (!startMuted)
        enableAudio();
}

void Game::resize(sf::Vector2u size)
{
    if (!size.x || !size.y)
        return;
    const sf::Vector2f pixels(size);
    const float scale = std::min(pixels.x / screenSize.x, pixels.y / screenSize.y);
    const sf::Vector2f extent{screenSize.x * scale / pixels.x, screenSize.y * scale / pixels.y};
    const sf::Vector2f origin = (sf::Vector2f{1.f, 1.f} - extent) * 0.5f;
    hudView.setViewport({origin, extent});
    fieldView.setViewport({{origin.x, origin.y + extent.y * headerHeight / screenSize.y},
                           {extent.x, extent.y * fieldSize.y / screenSize.y}});
}

void Game::enableAudio()
{
    try
    {
        audio.emplace(audioDirectory);
        // Enabling audio mid-flight starts with the next combat event.
        audio->update(combat, false);
        audio->setPlaying(!paused && !combat.finished());
    }
    catch (const std::exception& error)
    {
        std::cerr << "Audio: " << error.what() << '\n';
    }
}

void Game::restart()
{
    combat.reset();
    pressed.clear();
    player.setPosition(launchPosition);
    powerNotice = pickupNotice = muzzleFlash = 0.f;
    paused = false;
    if (audio)
        audio->restart();
}

void Game::keyPressed(Key key)
{
    if (key == Key::Escape)
        window.close();
    else if (key == Key::M)
    {
        if (audio)
            audio->toggleMute();
        else
            enableAudio();
    }
    else if ((key == Key::Enter || key == Key::R) && combat.finished())
        restart();
    else if (key == Key::P && !combat.finished())
    {
        paused = !paused;
        pressed.clear();
        if (audio)
            audio->setPlaying(!paused);
    }
    else if (!paused && !combat.finished())
        pressed.insert(key);
}

sf::Vector2f Game::flightDirection() const
{
    const auto held = [this](Key arrow, Key letter) {
        return pressed.count(arrow) != 0 || pressed.count(letter) != 0;
    };
    return {static_cast<float>(held(Key::Right, Key::D) - held(Key::Left, Key::A)),
            static_cast<float>(held(Key::Down, Key::S) - held(Key::Up, Key::W))};
}

bool Game::precisionFlight() const
{
    return pressed.count(Key::LShift) || pressed.count(Key::RShift);
}

void Game::update(float dt)
{
    combat.setPowerInput(precisionFlight(), pressed.count(Key::Space) != 0);
    if (paused)
        return;
    const int oldPower = combat.power();
    const auto oldRank = combat.weaponRank();
    const WeaponMode oldWeapon = combat.weaponMode();
    const auto oldPickups = combat.pickupsCollected();
    const auto oldVolley = combat.volleysFired();
    const auto direction = flightDirection();
    float worldDt = 0.f;
    while (dt > 0.f)
    {
        const float realSlice = std::min(dt, Combat::maxStep);
        const float worldSlice = realSlice * combat.timeScale();
        if (!combat.finished())
            player.update(direction, sf::seconds(worldSlice), fieldSize);
        combat.update(realSlice, player.getPosition(), player.muzzleOffset());
        worldDt += worldSlice;
        dt -= realSlice;
    }
    bestScore = std::max(bestScore, combat.score());
    const bool leveledUp = combat.power() > oldPower || combat.weaponMode() != oldWeapon ||
                           combat.weaponRank() > oldRank;
    const bool pickedUp = combat.pickupsCollected() > oldPickups;
    powerNotice = leveledUp ? 1.6f : std::max(0.f, powerNotice - worldDt);
    pickupNotice = pickedUp ? 1.6f : std::max(0.f, pickupNotice - worldDt);
    muzzleFlash = combat.volleysFired() != oldVolley ? 0.055f : std::max(0.f, muzzleFlash - worldDt);
    if (audio)
        audio->update(combat, leveledUp || pickedUp);
}

void Game::drawPlayer()
{
    if (combat.gameOver())
        return;
    const auto position = player.getPosition();
    window.draw(player);
    const float recoveryGlow = combat.lifeRecoveryGlow();
    if (!paused && recoveryGlow > 0.f)
        player.drawRecoveryGlow(window, recoveryGlow);

    if (muzzleFlash > 0.f && (combat.weaponMode() == WeaponMode::Cannon || combat.weaponMode() == WeaponMode::Missiles))
    {
        const auto tip = position + player.muzzleOffset();
        const float length = 8.f + (8.f + 0.9f * std::min(20u, combat.weaponRank())) * muzzleFlash / 0.055f;
        const sf::Vertex flash[]{
            {tip + sf::Vector2f{-2.f, -3.f}, {255, 176, 61}}, {tip + sf::Vector2f{length, 0.f}, {255, 247, 178}},
            {tip + sf::Vector2f{-2.f, 3.f}, {255, 176, 61}}
        };
        window.draw(flash, 3, sf::PrimitiveType::Triangles);
    }

    if (combat.precisionActive() || combat.invulnerable())
    {
        const bool shield = combat.invulnerable();
        const float radius = shield ? 18.f + 3.f * std::sin(16.f * combat.elapsed()) : Combat::playerHitRadius;
        sf::CircleShape ring(radius, 20);
        ring.setOrigin({radius, radius});
        ring.setPosition(position);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(shield ? sf::Color{108, 242, 254, 200} : sf::Color{255, 252, 213});
        ring.setOutlineThickness(1.5f);
        window.draw(ring);
        if (combat.precisionActive())
        {
            sf::RectangleShape core({4.f, 4.f});
            core.setPosition(position - sf::Vector2f{2.f, 2.f});
            core.setFillColor({95, 255, 225});
            window.draw(core);
        }
    }
}

void Game::draw()
{
    window.clear({9, 13, 21});
    sf::View camera = fieldView;
    camera.move(combat.screenShakeOffset());
    window.setView(camera);
    battle.drawBackground(window, combat);
    battle.drawEnemies(window, combat);
    drawPlayer();
    battle.drawEffects(window, combat);
    window.setView(hudView);
    hud.draw(window, combat, paused, !audio || audio->muted(), bestScore, powerNotice > 0.f, pickupNotice > 0.f);
    window.display();
}

void Game::run()
{
    sf::Clock clock;
    while (window.isOpen())
    {
        while (const auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
                keyPressed(key->scancode);
            else if (const auto* key = event->getIf<sf::Event::KeyReleased>())
                pressed.erase(key->scancode);
            else if (const auto* resized = event->getIf<sf::Event::Resized>())
                resize(resized->size);
            else if (event->is<sf::Event::FocusLost>())
                pressed.clear();
        }
        if (!window.isOpen())
            break;
        update(std::min(clock.restart().asSeconds(), 0.05f));
        draw();
    }
}
