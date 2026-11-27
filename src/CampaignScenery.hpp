#pragma once

#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Vertex.hpp>

#include <filesystem>
#include <vector>

namespace sf
{
class RenderTarget;
}

class CampaignScenery
{
  public:
    explicit CampaignScenery(const std::filesystem::path& arcadeDirectory);
    void draw(sf::RenderTarget& target, unsigned level, double distance, float time);

  private:
    sf::Texture horizons;
    std::vector<sf::Vertex> textured;
    std::vector<sf::Vertex> details;
};
