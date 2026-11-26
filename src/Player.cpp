#include "Player.hpp"

#include <SFML/Graphics/RenderTarget.hpp>

#include <algorithm>
#include <cstdint>

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

void Player::drawRecoveryGlow(sf::RenderTarget& target, float intensity) const
{
	sf::Sprite glow = getSprite();
	sf::RenderStates light(sf::BlendAdd);
	light.transform = getTransform();

	// Offset copies of the current pose soften the light around the wings and tail.
	const float spread = 1.f + 2.f * intensity;
	glow.setColor({255, 203, 112, static_cast<std::uint8_t>(72.f * intensity)});
	for (const sf::Vector2f offset : {sf::Vector2f{-spread, 0.f}, {spread, 0.f}, {0.f, -spread}, {0.f, spread}})
	{
		glow.setPosition(offset);
		target.draw(glow, light);
	}
	glow.setPosition({});
	glow.setColor({255, 244, 190, static_cast<std::uint8_t>(240.f * intensity)});
	target.draw(glow, light);
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
