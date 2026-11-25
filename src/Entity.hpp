#pragma once

#include <SFML/Graphics/Sprite.hpp>

class Entity : public sf::Drawable, public sf::Transformable
{
public:
    sf::FloatRect getBounds() const;

protected:
    explicit Entity(const sf::Texture& texture);
    void setTexture(const sf::Texture& texture);

private:
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;

    sf::Sprite sprite;
};
