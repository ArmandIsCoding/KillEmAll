#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <filesystem>

class Combat;
namespace sf { class RenderTarget; }

class Hud
{
public:
    explicit Hud(const std::filesystem::path& arcadeDir);
    void draw(sf::RenderTarget& target, const Combat& combat, bool paused, bool muted,
              int bestScore, bool powerNotice, bool pickupNotice) const;

private:
    sf::Font font;
    sf::Texture lifeTexture;
};
