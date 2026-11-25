#include "Player.hpp"

#include <algorithm>

namespace
{
	constexpr float flightSpeed = 320.f; // World units per second.

	sf::Texture loadTexture(const std::filesystem::path& file)
	{
		sf::Texture texture(file);
		texture.setSmooth(false);
		return texture;
	}

	float clampToWorld(float position, float spriteSize, float worldSize)
	{
		const float margin = std::min(spriteSize, worldSize) * 0.5f;
		return std::clamp(position, margin, worldSize - margin);
	}
}

PlayerTextures::PlayerTextures(const std::filesystem::path& folder)
	: idle(loadTexture(folder / "reposo.png")),
	climb(loadTexture(folder / "subir.png")),
	descend(loadTexture(folder / "bajar.png")),
	advance(loadTexture(folder / "avanzar.png")),
	retreat(loadTexture(folder / "retroceder.png"))
{}

Player::Player(const PlayerTextures& textures, sf::Vector2f startingPosition)
	: Entity(textures.idle), textures(textures)
{
	setPosition(startingPosition);
}

const sf::Texture& Player::textureFor(sf::Vector2f direction) const
{
	// In diagonal flight the bank makes the vertical direction easier to see.
	if (direction.y < 0.f)
		return textures.climb;
	if (direction.y > 0.f)
		return textures.descend;
	if (direction.x > 0.f)
		return textures.advance;
	if (direction.x < 0.f)
		return textures.retreat;
	return textures.idle;
}

void Player::update(sf::Vector2f direction, sf::Time elapsed, sf::Vector2f worldSize)
{
	setTexture(textureFor(direction));
	muzzle.y = direction.y < 0.f ? -10.f : direction.y > 0.f ? 7.f : 0.f;

	auto position = getPosition();
	if (direction != sf::Vector2f{})
		position += direction.normalized() * (flightSpeed * elapsed.asSeconds());

	const auto size = getBounds().size;
	setPosition({ clampToWorld(position.x, size.x, worldSize.x),
				 clampToWorld(position.y, size.y, worldSize.y) });
}
