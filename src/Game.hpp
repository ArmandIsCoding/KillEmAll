#pragma once

#include "BattleRenderer.hpp"
#include "Combat.hpp"
#include "GameAudio.hpp"
#include "Hud.hpp"
#include "Player.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <filesystem>
#include <optional>
#include <set>

class Game
{
public:
    Game(const std::filesystem::path& arcadeDirectory, bool startMuted);
    void run();

private:
    using Key = sf::Keyboard::Scancode;
    void resize(sf::Vector2u size);
    void keyPressed(Key key);
    void restart();
    void enableAudio();
    sf::Vector2f flightDirection() const;
    bool precisionFlight() const;
    void update(float dt);
    void draw();
    void drawPlayer();

    sf::RenderWindow window;
    sf::View fieldView, hudView;
    const PlayerTextures textures;
    Player player;
    Combat combat;
    BattleRenderer battle;
    Hud hud;
    std::filesystem::path audioDirectory;
    std::optional<GameAudio> audio;
    std::set<Key> pressed;
    bool paused = false;
    float powerNotice = 0.f;
    float muzzleFlash = 0.f;
    int bestScore = 0;
};
