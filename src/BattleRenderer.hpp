#pragma once

#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Vertex.hpp>

#include <filesystem>
#include <vector>

class Combat;
namespace sf { class RenderTarget; }

class BattleRenderer
{
public:
    explicit BattleRenderer(const std::filesystem::path& arcadeDirectory);

    void drawBackground(sf::RenderTarget& target, const Combat& combat);
    void drawEnemies(sf::RenderTarget& target, const Combat& combat);
    void drawEffects(sf::RenderTarget& target, const Combat& combat);

private:
    sf::Texture coast;
    sf::Texture enemyAtlas;
    sf::Texture burstAtlas;
    sf::Texture battleshipHull;
    std::vector<sf::Vertex> geometry;
    std::vector<sf::Vertex> textured;
    std::vector<sf::Vertex> foreground;
};
