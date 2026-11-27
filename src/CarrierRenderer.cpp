#include "CarrierRenderer.hpp"
#include "Combat.hpp"

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
using Vec = sf::Vector2f;
using Color = sf::Color;
using Batch = std::vector<sf::Vertex>;
constexpr Color shadow{24, 34, 48}, steel{81, 111, 123}, metal{154, 173, 167};
constexpr Color brass{219, 153, 76}, amber{252, 213, 122}, cyan{111, 220, 220};
constexpr float lightsOn = 1.65f;
constexpr float lensOffset = 9.f;
struct DeckFire { float x, size; };
constexpr std::array<DeckFire, 4> deckFires{{{69.f, 1.12f}, {158.f, 1.52f}, {202.f, 0.86f}, {291.f, 1.28f}}};

Color light(Color c, float amount)
{
    c.a = static_cast<std::uint8_t>(255.f * std::clamp(amount, 0.f, 1.f));
    return c;
}

void quad(Batch& batch, sf::Vertex a, sf::Vertex c, sf::Vertex d, sf::Vertex e)
{
    batch.insert(batch.end(), {a, c, d, a, d, e});
}

void box(Batch& batch, Vec position, float w, float h, Color color)
{
    position = {std::round(position.x), std::round(position.y)};
    quad(batch, {position, color}, {position + Vec{w, 0}, color}, {position + Vec{w, h}, color},
         {position + Vec{0, h}, color});
}

void stroke(Batch& batch, Vec a, Vec z, float halfWidth, Color color)
{
    const auto delta = z - a;
    if (delta.lengthSquared() < 0.01f)
        return;
    const auto side = delta.perpendicular().normalized() * halfWidth;
    quad(batch, {a - side, color}, {a + side, color}, {z + side, color}, {z - side, color});
}

void draw(sf::RenderTarget& target, const Batch& batch, sf::RenderStates states = {})
{
    if (!batch.empty())
        target.draw(batch.data(), batch.size(), sf::PrimitiveType::Triangles, states);
}

Vec lightDirection(const CarrierRaid& raid, unsigned index)
{
    const float angle = -1.83f + 0.87f * std::sin(raid.age * 0.79f + index * 2.65f);
    const Vec sweep{std::cos(angle), std::sin(angle)};
    if (raid.phase != CarrierPhase::Battle)
        return sweep;
    const Vec position = raid.mount(index ? CarrierPart::AftLight : CarrierPart::ForwardLight);
    const auto aim = raid.salvoTarget - position;
    if (aim.lengthSquared() < 1.f)
        return sweep;
    const float tracking = std::clamp(raid.salvoCharge * 2.4f, 0.f, 1.f);
    const auto dir = (1.f - tracking) * sweep + tracking * aim.normalized();
    return dir.lengthSquared() > 0.001f ? dir.normalized() : sweep;
}

void lampBeam(Batch& batch, Vec position, Vec dir, float power)
{
    const Vec side = dir.perpendicular();
    for (int band = 0; band < 2; ++band)
    {
        const float wide = band ? 29.f : 68.f;
        const Color close = light({229, 222, 170}, power * (band ? 0.16f : 0.105f));
        const Color mid = light({172, 215, 226}, power * (band ? 0.095f : 0.065f));
        const Color far = light({160, 198, 222}, 0.f);
        const Vec a = position - side * 2.f, z = position + side * 2.f;
        const Vec m = position + dir * 225.f, e = position + dir * 770.f;
        quad(batch, {a, close}, {z, close}, {m + side * (wide * 0.36f), mid}, {m - side * (wide * 0.36f), mid});
        quad(batch, {m - side * (wide * 0.36f), mid}, {m + side * (wide * 0.36f), mid}, {e + side * wide, far},
             {e - side * wide, far});
    }
}

void reflection(Batch& batch, Vec position, float length, float width, Color tint, float t, float power)
{
    for (float d = 1.f; d < length; d += 2.7f)
    {
        const float wave = std::sin(d * 1.58f + position.x * 0.017f - t * 2.5f);
        const float fade = 1.f - d / length;
        const float span = 1.f + width * (0.6f + 0.36f * wave) * (0.5f + d / length);
        box(batch, {position.x + wave * (1.f - fade) * width * 0.66f - span, position.y + d}, 2.f * span, 1.f,
            light(tint, power * fade));
    }
}

void deckFire(Batch& batch, Vec base, float time, float strength)
{
    // Stagger rising ash and smoke so each breach has its own silhouette.
    for (int i = 0; i < 5; ++i)
    {
        const float rise = std::fmod(time * 0.54f + i * 0.2f, 1.f);
        const float radius = (5.f + 11.f * rise) * strength;
        const Vec center = base + Vec{rise * (31.f + 4.f * std::sin(time + i)), -(11.f + 64.f * rise) * strength};
        const Color smoke = light({89, 73, 85}, (1.f - rise) * 0.76f * strength);
        box(batch, center - Vec{radius, radius * 0.45f}, radius * 2.f, radius * 0.9f, smoke);
        box(batch, center - Vec{radius * 0.63f, radius * 0.8f}, radius * 1.26f, radius * 1.6f, smoke);
        const float spark = std::fmod(time * 0.83f + i * 0.22f, 1.f);
        box(batch, base + Vec{(i - 2) * 3.7f + spark * 18.f, -(8.f + 39.f * spark) * strength},
            2.f, 3.f, light(amber, (1.f - spark) * strength));
    }
    // Pixel terraces taper to a windblown tip. Each tongue varies in height and phase.
    for (int i = -2; i <= 2; ++i)
    {
        const float flicker = std::sin(time * 9.f + i * 2.4f);
        const float height = (33.f - 7.f * std::abs(i) + 5.f * flicker) * strength;
        const float step = height / 6.f;
        for (int j = 0; j < 6; ++j)
        {
            const float up = j / 6.f;
            const float x = base.x + i * 4.f + up * (5.f + 3.f * flicker);
            const float half = (5.f - 4.f * up) * strength;
            const float y = base.y - (j + 1) * step;
            box(batch, {x - half, y}, 2.f * half + 1.f, std::ceil(step) + 1.f, {226, 75, 44});
            if (j < 5)
                box(batch, {x - half * 0.64f, y + 1.f}, half + 1.f, std::ceil(step),
                    j < 2 ? Color{255, 235, 149} : Color{255, 162, 56});
        }
    }
}

void deckFighter(Batch& batch, Vec position, float heat)
{
    // Nose points down the launch rail, with folding wings and a copper canopy.
    box(batch, position + Vec{-18, 0}, 33, 4, shadow);
    box(batch, position + Vec{-12, -2}, 27, 4, steel);
    box(batch, position + Vec{-18, 0}, 15, 1, metal);
    box(batch, position + Vec{-7, -3}, 8, 2, amber);
    box(batch, position + Vec{-3, -5}, 5, 3, {91, 185, 209});
    box(batch, position + Vec{3, -1}, 10, 2, {128, 147, 158});
    stroke(batch, position + Vec{0, 2}, position + Vec{8, -7}, 1.2f, steel);
    box(batch, position + Vec{8, -9}, 3, 4, light(cyan, 0.75f));
    box(batch, position + Vec{12, -5}, 3, 5, metal);
    box(batch, position + Vec{15, 0}, 3.f + 6.f * heat, 2, light(amber, 0.45f + heat * 0.55f));
}

void spotlightHead(Batch& batch, Vec position, Vec dir, float baseY, bool broken, bool hit)
{
    box(batch, {position.x - 4.f, position.y + 6.f}, 8, std::max(1.f, baseY - position.y - 6.f), steel);
    stroke(batch, {position.x - 8, baseY}, {position.x - 1, position.y + 5}, 1.6f, shadow);
    box(batch, {position.x - 7, baseY - 1}, 14, 3, brass);
    if (broken)
    {
        stroke(batch, {position.x - 4, position.y + 9}, {position.x + 6, position.y + 5}, 1.8f, shadow);
        box(batch, position + Vec{2, 7}, 3, 2, {161, 85, 52});
        return;
    }
    const Vec tip = position + dir * lensOffset;
    stroke(batch, position - dir * 5.f, tip, 5.2f, hit ? Color::White : shadow);
    stroke(batch, position - dir * 4.f, tip, 3.4f, hit ? Color::White : steel);
    stroke(batch, tip - dir * 2.f, tip + dir, 4.f, amber);
    box(batch, position + Vec{-2, 2}, 4, 3, brass);
}
}

void drawCarrierSky(sf::RenderTarget& target, const Combat& combat)
{
    const auto& raid = combat.carrierRaid();
    const bool intro = raid.phase == CarrierPhase::Approach;
    if (!intro && raid.phase != CarrierPhase::Battle)
        return;
    Batch batch;
    const float power = intro ? std::clamp((raid.age - lightsOn) / 0.95f, 0.f, 1.f) : 0.82f;
    for (unsigned i = 0; i < 2; ++i)
    {
        if (raid.destroyed[1 + i])
            continue;
        const Vec position = raid.mount(i ? CarrierPart::AftLight : CarrierPart::ForwardLight);
        const Vec direction = lightDirection(raid, i);
        lampBeam(batch, position + direction * lensOffset, direction, power);
    }
    if (!intro && raid.salvoCharge >= raid.lockAt())
    {
        const float fade = std::clamp(0.35f + (raid.salvoCharge - raid.lockAt()) * 6.f, 0.f, 1.f);
        const Color sight = light({255, 148, 111}, fade * 0.85f);
        const Vec aim = raid.salvoTarget;
        for (int i = 0; i < 2; ++i)
        {
            const Vec position = raid.deckGun(i), ray = aim - position;
            const float distance = ray.length();
            if (distance < 1.f)
                continue;
            const Vec dir = ray / distance;
            for (float x = 13.f; x < distance - 9.f; x += 25.f)
                stroke(batch, position + dir * x, position + dir * std::min(x + 7.f, distance - 9.f), 0.65f,
                       light(sight, 0.26f * fade));
        }
        for (const Vec dir : {Vec{1, 0}, Vec{-1, 0}, Vec{0, 1}, Vec{0, -1}})
            stroke(batch, aim + dir * 5.f, aim + dir * 11.f, 1.f, sight);
    }
    draw(target, batch);
}

void drawCarrier(sf::RenderTarget& target, const Combat& combat, const sf::Texture& hull)
{
    const auto& raid = combat.carrierRaid();
    const bool intro = raid.phase == CarrierPhase::Approach, sinking = raid.phase == CarrierPhase::Sinking;
    if (!intro && !sinking && raid.phase != CarrierPhase::Battle)
        return;
    // Bring the hull and lamp towers out of the blackout before their first beams appear.
    constexpr float silhouetteFade = 0.9f;
    const float reveal = intro ? std::clamp(1.f + (raid.age - lightsOn) / silhouetteFade, 0.f, 1.f) : 1.f;
    if (reveal <= 0.f)
        return;
    const float sink = sinking ? std::min(1.f, raid.age / CarrierRaid::sinkDuration) : 0.f;
    const Vec origin = raid.position - CarrierRaid::spriteOrigin;
    const float water = raid.position.y + 17.f;
    const auto drop = [&](float x) { return 124.f * sink * sink + (raid.position.x - x) * 0.094f * sink; };
    const float burn = raid.deckBurning()
        ? (1.f - sink) * (1.f - 0.65f * raid.health / raid.maxHealth) : 0.f;
    Batch batch;
    for (const auto x : {26.f, 176.f, 319.f})
        reflection(batch, {origin.x + x, water}, 23, 7, amber, combat.elapsed(), reveal * (1.f - sink) * 0.72f);
    if (burn > 0.f)
        for (const auto fire : deckFires)
            reflection(batch, {origin.x + fire.x, water}, 26, 18 * fire.size, {255, 117, 55}, combat.elapsed(), burn);
    for (int i = 0; i < 12; ++i)
    {
        const float drift = std::fmod(i * 10.f + combat.elapsed() * 28.f, 121.f);
        box(batch, {origin.x + 318.f + drift, water - 4 + (i % 3) * 3.f + drift * .055f},
            (1.f - drift / 121.f) * 17.f + 2.f, 1.f,
            light({150, 208, 208}, reveal * (1.f - sink) * (0.50f - drift * .003f)));
    }
    draw(target, batch);
    batch.clear();
    const float illumination = intro ? 0.40f + 0.40f * std::max(0.f, 1.f - std::abs(raid.age - 6.f))
                                     : std::clamp(0.40f + raid.age * 0.75f, 0.40f, 1.f);
    Color tint{static_cast<std::uint8_t>(226.f * illumination), static_cast<std::uint8_t>(235.f * illumination),
               static_cast<std::uint8_t>(250.f * illumination), light(Color::White, reveal).a};
    if (sinking)
        tint = {175, 185, 197};
    const auto column = [&](float u) {
        const float x = origin.x + u;
        const float top = std::min(water, origin.y + drop(x));
        const float visible = std::min(104.f, water - top);
        return std::array<sf::Vertex, 2>{{{{x, top}, tint, {u, 0}}, {{x, top + visible}, tint, {u, visible}}}};
    };
    const auto left = column(0.f), right = column(352.f);
    quad(batch, left[0], right[0], right[1], left[1]);
    draw(target, batch, sf::RenderStates(&hull));
    if (raid.hitFlash > 0.f)
    {
        sf::RenderStates flash(&hull);
        flash.blendMode = sf::BlendAdd;
        for (auto& v : batch)
            v.color = light(Color::White, raid.hitFlash * 9.f);
        draw(target, batch, flash);
    }
    batch.clear();
    if (burn > 0.f)
        for (const auto fire : deckFires)
            deckFire(batch, origin + Vec{fire.x, 48.f}, combat.elapsed() + fire.x * 0.13f, burn * fire.size);
    const Vec cat = raid.mount(CarrierPart::Catapult);
    const auto* catTarget = combat.carrierPart(CarrierPart::Catapult);
    const bool catHit = catTarget && catTarget->hitFlash > 0.045f;
    box(batch, cat + Vec{-21, 4}, 66, 3, shadow);
    if (!raid.destroyed[0])
    {
        box(batch, cat + Vec{-16, -2}, 23, 6, catHit ? Color::White : brass);
        box(batch, cat + Vec{-13, -4}, 16, 3, catHit ? Color::White : steel);
        box(batch, cat + Vec{-11, -2}, 8, 2, cyan);
        for (int i = 0; i < 8; ++i)
            box(batch, cat + Vec{-17.f + i * 8, 3}, 4, 1, i % 2 ? metal : brass);
        for (int i = raid.queuedFighters - 1; i >= 0; --i)
            deckFighter(batch, raid.readyFighter(static_cast<unsigned>(i)),
                        i == 0 ? std::clamp(1.f - raid.launchTimer, 0.f, 1.f) : 0.f);
    }
    else
    {
        box(batch, cat + Vec{-10, 1}, 5, 4, steel);
        box(batch, cat + Vec{4, 2}, 8, 3, shadow);
        box(batch, cat + Vec{-2, 3}, 4, 1, {227, 106, 53});
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto part = i ? CarrierPart::AftLight : CarrierPart::ForwardLight;
        const auto* targetPart = combat.carrierPart(part);
        spotlightHead(batch, raid.mount(part), lightDirection(raid, i), origin.y + (i ? 31.f : 49.f),
                      raid.destroyed[i + 1], targetPart && targetPart->hitFlash > 0.045f);
        const Vec position = raid.deckGun(i);
        const auto delta = raid.salvoTarget - position;
        const Vec dir = delta.lengthSquared() > 1.f ? delta.normalized() : Vec{-1, 0};
        stroke(batch, position, position + dir * 13.f, 2.f, steel);
        box(batch, position + Vec{-6, -2}, 12, 6, shadow);
        box(batch, position + Vec{-5, -3}, 9, 3, metal);
        if (raid.phase == CarrierPhase::Battle && raid.salvoCharge > 0.70f)
            box(batch, position + dir * 13.f - Vec{1, 1}, 3, 3, light({255, 146, 112}, raid.salvoCharge));
    }
    for (auto& v : batch)
    {
        v.position.y = std::min(water, v.position.y + drop(v.position.x));
        if (intro)
            v.color = v.color * tint;
    }
    draw(target, batch);
}
