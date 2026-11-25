#include "Entity.hpp"

#include <SFML/Graphics/RenderTarget.hpp>

Entity::Entity(const sf::Texture& texture) : sprite(texture)
{
    setOrigin(sprite.getLocalBounds().getCenter());
}

sf::FloatRect Entity::getBounds() const
{
    return getTransform().transformRect(sprite.getLocalBounds());
}

void Entity::setTexture(const sf::Texture& texture)
{
    sprite.setTexture(texture, true);
}

void Entity::draw(sf::RenderTarget& target, sf::RenderStates states) const
{
    states.transform *= getTransform();
    target.draw(sprite, states);
}
