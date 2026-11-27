#pragma once

namespace sf
{
class RenderTarget;
class Texture;
}
class Combat;

void drawCarrierSky(sf::RenderTarget& target, const Combat& combat);
void drawCarrier(sf::RenderTarget& target, const Combat& combat, const sf::Texture& hull);
