#pragma once

#include "Entity.hpp"

#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Time.hpp>

#include <filesystem>

struct PlayerTextures
{
    explicit PlayerTextures(const std::filesystem::path& folder);

    sf::Texture idle;
    sf::Texture climb;
    sf::Texture descend;
    sf::Texture advance;
    sf::Texture retreat;
};

class Player final : public Entity
{
public:
    Player(const PlayerTextures& textures, sf::Vector2f startingPosition);
    Player(PlayerTextures&&, sf::Vector2f) = delete;

    void update(sf::Vector2f direction, sf::Time elapsed, sf::Vector2f worldSize, bool precision = false);
    sf::Vector2f muzzleOffset() const { return muzzle; }

private:
    const sf::Texture& textureFor(sf::Vector2f direction) const;

    const PlayerTextures& textures;
    sf::Vector2f muzzle{49.f, 0.f};
};
