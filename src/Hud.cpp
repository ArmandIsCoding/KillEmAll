#include "Hud.hpp"
#include "Combat.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <string>

namespace
{
constexpr sf::Color ink{15, 27, 34};
constexpr sf::Color steel{26, 45, 52};
constexpr sf::Color seam{60, 83, 84};
constexpr sf::Color dim{145, 174, 166};
constexpr sf::Color ivory{255, 237, 201};
constexpr sf::Color gold{255, 185, 86};
constexpr sf::Color cyan{97, 224, 201};

void rect(sf::RenderTarget& target, float x, float y, float w, float h, sf::Color color)
{
    sf::RectangleShape shape({w, h});
    shape.setPosition({x, y});
    shape.setFillColor(color);
    target.draw(shape);
}

void label(sf::RenderTarget& target, const sf::Font& font, const sf::String& string,
           float x, float y, unsigned size, sf::Color color)
{
    sf::Text text(font, string, size);
    text.setOrigin(text.getLocalBounds().position);
    text.setPosition({x, y});
    text.setFillColor(color);
    target.draw(text);
}

std::string scoreNumber(int points)
{
    auto number = std::to_string(points);
    if (number.size() < 6)
        number.insert(0, 6 - number.size(), '0');
    return number;
}

void keycap(sf::RenderTarget& target, const sf::Font& font, const sf::String& name,
            float x, float y, float width)
{
    rect(target, x, y, width, 23, seam);
    rect(target, x + 1, y + 1, width - 2, 19, steel);
    label(target, font, name, x + 8, y + 6, 10, ivory);
}

void panel(sf::RenderTarget& target, float x, float y, float w, float h, sf::Color accent)
{
    rect(target, 0, 60, 960, 540, {7, 15, 22, 168});
    rect(target, x + 5, y + 6, w, h, {7, 12, 17, 180});
    rect(target, x, y, w, h, seam);
    rect(target, x + 2, y + 2, w - 4, h - 4, ink);
    rect(target, x, y, w, 4, accent);
}

void pausePanel(sf::RenderTarget& target, const sf::Font& font, bool muted)
{
    panel(target, 234, 164, 492, 317, cyan);
    label(target, font, "PAUSA", 266, 192, 32, ivory);
    rect(target, 266, 248, 427, 2, seam);

    const auto row = [&](float y, const sf::String& key, const sf::String& action) {
        keycap(target, font, key, 266, y, 158);
        label(target, font, action, 439, y + 5, 12, ivory);
    };
    row(265, "WASD / FLECHAS", "MOVERSE");
    row(303, "SHIFT", U"VUELO DE PRECISI\u00d3N");
    row(341, "M", muted ? "ACTIVAR SONIDO" : "SILENCIAR SONIDO");
    row(379, "P", "REANUDAR");
    row(417, "ESC", "SALIR");
}

void endPanel(sf::RenderTarget& target, const sf::Font& font, int score, int bestScore)
{
    panel(target, 216, 170, 528, 292, gold);
    label(target, font, "FIN DE PARTIDA", 248, 200, 27, ivory);
    rect(target, 248, 250, 464, 2, seam);
    label(target, font, "PUNTOS", 248, 273, 9, dim);
    label(target, font, scoreNumber(score), 248, 297, 24, gold);
    label(target, font, U"R\u00c9CORD DE SESI\u00d3N", 487, 273, 9, dim);
    label(target, font, scoreNumber(std::max(score, bestScore)), 487, 299, 19, ivory);
    rect(target, 248, 355, 464, 2, seam);
    keycap(target, font, "ENTER", 248, 383, 72);
    label(target, font, "VOLVER A JUGAR", 334, 389, 12, ivory);
    keycap(target, font, "ESC", 568, 383, 49);
    label(target, font, "SALIR", 632, 390, 10, dim);
}
}

Hud::Hud(const std::filesystem::path& arcadeDir)
    : font(arcadeDir / "font" / "PressStart2P-Regular.ttf"),
      lifeTexture(arcadeDir / "player" / "reposo.png")
{
    font.setSmooth(false);
}

void Hud::draw(sf::RenderTarget& target, const Combat& combat, bool paused, bool muted,
               int bestScore, bool powerNotice, bool pickupNotice) const
{
    rect(target, 0, 0, 960, 60, ink);
    rect(target, 0, 2, 960, 2, steel);
    rect(target, 0, 56, 960, 2, seam);
    rect(target, 0, 58, 960, 2, {149, 96, 55});
    for (float x : {234.f, 412.f, 626.f, 836.f})
        rect(target, x, 11, 2, 34, seam);

    label(target, font, "PUNTOS", 26, 9, 9, dim);
    label(target, font, scoreNumber(combat.score()), 26, 25, 23, gold);
    label(target, font, "VIDAS", 260, 9, 9, dim);
    sf::Sprite plane(lifeTexture);
    plane.setOrigin(sf::Vector2f(lifeTexture.getSize()) * 0.5f);
    plane.setScale({0.38f, 0.38f});
    for (int i = 0; i < 3; ++i)
    {
        plane.setPosition({280.f + i * 44.f, 36.f});
        plane.setColor(i < combat.lives() ? sf::Color::White : sf::Color{108, 137, 145, 135});
        target.draw(plane);
    }

    if (pickupNotice)
        label(target, font, "CADENCIA +50%", 438, 9, 9, cyan);
    else
        label(target, font, powerNotice ? "ARMA MEJORADA" : "POTENCIA", 438, 9, 9, powerNotice ? gold : dim);
    for (int i = 0; i < 5; ++i)
    {
        const float x = 438.f + i * 29.f;
        const bool active = i < combat.power();
        rect(target, x, 27, 25, 15, active ? (powerNotice ? gold : cyan) : seam);
        rect(target, x + 2, active ? 38.f : 29.f, 21, active ? 2.f : 11.f, active ? steel : ink);
    }

    label(target, font, "DISTANCIA", 652, 9, 9, dim);
    const auto meters = std::to_string(static_cast<long long>(combat.distance())) + " m";
    label(target, font, meters, 652, 28, 16, ivory);
    if (!combat.gameOver())
    {
        keycap(target, font, "P", 857, 18, 27);
        label(target, font, paused ? "SEGUIR" : "PAUSA", 895, 25, 9, dim);
    }

    if (combat.gameOver())
        endPanel(target, font, combat.score(), bestScore);
    else if (paused)
        pausePanel(target, font, muted);
    else if (combat.elapsed() < 2.8f)
    {
        rect(target, 22, 524, 410, 58, {14, 27, 33, 233});
        rect(target, 22, 524, 4, 58, gold);
        label(target, font, "COSTA INDUSTRIAL", 38, 535, 16, ivory);
        label(target, font, U"WASD / FLECHAS   DISPARO AUTOM\u00c1TICO", 38, 565, 9, dim);
    }
}
