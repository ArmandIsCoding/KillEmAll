#pragma once

#include "BattleRenderer.hpp"
#include "CampaignProgress.hpp"
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
    enum class Screen { CampaignMenu, LevelSelect, Flight };
    void resize(sf::Vector2u size);
    void keyPressed(Key key);
    void menuKeyPressed(Key key);
    void openLevelSelect();
    void launchLevel(unsigned level);
    void enableAudio();
    sf::Vector2f flightDirection() const;
    bool precisionFlight() const;
    void update(float dt);
    void updateCombat(float slice);
    void finishClearedTransition();
    void draw();
    void drawPlayer();

    sf::RenderWindow window;
    sf::View fieldView, hudView;
    const PlayerTextures textures;
    Player player;
    Combat combat;
    CampaignProgress progress;
    BattleRenderer battle;
    Hud hud;
    std::filesystem::path audioDirectory;
    std::optional<GameAudio> audio;
    std::set<Key> pressed;
    Screen screen = Screen::CampaignMenu;
    Screen selectorParent = Screen::CampaignMenu;
    unsigned selectedOption = 0;
    unsigned selectedLevel = 1;
    bool saveFailed = false;
    bool restored = false;
    bool paused = false;
    float powerNotice = 0.f;
    float pickupNotice = 0.f;
    float muzzleFlash = 0.f;
    std::uint64_t bestScore = 0;
};
