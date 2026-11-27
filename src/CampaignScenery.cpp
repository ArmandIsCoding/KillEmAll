#include "CampaignScenery.hpp"

#include <SFML/Graphics/RenderTarget.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
using Batch = std::vector<sf::Vertex>;
using Vec = sf::Vector2f;
using Color = sf::Color;
constexpr float width = 960.f, height = 540.f, margin = 10.f;
constexpr float panoramaWidth = 1090.f;

struct Palette
{
    Color rim, weather;
};
constexpr std::array<Palette, 9> paint{{
    {{146, 93, 70}, {197, 163, 130}}, {{94, 132, 143}, {141, 180, 198}},
    {{187, 217, 223}, {202, 228, 240}}, {{160, 112, 74}, {219, 168, 111}},
    {{82, 113, 71}, {160, 188, 115}}, {{137, 71, 62}, {224, 134, 84}},
    {{86, 83, 126}, {143, 151, 210}}, {{174, 186, 203}, {209, 212, 234}},
    {{112, 71, 86}, {170, 128, 150}},
}};

float wrap(double position, float period)
{
    return static_cast<float>(position - period * std::floor(position / period));
}

void quad(Batch& b, Vec a, Vec c, Vec d, Vec e, Color color)
{
    b.insert(b.end(), {{a, color}, {c, color}, {d, color}, {a, color}, {d, color}, {e, color}});
}

void box(Batch& b, float x, float y, float w, float h, Color color)
{
    x = std::round(x);
    y = std::round(y);
    quad(b, {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, color);
}

void line(Batch& b, Vec a, Vec z, float thickness, Color color)
{
    const auto direction = z - a;
    const float length = direction.length();
    if (length < 0.1f)
        return;
    const auto side = direction.perpendicular() * (0.5f * thickness / length);
    quad(b, a - side, a + side, z + side, z - side, color);
}

Color alpha(Color color, unsigned opacity)
{
    color.a = static_cast<std::uint8_t>(opacity);
    return color;
}

// A distant landmark such as the eclipse should only pass once per stage.
void panoramaStrip(Batch& batch, sf::Vector2u size, unsigned scene, float top, float bottom, float pan, float sourceTop,
                   float sourceBottom)
{
    const float tileWidth = static_cast<float>(size.x / 3);
    const float tileHeight = static_cast<float>(size.y / 3);
    const float u = tileWidth * (scene % 3) + 2.f;
    const float v = tileHeight * (scene / 3) + 2.f;
    const float croppedWidth = (tileWidth - 4.f) / panoramaWidth;
    const float u1 = u + pan * croppedWidth, u2 = u1 + (width + 2.f * margin) * croppedWidth;
    const float v1 = v + sourceTop * (tileHeight - 4.f), v2 = v + sourceBottom * (tileHeight - 4.f);
    const Color tint{205, 210, 214};
    const sf::Vertex a{{-margin, top}, tint, {u1, v1}}, b{{width + margin, top}, tint, {u2, v1}};
    const sf::Vertex c{{width + margin, bottom}, tint, {u2, v2}}, d{{-margin, bottom}, tint, {u1, v2}};
    batch.insert(batch.end(), {a, b, c, a, c, d});
}

// Top and bottom of an illustrated landmark, in the panel's original 512 × 341 coordinate system.
// Tracing both edges keeps open water and sky out of the moving silhouette.
struct Section
{
    float x, top, bottom;
};
constexpr Section wreck[] = {{5, 159, 276},   {25, 160, 282},  {55, 158, 285},  {79, 152, 285},  {104, 166, 282},
                             {134, 163, 285}, {160, 160, 282}, {175, 160, 254}, {191, 159, 193}, {204, 157, 165}};
constexpr Section island[] = {{2, 264, 277},  {14, 251, 280}, {24, 244, 280},  {34, 228, 278},
                              {45, 226, 281}, {51, 231, 279}, {65, 228, 277},  {77, 246, 279},
                              {88, 246, 277}, {98, 257, 277}, {108, 266, 277}, {118, 276, 277}};
constexpr Section iceberg[] = {{395, 313, 315}, {407, 311, 317}, {417, 305, 317}, {427, 304, 317},
                               {433, 295, 317}, {447, 294, 317}, {455, 289, 317}, {466, 288, 317},
                               {476, 292, 316}, {481, 300, 316}, {498, 301, 317}, {507, 306, 313}};
constexpr Section tank[] = {{219, 230, 248}, {222, 213, 250}, {228, 207, 249}, {235, 202, 250}, {246, 200, 251},
                            {254, 200, 252}, {263, 204, 251}, {272, 209, 252}, {278, 218, 251}, {280, 230, 251}};
constexpr Section mangrove[] = {{149, 330, 335}, {157, 321, 337}, {170, 322, 336}, {178, 318, 337},
                                {187, 316, 337}, {194, 319, 336}, {202, 314, 335}, {208, 311, 335},
                                {215, 318, 334}, {229, 321, 331}, {242, 327, 328}};
constexpr Section volcanicRock[] = {{95, 281, 301},  {104, 262, 303}, {113, 260, 304}, {120, 247, 305},
                                    {125, 239, 305}, {134, 233, 307}, {137, 235, 308}, {139, 251, 308},
                                    {150, 244, 308}, {157, 246, 309}, {164, 279, 309}, {176, 294, 307}};
constexpr Section arcology[] = {{65, 144, 218},  {70, 140, 220},  {71, 81, 219},   {88, 76, 220},
                                {94, 74, 221},   {94, 61, 221},   {102, 59, 224},  {105, 68, 223},
                                {119, 65, 224},  {139, 72, 224},  {147, 83, 225},  {140, 86, 225},
                                {144, 135, 225}, {151, 141, 224}, {151, 171, 224}, {158, 170, 224}};
constexpr Section skyPlatform[] = {{235, 224, 225}, {243, 221, 228}, {253, 221, 226}, {257, 215, 240},
                                   {264, 216, 236}, {269, 209, 237}, {275, 204, 242}, {277, 218, 244},
                                   {285, 217, 239}, {292, 220, 236}, {299, 222, 230}, {309, 222, 225}};
constexpr Section fortress[] = {{365, 274, 279}, {373, 266, 279}, {380, 266, 279}, {388, 252, 279},
                                {393, 250, 279}, {397, 234, 277}, {403, 230, 278}, {408, 218, 278},
                                {414, 215, 277}, {420, 240, 277}, {425, 258, 278}, {438, 274, 278}};

struct Landform
{
    const Section* sections;
    std::size_t count;
    float waterline;
    float scale;
};

template <std::size_t N> constexpr Landform trace(const Section (&sections)[N], float waterline, float scale)
{
    return {sections, N, waterline, scale};
}
constexpr std::array<Landform, 9> landforms{
    {trace(wreck, 285, .84f), trace(island, 281, 1.f), trace(iceberg, 317, 1.35f), trace(tank, 252, 1.1f),
     trace(mangrove, 337, 1.3f), trace(volcanicRock, 309, 1.06f), trace(arcology, 225, .59f),
     trace(skyPlatform, 244, 1.68f), trace(fortress, 279, 1.16f)}};

void drawLandform(Batch& batch, sf::Vector2u size, unsigned scene, float x, float y, bool nearBank, bool reflected,
                  float time)
{
    const auto& form = landforms[scene];
    const auto* section = form.sections;
    const float center = .5f * (section[0].x + section[form.count - 1].x);
    const float scale = form.scale * (nearBank ? 1.22f : .72f);
    const float tileWidth = static_cast<float>(size.x / 3), tileHeight = static_cast<float>(size.y / 3);
    const Color tint = nearBank ? Color{177, 176, 184} : Color{150, 162, 174};
    const auto point = [&](float px, float py) {
        float dy = (py - form.waterline) * scale;
        float sx = x + (px - center) * scale * (nearBank ? 1.f : -1.f);
        Color light = tint;
        if (reflected)
        {
            dy *= -.24f;
            sx += dy * .11f * std::sin(time * 2.4f + dy * .72f);
            light.a = static_cast<std::uint8_t>(74.f * std::clamp(1.f - dy / 28.f, 0.f, 1.f));
        }
        return sf::Vertex{
            {sx, y + dy}, light, {tileWidth * ((scene % 3) + px / 512.f), tileHeight * ((scene / 3) + py / 341.f)}};
    };
    auto a = point(section[0].x, section[0].top), d = point(section[0].x, section[0].bottom);
    for (std::size_t i = 1; i < form.count; ++i)
    {
        const auto b = point(section[i].x, section[i].top), c = point(section[i].x, section[i].bottom);
        batch.insert(batch.end(), {a, b, c, a, c, d});
        a = b;
        d = c;
    }
}

void atmosphere(Batch& b, unsigned level, const Palette& p, double distance, float t)
{
    const bool rain = level == 3 || level == 8;
    const bool rising = level == 6 || level == 7 || level == 10;
    const int count = rain ? 65 : 32;
    for (int i = 0; i < count; ++i)
    {
        const float depth = .2f + .8f * ((i * 13) % 23) / 22.f;
        const float drift = rain ? 130.f * depth : rising ? 12.f * depth : 19.f * depth;
        const float speedY = rain ? 178.f + 90.f * depth : rising ? -10.f - 15.f * depth : 7.f + 7.f * depth;
        const float x = wrap(i * 274.3 - distance * (.08 + .28 * depth) - t * drift, 1010.f) - 25.f;
        const float y = wrap(i * 97.7 + t * speedY, 550.f) - 5.f;
        if (rain)
            line(b, {x, y}, {x - 2.f - 5.f * depth, y + 3.f + 8.f * depth}, 1.f, alpha(p.weather, 42));
        else
        {
            const unsigned opacity = static_cast<unsigned>(42.f + 32.f * std::sin(i + t * 1.6f));
            const float length = level == 5 || level == 9 ? 3.f + depth * 5.f : 1.f + depth;
            box(b, x, y, length, 1.f, alpha(p.weather, opacity));
        }
    }
    if (level == 3)
    {
        const float beat = wrap(t + 2.6f, 14.f);
        if (beat < .65f)
            box(b, -margin, -margin, width + 2 * margin, height + 2 * margin,
                alpha({155, 174, 204}, static_cast<unsigned>(17.f * std::sin(beat / .65f * 3.14159f))));
    }
}
} // namespace

CampaignScenery::CampaignScenery(const std::filesystem::path& arcadeDirectory)
    : horizons(arcadeDirectory / "campaign-horizons.png")
{
    textured.reserve(300);
    details.reserve(2200);
}

void CampaignScenery::draw(sf::RenderTarget& target, unsigned level, double distance, float time)
{
    if (level < 2 || level > 10)
        return;
    const auto scene = level - 2;
    const auto size = horizons.getSize();
    const auto& palette = paint[scene];
    // Bounded camera travel keeps the distant geography intact. Shores and spray continue to move past the camera.
    const float pan =
        2.f + (panoramaWidth - width - 2.f * margin - 4.f) * static_cast<float>(distance / (distance + 5500.0));
    constexpr float surface = 480.f;
    textured.clear();
    panoramaStrip(textured, size, scene, -margin, 298.f, pan, 0.f, 0.445f);
    panoramaStrip(textured, size, scene, 298.f, surface, pan, 0.445f, 0.84f);
    for (float y = surface; y < height + margin; y += 5.f)
    {
        const float bottom = std::min(y + 5.f, height + margin);
        const float depth = (y - surface) / (height + margin - surface);
        const float nextDepth = (bottom - surface) / (height + margin - surface);
        const float ripple = depth * 1.8f * std::sin(time * 2.1f + y * .19f);
        panoramaStrip(textured, size, scene, y, bottom, pan + ripple, 0.84f + 0.16f * depth, 0.84f + 0.16f * nextDepth);
    }
    for (int i = 0; i < 2; ++i)
    {
        const bool nearBank = i == 1;
        const float speed = nearBank ? .61f : .29f, period = nearBank ? 1650.f : 1280.f;
        const float x = wrap(625.f + level * 43.f + i * 535.f - distance * speed + 140.f, period) - 140.f;
        if (x > width + 140.f)
            continue;
        const bool airborne = level == 9;
        const float y =
            (nearBank ? 518.f : 466.f) + std::sin(time * (airborne ? .48f : 1.1f) + i) * (airborne ? 4.f : 1.f);
        if (!airborne)
            drawLandform(textured, size, scene, x, y, nearBank, true, time);
        drawLandform(textured, size, scene, x, y, nearBank, false, time);
    }
    target.draw(textured.data(), textured.size(), sf::PrimitiveType::Triangles, sf::RenderStates(&horizons));

    details.clear();
    for (int i = 0; i < 57; ++i)
    {
        const float depth = ((i * 17) % 61) / 60.f;
        const float y = 492.f + 48.f * depth;
        const float x = wrap(i * 319.47 - distance * (.24f + .69f * depth), 1010.f) - 25.f;
        const float pulse = .5f + .5f * std::sin(time * (1.1f + depth) + i);
        box(details, x, y, 2.f + depth * (5.f + 8.f * pulse), 1.f,
            alpha(palette.rim, static_cast<unsigned>(23.f + 42.f * pulse)));
    }
    atmosphere(details, level, palette, distance, time);
    target.draw(details.data(), details.size(), sf::PrimitiveType::Triangles);
}
