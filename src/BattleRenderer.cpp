#include "BattleRenderer.hpp"
#include "CarrierRenderer.hpp"
#include "Combat.hpp"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace
{
	using Batch = std::vector<sf::Vertex>;
	using Vec = sf::Vector2f;
	using Color = sf::Color;

	constexpr float sceneWidth = 960.f;
	constexpr float tau = 6.2831853f;

	void drawBatch(sf::RenderTarget& target, const Batch& vertices,
		const sf::RenderStates& states = sf::RenderStates::Default)
	{
		if (!vertices.empty())
			target.draw(vertices.data(), vertices.size(), sf::PrimitiveType::Triangles, states);
	}

	void quad(Batch& batch, sf::Vertex a, sf::Vertex b, sf::Vertex c, sf::Vertex d)
	{
		batch.insert(batch.end(), { a, b, c, a, c, d });
	}

	void rectangle(Batch& batch, float left, float top, float width, float height, Color color)
	{
		const float right = std::round(left + width);
		const float bottom = std::round(top + height);
		left = std::round(left);
		top = std::round(top);
		quad(batch, { { left, top }, color }, { { right, top }, color },
			{ { right, bottom }, color }, { { left, bottom }, color });
	}

	void streak(Batch& batch, Vec from, Vec to, Vec direction, float halfWidth, Color color)
	{
		const Vec side{ -direction.y * halfWidth, direction.x * halfWidth };
		quad(batch, { from + side, color }, { to + side, color },
			{ to - side, color }, { from - side, color });
	}

	void bolt(Batch& batch, Vec from, Vec to, Vec direction, float radius, Color color)
	{
		const Vec side{ -direction.y * radius, direction.x * radius };
		const Vec back = from + direction * radius;
		const Vec front = to - direction * radius;
		quad(batch, { back + side, color }, { front + side, color },
			{ front - side, color }, { back - side, color });
		batch.insert(batch.end(), { { front + side, color },{ to, color },{ front - side, color },
								   { from, color },{ back + side, color },{ back - side, color } });
	}

	// Five horizontal bands give little clouds and fireballs a stepped arcade silhouette.
	void puff(Batch& batch, Vec center, float radius, Color color)
	{
		const std::array<float, 5> halfWidths{ 0.43f, 0.78f, 1.f, 0.78f, 0.43f };
		for (std::size_t band = 0; band < halfWidths.size(); ++band)
		{
			const float halfWidth = radius * halfWidths[band];
			const float top = center.y - radius + static_cast<float>(band) * 0.4f * radius;
			rectangle(batch, center.x - halfWidth, top, 2.f * halfWidth, radius * 0.4f, color);
		}
	}

	Color faded(Color color, float opacity)
	{
		color.a = static_cast<std::uint8_t>(255.f * std::clamp(opacity, 0.f, 1.f));
		return color;
	}

	float sample(unsigned& state)
	{
		state = state * 1664525u + 1013904223u;
		return static_cast<float>(state >> 8) / 16777216.f;
	}

	float wrap(double value, float period)
	{
		return static_cast<float>(value - period * std::floor(value / period));
	}

	void lampReflection(Batch& batch, float x, float waterline, float length, float width,
		Color tint, float strength, float time, float phase)
	{
		if (strength <= 0.f || x < -2.f * width || x > sceneWidth + 2.f * width)
			return;

		// Each row catches a different wave crest. Their lateral sway widens toward the viewer.
		for (float depth = 1.f; depth < length; depth += 2.f + 3.f * depth / length)
		{
			const float t = depth / length;
			const float wave = std::sin(phase + time * 2.7f - depth * 0.48f);
			const float glint = 0.5f + 0.5f * std::cos(time * 1.9f + phase + depth * 1.13f);
			const float middle = x + wave * t * width * 0.72f;
			const float span = (0.8f + width * (0.28f + 0.82f * t)) * (0.45f + 0.75f * glint);
			const float light = strength * (1.f - t) * (0.26f + 0.65f * glint);
			rectangle(batch, middle - span, waterline + depth, 2.f * span, 1.f, faded(tint, light));
		}
	}

	void gunshipFlash(Batch& batch, const Explosion& effect)
	{
		const float remaining = 1.f - effect.age / 0.38f;
		if (remaining <= 0.f)
			return;

		const Vec center = effect.position;
		const float light = remaining * remaining;
		const Color glow = faded({ 255, 193, 143 }, light * 0.58f);
		const Color rim = faded(glow, 0.f);
		const float radius = 108.f + (1.f - remaining) * 28.f;
		constexpr int sides = 16;
		Vec edge = center + Vec{ radius, 0.f };
		for (int i = 1; i <= sides; ++i)
		{
			const float angle = tau * i / sides;
			const Vec next = center + Vec{ radius * std::cos(angle), radius * 0.68f * std::sin(angle) };
			batch.insert(batch.end(), { { center, glow }, { edge, rim }, { next, rim } });
			edge = next;
		}

		// Keep each channel fixed as its light fades, even across several rendered frames.
		unsigned noise = effect.seed;
		const float side = center.x < sceneWidth * 0.5f ? 1.f : -1.f;
		const float vertical = center.y < 138.f ? 1.f : -1.f;
		Vec joint = center;
		for (int i = 1; i <= 5; ++i)
		{
			const Vec next = center + Vec{ side * (i * 9.f + (sample(noise) - 0.5f) * 43.f),
				vertical * (i * 19.f + sample(noise) * 8.f) };
			const Vec direction = (next - joint).normalized();
			streak(batch, joint, next, direction, 3.2f, faded({ 131, 192, 226 }, light * 0.46f));
			streak(batch, joint, next, direction, 1.2f, faded({ 244, 253, 246 }, remaining));
			if (i == 3)
			{
				const Vec elbow = joint + Vec{ -side * 21.f, vertical * 11.f };
				const Vec tip = elbow + Vec{ side * 4.f, vertical * 22.f };
				const Color spur = faded({ 192, 228, 238 }, light);
				streak(batch, joint, elbow, (elbow - joint).normalized(), 1.f, spur);
				streak(batch, elbow, tip, (tip - elbow).normalized(), 0.8f, spur);
			}
			joint = next;
		}
	}

	constexpr float coastWidth = 1536.f;
	constexpr float coastMargin = Combat::gunshipShakeAmplitude + 1.f;
	constexpr float skyTravel = 0.014f;
	constexpr float townTravel = 0.26f;
	constexpr float harbourTravel = 0.44f;

	float nightfall(float elapsed)
	{
		const float t = std::clamp((elapsed - 8.f) / 132.f, 0.f, 1.f);
		return t * t * (3.f - 2.f * t);
	}

	Color nightTint(Color moonlit, float night)
	{
		const auto channel = [night](std::uint8_t value) {
			return static_cast<std::uint8_t>(255.f + night * (value - 255.f));
		};
		return { channel(moonlit.r), channel(moonlit.g), channel(moonlit.b) };
	}

	// All panoramas tile across the atlas and extend past the sides of the shaken view.
	void coastStrip(Batch& batch, float top, float bottom, float atlasTop, float atlasBottom, double scroll,
		Color tint = Color::White)
	{
		const float left = -coastMargin, right = sceneWidth + coastMargin;
		const float u = std::floor(wrap(scroll, coastWidth));
		quad(batch, { { left, top }, tint, { u + left, atlasTop } },
			{ { right, top }, tint, { u + right, atlasTop } },
			{ { right, bottom }, tint, { u + right, atlasBottom } },
			{ { left, bottom }, tint, { u + left, atlasBottom } });
	}

	void nightSky(Batch& batch, float night)
	{
		if (night == 0.f) return;
		const float left = -coastMargin, right = sceneWidth + coastMargin;
		const Color afterglow = faded({ 254, 131, 91 }, 0.8f * night * (1.f - night));
		const Color clearGlow = faded(afterglow, 0.f);
		quad(batch, {{left, 230.f}, clearGlow}, {{right, 230.f}, clearGlow},
			{{right, 427.f}, afterglow}, {{left, 427.f}, afterglow});
		const Color zenith = faded({ 6, 13, 34 }, night * 0.92f);
		const Color middle = faded({ 16, 29, 64 }, night * night * 0.91f);
		const Color horizon = faded({ 59, 53, 90 }, night * night * night * 0.92f);
		quad(batch, {{left, -coastMargin}, zenith}, {{right, -coastMargin}, zenith},
			{{right, 235.f}, middle}, {{left, 235.f}, middle});
		quad(batch, {{left, 235.f}, middle}, {{right, 235.f}, middle},
			{{right, 427.f}, horizon}, {{left, 427.f}, horizon});
	}

	struct Star
	{
		float x, y, phase, threshold;
	};

	void eveningStars(Batch& batch, float time, double distance, float night)
	{
		// Each star has a fixed magnitude so the same sky is revealed throughout a run.
		static const auto stars = [] {
			std::array<Star, 614> field{};
			unsigned seed = 3718;
			for (std::size_t i = 0; i < field.size(); ++i)
			{
				auto& star = field[i];
				star.x = sample(seed) * coastWidth;
				star.y = 3.f + (i < 153 ? 192.f : 344.f) * sample(seed);
				star.phase = sample(seed) * tau;
				star.threshold = i < 153 ? -1.f : sample(seed) * 0.84f;
				if (i > 410)
				{
					const float band = 136.f + 73.f * std::sin(star.x / coastWidth * tau);
					star.y = band + (sample(seed) + sample(seed) - 1.f) * 72.f;
				}
			}
			return field;
		}();
		for (std::size_t i = 0; i < stars.size(); ++i)
		{
			const Star& star = stars[i];
			if (night <= star.threshold) continue;
			const float x = wrap(star.x - distance * 0.009f + 4.f, coastWidth) - 4.f;
			if (x < -coastMargin || x > sceneWidth + coastMargin) continue;
			const float y = star.y;
			const float pulse = 0.57f + 0.43f * std::sin(time * 1.25f + star.phase);
			const float reveal = std::min(1.f, 6.f * (night - star.threshold));
			const float brightness = 0.32f + 0.55f * pulse + 0.18f * night;
			const float opacity = reveal * brightness * (1.f - y / (255.f + 685.f * night));
			const Color light = i % 4 ? Color{ 193, 224, 243 } : Color{ 255, 216, 175 };
			rectangle(batch, x, y, 1.f, 1.f, faded(light, opacity));
			if (i % 19 == 0)
			{
				rectangle(batch, x - 2.f, y, 5.f, 1.f, faded(light, opacity * 0.58f));
				rectangle(batch, x, y - 2.f, 1.f, 5.f, faded(light, opacity * 0.61f));
			}
			else if (i % 11 == 0)
				rectangle(batch, x + 1.f, y, 1.f, 1.f, faded(light, 0.76f * opacity * night));
		}
	}

	void sunsetReflection(Batch& batch, float time, float distance, float night)
	{
		const float sunlight = 1.f - std::clamp((night - 0.20f) / 0.59f, 0.f, 1.f);
		if (sunlight == 0.f) return;
		unsigned seed = 9213;
		const float sun = 708.f - distance * skyTravel;
		for (int i = 0; i < 135; ++i)
		{
			const float depth = sample(seed);
			const float drift = (sample(seed) + sample(seed) - 1.f) * (24.f + 152.f * depth);
			const float phase = sample(seed) * tau;
			const float wave = 0.5f + 0.5f * std::sin(time * (1.3f + depth) + phase);
			const float x = wrap(sun + drift + 5.f * std::sin(phase + time) + 64.f, coastWidth) - 64.f;
			const float y = 425.f + depth * 114.f;
			rectangle(batch, x, y, (2.f + depth * 20.f) * (0.28f + wave), 1.f,
				faded({ 255, 197, 132 }, sunlight * (0.14f + 0.55f * wave) * (1.f - 0.42f * depth)));
		}
	}

	void water(Batch& batch, float time, double distance, float night)
	{
		// A closer band of moving glints and navigation buoys tracks faster than the coast.
		const float glint = 1.f - 0.56f * night;
		for (unsigned i = 0; i < 68; ++i)
		{
			const float depth = static_cast<float>((i * 37) % 67) / 66.f;
			const float x = wrap(static_cast<float>(i * 367) - distance * (0.32f + 0.60f * depth),
				sceneWidth + 30.f) - 15.f;
			const float y = 435.f + depth * 104.f;
			const float twinkle = 0.5f + 0.5f * std::sin(1.6f * time + i * 2.3f);
			const float width = 2.f + depth * (4.f + 15.f * twinkle);
			rectangle(batch, x, y, width, 1.f, faded({ 151, 212, 204 }, glint * (0.14f + 0.49f * twinkle)));
			if (depth > 0.59f && twinkle > 0.65f)
				rectangle(batch, x + width, y + 1.f, width * 0.43f, 1.f,
					faded({ 139, 196, 191 }, glint * 0.33f * twinkle));
		}

		for (int i = 0; i < 2; ++i)
		{
			const float x = wrap(190.f + i * 585.f - distance * 0.78f, 1170.f) - 14.f;
			const float y = 526.f + std::round(std::sin(time * 2.5f + i * 3.f));
			rectangle(batch, x - 8.f, y + 1.f, 16.f, 2.f, { 55, 126, 138 });
			rectangle(batch, x - 5.f, y - 2.f, 10.f, 3.f, { 28, 49, 71 });
			rectangle(batch, x - 3.f, y - 4.f, 6.f, 2.f, { 209, 124, 96 });
			rectangle(batch, x - 1.f, y - 9.f, 2.f, 5.f, { 46, 62, 74 });
			const bool beacon = wrap(time + 0.6f * i, 2.4f) < 0.24f;
			rectangle(batch, x - 1.f, y - 10.f, 2.f, 2.f,
				beacon ? Color{ 255, 229, 151 } : Color{ 178, 103, 90 });
		}

		const float pier = std::round(wrap(1410.f - distance * 1.12f, 1960.f) - 165.f);
		if (pier < sceneWidth && pier + 158.f >= 0.f)
		{
			const Color timber{ 28, 42, 46 };
			for (int i = 0; i < 4; ++i)
			{
				const float post = pier + 7.f + i * 45.f;
				rectangle(batch, post, 527.f, 6.f, 13.f, timber);
				rectangle(batch, post + 1.f, 528.f, 1.f, 12.f, { 66, 75, 68 });
				streak(batch, { post + 4.f, 539.f }, { post + 24.f, 528.f },
					{ 0.88f, -0.48f }, 1.5f, timber);
			}
			rectangle(batch, pier, 524.f, 158.f, 5.f, timber);
			rectangle(batch, pier, 523.f, 158.f, 1.f, { 114, 99, 78 });
			for (float offset : {19.f, 119.f})
			{
				rectangle(batch, pier + offset, 518.f, 4.f, 5.f, timber);
				rectangle(batch, pier + offset - 2.f, 516.f, 8.f, 3.f, timber);
				rectangle(batch, pier + offset - 2.f, 516.f, 8.f, 1.f, { 125, 104, 77 });
			}
		}
	}

	constexpr Color moonlitSteel{ 140, 179, 224 };

	float lampIntensity(float night, float start)
	{
		return std::clamp((night - start) / 0.36f, 0.f, 1.f);
	}

	float fixtureX(float atlasX, float scroll)
	{
		// Match the texture's snapped offset, with room for a beam to enter before its lamp.
		return wrap(atlasX - std::floor(wrap(scroll, coastWidth)) + 64.f, coastWidth) - 64.f;
	}

	void distantHarbourLights(Batch& batch, float time, float townOffset, float night, const CarrierRaid& raid)
	{
		const float strength = lampIntensity(night, 0.17f);
		if (strength <= 0.f)
			return;

		constexpr std::array<Vec, 5> craneTips{{{164, 396}, {200, 401}, {923, 391}, {967, 398}, {1007, 402}}};
		constexpr Color amber{255, 182, 111};
		for (const auto& tip : craneTips)
		{
			const float x = fixtureX(tip.x, townOffset);
			const float light = strength * raid.harbourLight(x);
			rectangle(batch, x, tip.y, 2.f, 1.f, faded(amber, light * 0.83f));
			lampReflection(batch, x, 425.f, 9.f, 1.2f, amber, light * 0.64f, time, tip.x);
		}
		const float x = fixtureX(1200.f, townOffset);
		const float sweep = std::max(0.f, std::cos(time * 1.6f));
		const float beacon = strength * raid.harbourLight(x) * (0.20f + 0.8f * sweep * sweep * sweep);
		constexpr Color lantern{255, 229, 165};
		puff(batch, {x, 376.f}, 4.f, faded(lantern, beacon * 0.21f));
		rectangle(batch, x - 1.f, 376.f, 3.f, 1.f, faded(lantern, beacon));
		lampReflection(batch, x, 425.f, 15.f, 2.f, lantern, beacon, time, 0.f);
	}

	void harbourLights(Batch& batch, float time, float harbourOffset, float night, const CarrierRaid& raid)
	{
		if (night <= 0.f)
			return;

		// Mounts are in panorama coordinates with y measured from the top of the playfield.
		struct Fixture
		{
			Vec mount;
			Color bulb;
			float switchAt;
			int panes;
			float beamDrop;
			float reflectionLength;
		};
		constexpr Color amber{255, 181, 79}, workLight{255, 234, 168}, glass{130, 227, 234};
		constexpr Color beacon{255, 134, 72};
		static constexpr std::array<Fixture, 13> fixtures{{
			{{539, 233}, beacon,    0.05f, 1,  0, 24}, // tips of the two crane booms
			{{435, 299}, beacon,    0.15f, 1,  0, 20},
			{{662, 315}, glass,     0.19f, 4,  0, 52}, // crane cabs
			{{499, 349}, glass,     0.30f, 3,  0, 45},
			{{623, 336}, workLight, 0.34f, 1, 70, 79}, // work lamps under the cabs
			{{471, 360}, workLight, 0.46f, 1, 47, 65},
			{{223, 389}, amber,     0.23f, 5,  0, 44}, // windows and lamps along the quay
			{{737, 389}, amber,     0.41f, 5,  0, 48},
			{{187, 392}, workLight, 0.12f, 1,  0, 26},
			{{345, 394}, amber,     0.53f, 1,  0, 30},
			{{573, 374}, workLight, 0.59f, 1,  0, 32},
			{{709, 394}, amber,     0.63f, 1,  0, 31},
			{{832, 393}, workLight, 0.50f, 1,  0, 26}
		}};
		for (const auto& fixture : fixtures)
		{
			const float x = fixtureX(fixture.mount.x, harbourOffset);
			if (night <= fixture.switchAt || x > sceneWidth + 64.f)
				continue;
			const float t = std::min((night - fixture.switchAt) / 0.16f, 1.f);
			const float light = t * t * (3.f - 2.f * t) * raid.harbourLight(x);
			if (light <= 0.f)
				continue;
			const float y = fixture.mount.y;
			if (fixture.beamDrop > 0.f)
			{
				// Pools of work light spill down and left onto the loading deck.
				const float drop = fixture.beamDrop;
				const Color lens = faded(fixture.bulb, light * 0.20f);
				const Color mist = faded(fixture.bulb, 0.f);
				quad(batch, {{x - 1.f, y + 1.f}, lens}, {{x + 2.f, y + 1.f}, lens},
				     {{x - 0.12f * drop, y + drop}, mist}, {{x - 0.67f * drop, y + drop}, mist});
			}
			const float center = x + 3.5f * (fixture.panes - 1);
			const float waterline = 444.f + 0.10f * (444.f - y);
			lampReflection(batch, center, waterline, fixture.reflectionLength,
			               4.f + fixture.panes * 1.2f, fixture.bulb, light * 0.88f, time, fixture.mount.x);
			for (int i = 0; i < fixture.panes; ++i)
			{
				const float pane = x + 7.f * i;
				puff(batch, {pane, y}, fixture.beamDrop > 0.f ? 7.f : 4.f,
				     faded(fixture.bulb, 0.12f * light));
				rectangle(batch, pane - 1.f, y - 1.f, fixture.panes > 1 ? 4.f : 3.f, 2.f,
				          faded(fixture.bulb, 0.97f * light));
				rectangle(batch, pane, y - 1.f, 1.f, 1.f, faded({255, 251, 212}, light));
			}
		}
	}

	void vesselLamp(Batch& batch, Vec p, float width, Color color, float intensity,
		float waterline, float time, float phase)
	{
		if (intensity <= 0.f || p.y >= waterline)
			return;
		// The wash vanishes with the fixture as a sinking deck meets the surface.
		intensity *= std::min(1.f, (waterline - p.y) / 4.f);
		rectangle(batch, p.x - width * 0.5f - 3.f, p.y - 2.f, width + 6.f, 5.f,
		          faded(color, 0.15f * intensity));
		rectangle(batch, p.x - width * 0.5f - 1.f, p.y - 1.f, width + 2.f, 3.f,
		          faded(color, 0.32f * intensity));
		rectangle(batch, p.x - width * 0.5f, p.y, width, 2.f, faded(color, intensity));
		lampReflection(batch, p.x, waterline + 1.f, 17.f + 1.5f * width,
		               width, color, intensity, time, phase);
	}

	void patrolBoat(Batch& batch, const Enemy& boat, float night)
	{
		const Vec p{ std::round(boat.position.x), std::round(boat.position.y) };
		const Color tint = boat.hitFlash > 0.f ? Color::White : nightTint(moonlitSteel, night);
		const auto part = [&](float x, float y, float w, float h, Color color) {
			rectangle(batch, p.x + x, p.y + y, w, h, color * tint);
		};
		const Color dark{ 36, 31, 44 }, steel{ 79, 96, 102 }, plate{ 132, 152, 151 };
		const Color rust{ 166, 77, 53 }, trim{ 240, 161, 90 }, glass{ 108, 201, 202 };
		// Broken foam moves astern; the bow cuts a shorter, brighter crest.
		for (int i = 0; i < 7; ++i)
		{
			const float along = wrap(i * 8.f + boat.age * 32.f, 56.f);
			const float remaining = 1.f - along / 56.f;
			const float ripple = 1.2f * std::sin(boat.age * 5.f + i);
			part(26.f + along, 7.f + ripple, 2.f + remaining * 8.f, 2.f,
				faded({ 185, 229, 213 }, remaining * 0.85f));
			part(24.f + along, 10.f + along * 0.09f - ripple, 2.f + remaining * 5.f, 1.f,
				faded({ 108, 195, 192 }, remaining));
		}
		// Stepped plating, waterline stripe and rivets follow the pointed bow.
		part(-35, -2, 66, 5, dark);
		part(-32, 3, 62, 3, dark);
		part(-28, 6, 56, 3, dark);
		part(-22, 9, 47, 2, dark);
		part(-32, -1, 61, 2, plate);
		part(-30, 1, 59, 3, boat.hitFlash > 0.f ? Color{ 255, 224, 161 } : steel);
		part(-30, 1, 13, 1, { 199, 197, 163 });
		part(13, 2, 14, 2, { 57, 72, 81 });
		part(-25, 4, 53, 3, rust);
		part(-23, 4, 50, 1, trim);
		part(-24, 7, 49, 1, { 86, 52, 53 });
		part(-21, 9, 35, 1, { 50, 81, 88 });
		for (int x = -21; x < 24; x += 9)
			part(static_cast<float>(x), 1, 2, 1, dark);
		part(-35, 3, 6, 2, { 209, 240, 222 });
		part(-31, 6, 8, 2, glass);

		// Wheelhouse, bridge windows, mast and aft exhaust.
		part(2, -15, 17, 13, dark);
		part(0, -15, 21, 2, { 212, 190, 148 });
		part(1, -13, 19, 1, steel);
		part(3, -11, 6, 4, { 76, 138, 156 });
		part(11, -11, 5, 4, { 76, 138, 156 });
		part(4, -11, 3, 2, glass);
		part(12, -11, 2, 2, glass);
		part(1, -6, 18, 4, plate);
		part(0, -3, 21, 1, steel);
		part(11, -21, 2, 8, dark);
		part(7, -19, 10, 1, plate);
		part(13, -21, 7, 3, rust);
		part(13, -21, 4, 1, trim);
		part(21, -9, 5, 7, dark);
		part(20, -10, 7, 2, rust);
		part(21, -8, 2, 5, steel);
		part(13, -6, 5, 5, rust);
		part(15, -5, 1, 2, dark);
		part(13, -6, 2, 1, { 250, 224, 168 });
		part(16, -2, 2, 1, { 250, 224, 168 });

		const auto pivot = boat.turretPivot();
		const auto muzzle = boat.turretMuzzle();
		const auto direction = boat.turretDirection();
		streak(batch, pivot, muzzle + direction, direction, 2.5f, dark);
		streak(batch, pivot, muzzle, direction, 1.3f, plate);
		streak(batch, muzzle - direction * 5.f, muzzle, direction, 1.8f, steel);
		part(-17, -7, 13, 5, dark);
		part(-15, -8, 9, 4, steel);
		part(-14, -8, 6, 1, plate);
		const float waterline = p.y + 11.f;
		const float cabin = lampIntensity(night, 0.22f);
		for (float x : { 5.f, 13.f })
			vesselLamp(batch, p + Vec{x, -10.f}, 4.f, { 255, 204, 112 }, cabin, waterline, boat.age, x);
		vesselLamp(batch, p + Vec{-28.f, -2.f}, 2.f, { 114, 239, 197 }, lampIntensity(night, 0.08f),
		           waterline, boat.age, 1.f);
		vesselLamp(batch, p + Vec{24.f, -3.f}, 2.f, { 223, 231, 246 }, lampIntensity(night, 0.36f),
		           waterline, boat.age, 3.f);
		if (boat.fireTimer <= Enemy::boatAimWarning)
		{
			const float charge = 1.f - boat.fireTimer / Enemy::boatAimWarning;
			puff(batch, muzzle, 2.f + 3.f * charge, { 247, 65, 120 });
			rectangle(batch, muzzle.x - 1.f, muzzle.y - 1.f, 2.f, 2.f, { 255, 243, 207 });
		}
	}

	void battleshipWake(Batch& batch, Vec p, float time, float strength)
	{
		for (int i = 0; i < 13; ++i)
		{
			const float drift = wrap(time * 38.f + i * 23.f, 110.f);
			const float foam = (1.f - drift / 110.f) * strength;
			const float y = p.y + 9.f + (i % 3) * 2.f + drift * 0.06f;
			rectangle(batch, p.x + 105.f + drift, y, 3.f + 16.f * foam, 2.f,
				faded({ 166, 226, 215 }, 0.85f * foam));
		}
		for (int i = 0; i < 7; ++i)
		{
			const float drift = wrap(time * 26.f + i * 19.f, 58.f);
			const float y = p.y + 3.f + 0.14f * drift + 2.f * std::sin(drift * 0.2f);
			rectangle(batch, p.x - 124.f + drift, y, 5.f + 0.12f * drift, 2.f,
				faded({ 221, 246, 222 }, strength * (1.f - drift / 78.f)));
		}
	}

	void battleshipTurret(Batch& batch, Vec p, TurretKind kind, const Enemy* turret, float time)
	{
		const auto firstVertex = batch.size();
		const auto part = [&](float x, float y, float w, float h, Color color) {
			rectangle(batch, p.x + x, p.y + y, w, h, color);
		};
		const Color ink{ 40, 37, 48 }, shade{ 79, 93, 107 }, iron{ 151, 170, 166 }, brass{ 213, 173, 106 };
		part(-15, 5, 30, 4, ink);
		part(-12, 5, 24, 1, iron);
		if (!turret)
		{
			part(-13, 1, 26, 4, shade);
			part(-10, -2, 5, 4, ink);
			part(-5, 1, 9, 3, { 148, 65, 50 });
			part(6, -4, 4, 6, ink);
			for (int i = 0; i < 3; ++i)
			{
				const float rise = wrap(time * 14.f + i * 7.f, 21.f);
				puff(batch, p + Vec{ rise * 0.38f, -rise }, 2.f + rise * 0.21f,
					faded({ 63, 53, 59 }, 0.76f * (1.f - rise / 21.f)));
			}
			part(-4, 1, 4, 1, { 254, 149, 78 });
			return;
		}

		const auto base = turret->turretPivot();
		const auto tip = turret->turretMuzzle();
		const auto dir = turret->turretDirection();
		const Vec side = dir.perpendicular();
		const auto barrel = [&](float offset, float thickness) {
			const Vec a = base + side * offset;
			const Vec b = tip + side * offset;
			streak(batch, a, b + dir, dir, thickness + 1.f, ink);
			streak(batch, a, b, dir, thickness, iron);
			streak(batch, b - dir * 5.f, b, dir, thickness + 0.4f, shade);
		};
		const Color armor = turret->hitFlash > 0.f ? Color{ 255, 239, 197 } : iron;
		Color lamp = brass;
		if (kind == TurretKind::Cannon)
		{
			barrel(0.f, 3.f);
			part(-15, -3, 29, 9, ink);
			part(-12, -8, 23, 7, ink);
			part(-11, -7, 19, 3, armor);
			part(-13, -3, 23, 5, shade);
			part(-10, -3, 6, 4, brass);
		}
		else if (kind == TurretKind::Flak)
		{
			barrel(-2.8f, 1.4f);
			barrel(2.8f, 1.4f);
			part(-10, -6, 20, 12, ink);
			part(-7, -10, 13, 6, ink);
			part(-6, -9, 11, 4, armor);
			part(-8, -3, 16, 7, shade);
			lamp = { 107, 232, 214 };
		}
		else
		{
			barrel(0.f, 2.f);
			part(-13, -5, 27, 11, ink);
			part(-11, -7, 18, 3, armor);
			part(-10, -4, 21, 6, shade);
			for (float x : { 3.f, 6.f, 9.f })
				part(x, -4, 1, 5, ink);
			lamp = { 248, 131, 104 };
		}
		part(-7, 1, 5, 2, lamp);
		const float warning = turret->aimWarning();
		if (warning > 0.f)
		{
			puff(batch, tip, 3.f + 4.f * warning, { 217, 52, 106 });
			puff(batch, tip, 1.f + 2.f * warning, { 255, 236, 174 });
		}
		if (turret->hitFlash > 0.045f)
			for (auto i = firstVertex; i < batch.size(); ++i)
				batch[i].color = Color::White;
	}

	sf::Texture whiteSilhouette(const sf::Texture& texture)
	{
		auto image = texture.copyToImage();
		const auto size = image.getSize();
		for (unsigned y = 0; y < size.y; ++y)
			for (unsigned x = 0; x < size.x; ++x)
				image.setPixel({x, y}, {255, 255, 255, image.getPixel({x, y}).a});
		return sf::Texture(image);
	}

	void drawBattleship(sf::RenderTarget& target, Batch& batch, const sf::Texture& hull,
						const sf::Texture& flashMask, const Combat& combat, const Battleship& ship, float night)
	{
		const Vec p{ std::round(ship.position.x), std::round(ship.position.y) };
		const float sinking = ship.sinking ? std::min(1.f, ship.sinkAge / Battleship::sinkDuration) : 0.f;
		battleshipWake(batch, p, ship.age, 1.f - sinking);
		drawBatch(target, batch);
		batch.clear();

		const Vec topLeft = p - Vec{ 129.f, 87.f };
		const float waterline = p.y + 21.f;
		const Color hullColor = nightTint(moonlitSteel, night);
		const auto descent = [&](float x) {
			return 121.f * sinking * sinking + (p.x - x) * 0.07f * sinking;
		};
		const Vec size(hull.getSize());
		const auto edge = [&](float u) {
			const float x = topLeft.x + u;
			const float top = std::min(waterline, topLeft.y + descent(x));
			const float visible = std::min(size.y, waterline - top);
			return std::array<sf::Vertex, 2>{
				sf::Vertex{ { x, top }, hullColor, { u, 0.f } },
				sf::Vertex{ { x, top + visible }, hullColor, { u, visible } }
			};
		};
		const auto left = edge(0.f);
		const auto right = edge(size.x);
		quad(batch, left[0], right[0], right[1], left[1]);
		drawBatch(target, batch, &hull);
		const float flash = (ship.hitFlash - 0.045f) / (Battleship::impactInterval - 0.045f);
		if (flash > 0.f)
		{
			for (auto& vertex : batch)
				vertex.color = faded(Color::White, flash);
			drawBatch(target, batch, &flashMask);
		}
		batch.clear();

		const auto deckLamp = [&](float x, float y, float width, Color color, float start)
		{
			const float worldX = topLeft.x + x;
			const float strength = lampIntensity(night, start) * (1.f - sinking);
			vesselLamp(batch, {worldX, topLeft.y + y + descent(worldX)}, width, color,
			           strength, waterline, ship.age + ship.sinkAge, x * 0.14f);
		};
		deckLamp(7.f, 79.f, 3.f, { 116, 241, 195 }, 0.08f);
		deckLamp(243.f, 78.f, 3.f, { 222, 231, 247 }, 0.14f);
		for (float x : {118.f, 127.f, 136.f, 143.f})
			deckLamp(x, 34.f, 4.f, { 255, 220, 144 }, 0.18f + (x - 118.f) * 0.008f);
		for (float x : {101.f, 113.f, 126.f, 139.f, 151.f, 164.f})
			deckLamp(x, 60.f, 4.f, { 247, 183, 101 }, 0.25f + (x - 101.f) * 0.004f);
		// These fixtures already follow the hull's tilt. Only the smoke and guns below
		// still need the sinking transform.
		const auto firstDeckVertex = batch.size();
		for (int i = 0; i < 4; ++i)
		{
			const float lift = wrap(ship.age * 10.f + i * 4.5f, 18.f);
			puff(batch, p + Vec{ 38.f + 0.9f * lift, -55.f - lift }, 2.f + lift * 0.24f,
				faded({ 61, 61, 72 }, 0.63f * (1.f - lift / 18.f)));
		}
		for (auto kind : { TurretKind::Cannon, TurretKind::Flak, TurretKind::Burst })
			battleshipTurret(batch, ship.mountPosition(kind), kind, combat.bossTurret(kind), ship.age);
		if (ship.sinking)
		{
			for (auto i = firstDeckVertex; i < batch.size(); ++i)
			{
				auto& pos = batch[i].position;
				pos.y = std::min(waterline, pos.y + descent(pos.x));
			}
		}
		drawBatch(target, batch);
		batch.clear();
	}

	void hunterSignal(Batch& batch, const Enemy& enemy)
	{
		const float heading = enemy.velocity.x > 0.f ? 1.f : -1.f;
		const Vec p = enemy.position;
		if (p.x < 0.f || p.x > sceneWidth)
		{
			if (enemy.flight == FlightPath::Breakaway)
				return;
			const float edge = heading > 0.f ? 0.f : sceneWidth;
			const float pulse = 0.65f + 0.35f * std::cos(enemy.age * 14.f);
			for (float inset : { 4.f, 12.f })
			{
				const Vec tip{ edge + heading * (inset + 6.f), p.y };
				for (float y : {-6.f, 6.f})
				{
					const Vec start = tip + Vec{ -heading * 6.f, y };
					const Vec direction = (tip - start).normalized();
					streak(batch, start, tip, direction, 2.5f, { 51, 28, 48 });
					streak(batch, start, tip, direction, 1.3f, faded({ 255, 126, 188 }, pulse));
				}
			}
			return;
		}

		const Vec exhaust = p - Vec{ heading * enemy.radius * 1.33f, enemy.velocity.y * 0.021f };
		const float plume = 10.f + 3.f * std::sin(enemy.age * 33.f);
		batch.insert(batch.end(), { { exhaust - Vec{ heading * plume, 0.f }, { 224, 69, 151, 180 } },
									{ exhaust - Vec{ 0.f, 3.f }, { 255, 204, 151 } },
									{ exhaust + Vec{ 0.f, 3.f }, { 255, 224, 172 } } });
	}

	void enemyFrame(Batch& batch, const Enemy& enemy, Color tint)
	{
		constexpr float frameWidth = 160.f;
		constexpr float frameHeight = 96.f;
		const float u = static_cast<int>(enemy.kind) * frameWidth;
		const float scale = enemy.radius * 3.35f / frameWidth;
		const bool facingRight = enemy.velocity.x > 0.f;
		const float angle = std::clamp((facingRight ? 1.f : -1.f) * enemy.velocity.y * 0.001f, -0.15f, 0.15f);
		const Vec across{ scale * std::cos(angle), scale * std::sin(angle) };
		const Vec down{ -across.y, across.x };
		const Vec left = enemy.position - across * (frameWidth * 0.5f);
		const Vec right = enemy.position + across * (frameWidth * 0.5f);
		const Vec halfHeight = down * (frameHeight * 0.5f);
		const float leftU = facingRight ? u + frameWidth : u;
		const float rightU = facingRight ? u : u + frameWidth;
		quad(batch, { left - halfHeight, tint,{ leftU, 0.f } },
			{ right - halfHeight, tint,{ rightU, 0.f } },
			{ right + halfHeight, tint,{ rightU, frameHeight } },
			{ left + halfHeight, tint,{ leftU, frameHeight } });
	}

	void guidanceReticle(Batch& batch, const Enemy& enemy)
	{
		const Vec center{ std::round(enemy.position.x), std::round(enemy.position.y) };
		const float halfWidth = std::round(enemy.radius * 1.6f) + 3.f;
		const float halfHeight = std::round(enemy.radius * 0.75f) + 3.f;
		const float healthFraction = static_cast<float>(enemy.health) / enemy.maxHealth();
		const bool critical = healthFraction <= 0.25f || enemy.health == 1;
		Color signal{ 132, 247, 142 };
		if (critical)
			signal = { 255, 86, 76 };
		else if (healthFraction <= 0.60f)
			signal = { 255, 199, 84 };
		const float opacity = critical && wrap(enemy.age, 0.44f) >= 0.24f ? 0.38f : 1.f;
		signal = faded(signal, opacity);
		const Color ivory = faded({ 255, 243, 193 }, opacity);
		const Color ink{ 41, 31, 37 };
		// Open corners frame the wings. The dark edge keeps them legible in a blast.
		for (float x : {-halfWidth, halfWidth})
		{
			for (float y : {-halfHeight, halfHeight})
			{
				const float left = center.x + x;
				const float top = center.y + y;
				const float armX = x < 0.f ? left : left - 7.f;
				const float armY = y < 0.f ? top : top - 5.f;
				rectangle(batch, armX - 1.f, top - 1.f, 11.f, 4.f, ink);
				rectangle(batch, left - 1.f, armY - 1.f, 4.f, 9.f, ink);
				rectangle(batch, armX, top, 9.f, 2.f, signal);
				rectangle(batch, left, armY, 2.f, 7.f, signal);
				rectangle(batch, left, top, 2.f, 2.f, ivory);
			}
		}
		for (float y : {-halfHeight, halfHeight})
		{
			const float top = center.y + y - 2.f;
			rectangle(batch, center.x - 2.f, top - 1.f, 5.f, 7.f, ink);
			rectangle(batch, center.x - 1.f, top, 3.f, 5.f, ivory);
		}
	}

	void fireRateBadge(Batch& batch, const FireRatePickup& pickup)
	{
		const Vec p{ std::round(pickup.position.x), std::round(pickup.position.y) };
		const float radius = Combat::pickupRadius;
		const Color mint{ 90, 249, 208 };
		const float jet = 8.f + 3.f * std::sin(pickup.age * 12.f);
		for (float offset : {-4.f, 4.f})
			rectangle(batch, p.x + radius - 5.f, p.y + offset - 1.f, jet, 2.f, mint);

		const auto diamond = [&](float size, Color color) {
			quad(batch, { p - Vec{ size, 0.f }, color }, { p - Vec{ 0.f, size }, color },
				{ p + Vec{ size, 0.f }, color }, { p + Vec{ 0.f, size }, color });
		};
		diamond(radius + 2.f, { 14, 43, 52 });
		diamond(radius, mint);
		diamond(radius - 3.f, { 23, 82, 87 });

		const Color white{ 245, 255, 218 };
		for (float offset : {-6.f, 0.f})
		{
			const Vec tip = p + Vec{ offset + 4.f, 0.f };
			streak(batch, p + Vec{ offset, -4.f }, tip, { 0.707f, 0.707f }, 1.3f, white);
			streak(batch, tip, p + Vec{ offset, 4.f }, { -0.707f, 0.707f }, 1.3f, white);
		}
	}

	void curvedTrail(Batch& batch, const Shot& shot, float fadingAge)
	{
		const float trailAge = shot.verticalAcceleration != 0.f ? shot.age : shot.age - shot.trailHistory.front().age;
		const float history = std::min(trailAge, Combat::trailLifetime - fadingAge);
		if (history <= 0.f)
			return;

		const Color tint{ 255, static_cast<std::uint8_t>(173.f + 70.f * shot.trailPower),
			static_cast<std::uint8_t>(69.f + 124.f * shot.trailPower) };
		const float peakOpacity = 0.57f + 0.43f * shot.trailPower;
		const float halfWidth = 1.4f + 0.75f * shot.trailPower;
		const auto crossSection = [&](float lookback) {
			const auto sample = shot.trailSample(lookback);
			const float strength = 1.f - (lookback + fadingAge) / Combat::trailLifetime;
			const Vec side = sample.velocity.perpendicular().normalized() * (halfWidth * strength);
			const Color amber = faded(tint, peakOpacity * strength * strength);
			return std::array<sf::Vertex, 2>{ sf::Vertex{ sample.position - side, amber }, sf::Vertex{ sample.position + side, amber } };
		};

		constexpr int segments = 7;
		auto previous = crossSection(0.f);
		for (int i = 1; i <= segments; ++i)
		{
			const auto next = crossSection(history * i / segments);
			quad(batch, previous[0], previous[1], next[1], next[0]);
			previous = next;
		}

		if (shot.trailPower < 0.40f)
			return;

		constexpr float spacing = 0.10f;
		constexpr float lifetime = 0.24f;
		unsigned seed = shot.sparkSeed;
		const float firstSpark = spacing * (0.25f + 0.75f * sample(seed));
		const int latest = static_cast<int>(std::floor((shot.age - firstSpark) / spacing));
		for (int i = latest; i >= 0; --i)
		{
			const float lookback = shot.age - (firstSpark + i * spacing);
			const float age = lookback + fadingAge;
			if (age >= lifetime || lookback > history)
				break;

			unsigned noise = seed + static_cast<unsigned>(i) * 2654435761u;
			const float side = (noise & 1u) ? 1.f : -1.f;
			const float outwardSpeed = side * (48.f + 42.f * sample(noise));
			const auto emission = shot.trailSample(lookback);
			const Vec direction = emission.velocity.normalized();
			const Vec drift = direction * 16.f + direction.perpendicular() * outwardSpeed;
			const Vec spark = emission.position + drift * age;
			const float remaining = 1.f - age / lifetime;
			const float size = 1.f + (1.4f + shot.trailPower) * remaining;
			rectangle(batch, spark.x - size * 0.5f, spark.y - size * 0.5f, size, size,
				faded({ 255, 215, 122 }, peakOpacity * remaining));
		}
	}

	struct DroneColors
	{
		Color body, shadow, light;
	};

	constexpr std::array<DroneColors, 2> droneColors{{
		{ Color::White, { 31, 111, 146 }, { 101, 232, 249 } },
		{ { 196, 255, 168 }, { 47, 125, 75 }, { 165, 255, 124 } }
	}};

	void drawDrone(sf::RenderTarget& target, Batch& equipment, const sf::Texture& hull,
		const OrbitalDrone& drone, const DroneColors& colors, float time)
	{
		const Vec p{ std::round(drone.position.x), std::round(drone.position.y) };
		sf::Sprite body(hull);
		body.setOrigin({ 13.f, 11.f });
		body.setPosition(p);
		body.setColor(colors.body);
		target.draw(body);

		for (const float x : { -8.f, 10.f })
		{
			const float flame = 3.5f + 1.5f * std::sin(31.f * time + x);
			rectangle(equipment, p.x + x - 1.f, p.y + 7.f, 2.f, flame, colors.shadow);
			rectangle(equipment, p.x + x - 1.f, p.y + 7.f, 2.f, 2.f, colors.light);
		}
		const Vec aim = drone.direction;
		const Vec muzzle = drone.muzzle();
		streak(equipment, drone.position, muzzle, aim, 2.4f, { 30, 38, 51 });
		streak(equipment, drone.position, muzzle - aim, aim, 1.4f, { 153, 163, 158 });
		streak(equipment, muzzle - aim * 4.f, muzzle - aim, aim, 1.4f,
			drone.tracking ? colors.light : Color{ 67, 96, 111 });
		rectangle(equipment, p.x - 2.f, p.y - 1.f, 4.f, 3.f, { 52, 58, 69 });
		rectangle(equipment, p.x - 1.f, p.y - 1.f, 3.f, 2.f, colors.light);
		if (drone.muzzleFlash > 0.f)
		{
			const float reach = 2.f + 5.f * drone.muzzleFlash;
			bolt(equipment, muzzle - aim * 2.f, muzzle + aim * reach, aim, 2.4f,
				faded(colors.light, drone.muzzleFlash));
		}
	}

	void exhaust(Batch& batch, Vec head, Vec direction, float length, float width, Color tint)
	{
		const Vec tail = head - direction * length;
		const Vec edge = direction.perpendicular() * width;
		quad(batch, {head + edge, tint}, {head - edge, tint},
		     {tail - edge * 0.15f, faded(tint, 0.f)}, {tail + edge * 0.15f, faded(tint, 0.f)});
	}

	constexpr std::array<Color, 2> braidStrands{{ {101, 252, 238}, {255, 226, 119} }};

	void arsenalShield(Batch& batch, const Combat& combat)
	{
		const Vec muzzle = combat.arsenalMuzzle();
		const float phase = combat.arsenalPhase();
		const float flash = combat.arsenalShieldFlash();
		const float hit = combat.arsenalShieldHitY();
		// All bands meet at the tips. The bright outer edge follows the collision surface.
		const auto section = [&](float t) {
			const Vec edge = Combat::arsenalShieldPoint(muzzle, t);
			const float bow = 1.f - t * t;
			const float sweep = 0.5f + 0.5f * std::cos(phase * 2.f - t * 7.f);
			const float local = flash * std::max(0.f, 1.f - 3.2f * std::abs(t - hit));
			const Color rim = faded({69, 255, 151}, 0.75f + 0.23f * sweep);
			const Color core = faded({219, 255, 205}, 0.34f + 0.25f * sweep + 0.66f * local);
			return std::array<sf::Vertex, 6>{{
				{edge - Vec{3.f + 12.f * bow, 0.f}, {24, 197, 139, 0}},
				{edge - Vec{1.f + 3.f * bow, 0.f}, faded({11, 145, 122}, 0.50f + 0.40f * local)},
				{edge - Vec{0.3f + 1.2f * bow, 0.f}, rim},
				{edge, core},
				{edge + Vec{0.4f + 1.1f * bow, 0.f}, faded({14, 119, 94}, 0.70f)},
				{edge + Vec{1.f + 4.f * bow, 0.f}, {54, 239, 167, 0}}
			}};
		};
		constexpr int segments = 24;
		auto previous = section(-1.f);
		for (int i = 1; i <= segments; ++i)
		{
			const auto next = section(2.f * i / segments - 1.f);
			for (std::size_t band = 1; band < next.size(); ++band)
				quad(batch, previous[band - 1], previous[band], next[band], next[band - 1]);
			previous = next;
		}
		for (unsigned i = 0; i < braidStrands.size(); ++i)
		{
			const int side = i == 0 ? -1 : 1;
			const float charge = combat.shieldTipGlow(side);
			const Vec tip = Combat::arsenalShieldPoint(muzzle, static_cast<float>(side));
			rectangle(batch, tip.x - 1.f, tip.y - 1.f, 3.f, 2.f, faded(braidStrands[i], 0.48f));
			if (charge <= 0.f) continue;
			const Vec inner = Combat::arsenalShieldPoint(muzzle, Combat::shieldTipStart * side);
			streak(batch, tip, inner, (inner - tip).normalized(), 2.2f, faded(braidStrands[i], charge));
			puff(batch, tip, 5.f + charge * 3.f, faded(braidStrands[i], 0.22f * charge));
			rectangle(batch, tip.x - 3.f, tip.y - 1.f, 7.f, 3.f, faded(braidStrands[i], charge));
			rectangle(batch, tip.x - 1.f, tip.y - 3.f, 3.f, 7.f, faded(Color::White, charge));
		}
		if (const float windup = combat.braidWindup(); windup > 0.f)
		{
			const Vec focus = combat.braidFocus();
			for (unsigned i = 0; i < braidStrands.size(); ++i)
			{
				const Vec start = muzzle + combat.arsenalEmitterOffset(i) + Vec{4.f, 0.f};
				Vec previousPoint = start;
				for (int j = 1; j <= 14; ++j)
				{
					const float t = j / 14.f;
					const float amplitude = (start.y - focus.y) * std::cos(tau * t) +
					        (i == 0 ? 8.f : -8.f) * std::sin(tau * t);
					const Vec next{start.x + (focus.x - start.x) * t, focus.y + amplitude * (1.f - t)};
					streak(batch, previousPoint, next, (next - previousPoint).normalized(),
					       1.f + (1.f - windup) * 1.4f, braidStrands[i]);
					previousPoint = next;
				}
			}
			puff(batch, focus, 3.f + (1.f - windup) * 7.f, {209, 255, 210});
		}
		if (flash > 0.f)
		{
			const Vec impact = Combat::arsenalShieldPoint(muzzle, hit);
			puff(batch, impact, 2.f + flash * 3.4f, faded({222, 255, 218}, 0.9f * flash));
			for (const Vec dir : {Vec{0.50f, -0.87f}, Vec{0.96f, -0.28f}, Vec{0.80f, 0.60f}})
			{
				const Vec tip = impact + dir * (4.f + 12.f * (1.f - flash));
				streak(batch, tip - dir * (2.f + 3.f * flash), tip, dir, 0.85f,
				       faded({150, 255, 175}, flash));
			}
		}
	}

	constexpr std::array<Color, 2> oscillatorLight{{ {74, 248, 136}, {218, 241, 82} }};

	void drawOscillator(Batch& batch, Vec center, unsigned index, float flash)
	{
		const float x = std::round(center.x), y = std::round(center.y);
		const Color ink{45, 42, 44}, copper{202, 88, 47}, shine{255, 170, 98};
		const Color lens = oscillatorLight[index];
		// A split copper collar, two prongs, and a recessed luminous muzzle.
		rectangle(batch, x - 6.f, y - 3.f, 10.f, 7.f, ink);
		rectangle(batch, x - 4.f, y - 6.f, 6.f, 12.f, ink);
		rectangle(batch, x - 5.f, y - 2.f, 6.f, 5.f, {132, 65, 53});
		rectangle(batch, x - 4.f, y - 3.f, 3.f, 5.f, copper);
		for (const float side : {-1.f, 1.f})
		{
			rectangle(batch, x - 3.f, y + side * 4.f - 1.f, 8.f, 3.f, ink);
			rectangle(batch, x - 3.f, y + side * 4.f, 6.f, 1.f, shine);
			rectangle(batch, x, y + side * 3.f, 5.f, 1.f, copper);
		}
		rectangle(batch, x - 3.f, y - 1.f, 2.f, 2.f, {255, 196, 116});
		rectangle(batch, x, y - 1.f, 5.f, 3.f, {52, 86, 73});
		rectangle(batch, x + 2.f, y, 3.f, 1.f, lens);
		if (flash > 0.f)
		{
			const Vec tip{x + 4.f, y};
			const Vec axis{1.f, 0.f};
			exhaust(batch, tip, -axis, 3.f + 6.f * flash, 2.f, faded(lens, flash));
			rectangle(batch, tip.x, y, 1.f + 2.f * flash, 1.f, faded({246, 255, 198}, flash));
		}
	}

	// Leave room between luminous trails as the number of shots continues to grow.
	float weaponGlow(unsigned rank)
	{
		if (rank <= 10)
			return 0.1f * rank;
		const float extra = static_cast<float>(rank - 10);
		return 1.f + 0.6f * extra / (extra + 10.f);
	}

	void projectile(Batch& batch, const Shot& shot)
	{
		const Vec p = shot.position;
		const float r = shot.radius;
		const bool friendly = shot.isFriendly();
		const float power = weaponGlow(shot.weaponRank);
		const float wake = power * std::min(1.f, shot.age * 12.f);
		const Vec direction = shot.velocity != Vec{} ? shot.velocity.normalized() : Vec{ 1.f, 0.f };

		if (!friendly && shot.enemyBend != Vec{})
		{
			const float history = std::min(0.13f, shot.age);
			Vec near = p;
			for (int i = 1; i <= 2; ++i)
			{
				const Vec far = shot.enemyPastPosition(history * 0.5f * i);
				if (far != near)
					streak(batch, far, near, (near - far).normalized(), r * (i == 1 ? 0.52f : 0.3f),
					       {255, 77, 139, static_cast<std::uint8_t>(i == 1 ? 115 : 60)});
				near = far;
			}
		}

		if (shot.kind == ShotKind::Braid)
		{
			// Braid both colors around the actual trail, including turns made under guidance.
			constexpr float duration = 0.15f;
			const float history = std::min(shot.age, duration);
			std::array<Vec, 2> ends{p, p};
			for (int i = 1; i <= 24 && history > 0.f; ++i)
			{
				const float lookback = history * i / 24.f;
				const float t = lookback / duration;
				const auto sample = shot.trailSample(lookback);
				const float twist = 10.f * std::sin(0.5f * tau * t) * std::sin(2.f * tau * t - 16.f * shot.age);
				const Vec strandOffset = sample.velocity.normalized().perpendicular() * twist;
				for (unsigned s = 0; s < ends.size(); ++s)
				{
					const Vec point = sample.position + (s == 0 ? strandOffset : -strandOffset);
					const Vec along = point - ends[s];
					if (along.lengthSquared() > 0.f)
						streak(batch, ends[s], point, along.normalized(), 0.8f + 2.3f * (1.f - t),
						       faded(braidStrands[s], 1.f - t * t));
					ends[s] = point;
				}
			}
			puff(batch, p, r * 1.4f, {104, 252, 211, 100});
			bolt(batch, p - direction * 13.f, p + direction * r, direction, r, braidStrands[0]);
			bolt(batch, p - direction * 8.f, p + direction * r, direction, r * 0.57f, {255, 250, 186});
			return;
		}
		if (shot.kind == ShotKind::Salvo)
		{
			const Color tint = oscillatorLight[shot.waveSign > 0.f ? 0 : 1];
			const Vec tail = p - direction * (4.5f + 1.5f * power);
			const Vec tip = p + direction * 2.5f;
			exhaust(batch, tail, direction, (4.f + 3.f * power) * std::min(1.f, shot.age * 30.f),
			        1.5f, faded(tint, 0.65f));
			bolt(batch, tail, tip, direction, 2.f, {23, 71, 60});
			bolt(batch, tail + direction, tip, direction, 1.5f, tint);
			streak(batch, p, tip - direction, direction, 0.70f, {247, 255, 206});
			return;
		}

		if (shot.kind == ShotKind::Drone)
		{
			const auto& colors = droneColors[shot.droneIndex];
			const Vec tail = p - direction * (7.f + 4.f * power);
			if (wake > 0.f)
				exhaust(batch, tail, direction, 20.f * wake, 2.f, faded(colors.light, 0.72f));
			bolt(batch, tail, p + direction * 2.f, direction, 2.4f, colors.shadow);
			bolt(batch, tail + direction, p + direction * 2.f, direction, 1.65f, colors.light);
			streak(batch, p - direction * 3.f, p, direction, 0.85f, { 231, 255, 241 });
			return;
		}

		if (shot.kind == ShotKind::Missile)
		{
			const Vec side = direction.perpendicular();
			const Vec tail = p - direction * 11.f;
			const float flameLength = 8.f + 3.f * std::cos(51.f * shot.age) + 13.f * wake;
			for (int i = 4; i > 0; --i)
			{
				const float lookback = i * 0.035f;
				if (lookback > shot.age)
					continue;
				const auto past = shot.trailSample(lookback);
				puff(batch, past.position - past.velocity.normalized() * 12.f, 2.f + i * (0.65f + 0.26f * power),
				     faded({173, 189, 184}, 0.58f - 0.08f * i));
			}
			batch.insert(batch.end(), {
				{tail + side * 3.f, {255, 186, 72}}, {tail - direction * flameLength, {251, 87, 42}},
				{tail - side * 3.f, {255, 223, 126}},
				{p + direction * 2.f, {105, 140, 144}}, {tail + side * 7.f, {222, 97, 55}},
				{tail - side * 7.f, {157, 66, 48}}
			});
			if (wake > 0.f)
				exhaust(batch, tail, direction, flameLength * 0.75f, 2.3f, {255, 251, 185});
			bolt(batch, tail, p + direction * 10.f, direction, 4.f, {30, 49, 59});
			bolt(batch, tail + direction * 2.f, p + direction * 9.f, direction, 2.8f, {200, 223, 206});
			streak(batch, p + direction, p + direction * 4.f, direction, 2.8f, {241, 111, 59});
			return;
		}

		if (shot.kind == ShotKind::EnemyOrb)
		{
			const float pulse = 0.5f + 0.5f * std::sin(shot.age * 17.f);
			puff(batch, p, r + 2.f + pulse, { 73, 19, 61 });
			puff(batch, p, r + 0.8f, { 255, 58, 123 });
			puff(batch, p, std::max(2.f, r - 2.f), { 255, 217, 199 });
			rectangle(batch, p.x - 1.f, p.y - 2.f, 2.f, 3.f, { 255, 255, 239 });
			return;
		}

		const bool wave = shot.kind == ShotKind::PlayerWave;
		const float length = (wave ? 22.f : friendly ? 17.f : 8.f) + 6.f * power;
		const Vec tail = p - direction * length;
		const Vec head = p + direction * (friendly ? 4.f : 2.f);
		const Color shadow = friendly ? Color{ 24, 81, 82 } : Color{ 70, 17, 61 };
		const Color rim = friendly ? (wave ? Color{ 73, 244, 204 } : Color{ 255, 183, 44 })
			: Color{ 255, 53, 111 };
		const Color core = friendly ? Color{ 255, 255, 206 } : Color{ 255, 220, 219 };
		if (wake > 0.f)
			exhaust(batch, tail, direction, 26.f * wake, r * 0.8f, faded(rim, 0.66f));
		bolt(batch, tail, head, direction, r + 1.f, shadow);
		bolt(batch, tail + direction * 2.f, head - direction, direction, r, rim);
		bolt(batch, p - direction * (length * 0.52f), head - direction, direction, r * 0.48f, core);
	}

	void laserFlash(Batch& batch, const LaserPulse& pulse)
	{
		const Vec segment = pulse.end - pulse.origin;
		if (segment.lengthSquared() == 0.f)
			return;
		const Vec direction = segment.normalized();
		const float glow = 1.f - pulse.age / LaserPulse::duration;
		const float power = weaponGlow(pulse.weaponRank);
		streak(batch, pulse.origin, pulse.end, direction, 5.f + glow * (2.f + 3.f * power),
		       faded({63, 142, 255}, (0.33f + power * 0.2f) * glow));
		streak(batch, pulse.origin, pulse.end, direction, LaserPulse::radius, faded({92, 222, 255}, glow));
		streak(batch, pulse.origin, pulse.end, direction, 1.3f, faded({245, 255, 254}, glow));
		if (pulse.weaponRank > 0)
		{
			const float reach = segment.length();
			const Vec rim = direction.perpendicular() * (3.f + 2.5f * power) * glow;
			const float spacing = 88.f - 28.f * power;
			for (float d = std::fmod(pulse.age * 490.f, spacing); d < reach; d += spacing)
			{
				const Vec a = pulse.origin + direction * d;
				const Vec b = pulse.origin + direction * std::min(reach, d + 8.f + 17.f * power);
				streak(batch, a + rim, b + rim, direction, 0.9f, faded({157, 186, 255}, glow));
				streak(batch, a - rim, b - rim, direction, 0.9f, faded({152, 250, 255}, glow));
			}
		}
		puff(batch, pulse.origin, 2.f + glow * 3.f, faded({205, 255, 255}, glow));
	}

	void gunshipShockwave(Batch& batch, const Explosion& effect)
	{
		const float t = std::clamp(effect.age / effect.duration, 0.f, 1.f);
		const float remaining = 1.f - t;
		const float radius = effect.shockwaveRadius();
		const Color gold = faded({ 255, 203, 124 }, 0.82f * remaining);
		const Color edge = faded({ 255, 242, 205 }, 0.96f * remaining);
		const auto front = [&](Vec direction) {
			return std::array<sf::Vertex, 3>{
				sf::Vertex{ effect.position + direction * (radius - 5.f - 5.f * remaining), faded(gold, 0.f) },
				sf::Vertex{ effect.position + direction * (radius - 2.f), gold },
				sf::Vertex{ effect.position + direction * radius, edge }
			};
		};
		constexpr int segments = 48;
		auto previous = front({ 1.f, 0.f });
		for (int i = 1; i <= segments; ++i)
		{
			const float angle = tau * i / segments;
			const auto next = front({ std::cos(angle), std::sin(angle) });
			quad(batch, previous[0], previous[1], next[1], next[0]);
			quad(batch, previous[1], previous[2], next[2], next[1]);
			previous = next;
		}
	}

	void waveBurst(Batch& batch, const Explosion& effect)
	{
		const float t = std::clamp(effect.age / effect.duration, 0.f, 1.f);
		const float opacity = 1.f - t;
		const float radius = 5.f + (Combat::waveBlastRadius - 5.f) * (1.f - opacity * opacity);
		const Color edge = faded({ 36, 197, 190 }, opacity * 0.9f);
		const Color crest = faded({ 181, 255, 220 }, opacity);
		unsigned noise = effect.seed;
		const float phase = sample(noise) * tau;
		constexpr int segments = 16;
		for (int i = 0; i < segments; ++i)
		{
			const float angle = phase + i * tau / segments;
			const float end = angle + tau / segments;
			const Vec normal{ std::cos(angle), std::sin(angle) };
			const Vec endNormal{ std::cos(end), std::sin(end) };
			const Vec a = effect.position + normal * radius;
			const Vec b = effect.position + endNormal * radius;
			const Vec side = (b - a).normalized();
			streak(batch, a, b, side, 1.7f * opacity + 0.4f, edge);
			if (i % 2 == 0)
			{
				const Vec spark = effect.position + normal * (radius + 5.f * t);
				rectangle(batch, spark.x - 1.f, spark.y - 1.f, 3.f, 2.f, crest);
				streak(batch, a - normal * (2.f + 7.f * opacity), a, normal, 1.f, crest);
			}
		}
		const float inner = 12.f * std::max(0.f, 1.f - t * 2.7f);
		if (inner > 0.5f)
		{
			puff(batch, effect.position, inner, { 116, 244, 214 });
			puff(batch, effect.position, inner * 0.6f, { 241, 255, 212 });
		}
	}

	void burst(Batch& smoke, Batch& atlas, Batch& sparks, const Explosion& effect)
	{
		const float t = std::clamp(effect.age / effect.duration, 0.f, 1.f);
		if (t >= 1.f) return;

		const Vec center = effect.position;
		const float size = effect.kind == ExplosionKind::HullImpact ? 0.48f :
		                   effect.kind == ExplosionKind::Player ? 1.6f : 1.f;
		const float spread = t * (2.f - t);
		unsigned noise = effect.seed;

		for (int i = 0; i < 6; ++i)
		{
			const float angle = (i + sample(noise) * 0.65f) * tau / 6.f;
			const Vec direction{ std::cos(angle), std::sin(angle) };
			const float travel = size * (10.f + 20.f * sample(noise)) * spread;
			const Vec p = center + direction * travel + Vec{ -6.f * t, -12.f * t * t } * size;
			const float radius = size * (5.f + 7.f * t + 4.f * sample(noise));
			puff(smoke, p, radius, faded({ 47, 38, 57 }, (1.f - t) * 0.72f));
			puff(smoke, p - Vec{ 1.f, 2.f } * size, radius * 0.68f,
				faded({ 117, 87, 94 }, (1.f - t) * 0.55f));
		}

		constexpr float cell = 96.f;
		const float u = std::min(3, static_cast<int>(t * 4.f)) * cell;
		const float radius = std::round((31.f + 17.f * spread) * size);
		const Color tint = faded(Color::White, (1.f - t) * 5.f);
		const Vec corner{ std::round(center.x) - radius, std::round(center.y) - radius };
		const float edge = 2.f * radius;
		quad(atlas, { corner, tint,{ u, 0.f } },
			{ corner + Vec{ edge, 0.f }, tint,{ u + cell, 0.f } },
			{ corner + Vec{ edge, edge }, tint,{ u + cell, cell } },
			{ corner + Vec{ 0.f, edge }, tint,{ u, cell } });

		// Restart this stream so fragment trajectories stay fixed as the fire changes phase.
		noise = effect.seed ^ 0x5bc40ae3u;
		for (int i = 0; i < 15; ++i)
		{
			const float angle = (i + sample(noise) * 0.9f) * tau / 15.f;
			const Vec direction{ std::cos(angle), std::sin(angle) };
			const float distance = (24.f + sample(noise) * 38.f) * size * spread;
			const Vec p = center + direction * distance + Vec{ 0.f, t * t * size * 24.f };
			if (i % 4 == 0)
			{
				const Color metal = faded({ 74, 52, 63 }, 1.f - t * t);
				rectangle(sparks, p.x, p.y, size * 4.f, size * 3.f, metal);
				rectangle(sparks, p.x, p.y, size * 2.f, size, faded({ 241, 151, 86 }, 1.f - t));
			}
			else
			{
				const Color ember = faded({ 255, 154, 58 }, 1.f - t * t);
				streak(sparks, p - direction * ((5.f - 3.f * t) * size), p,
					direction, 1.15f * size, ember);
				rectangle(sparks, p.x - 1.f, p.y - 1.f, size * 2.f, size * 2.f,
					faded({ 255, 234, 133 }, 1.f - t));
			}
		}

		if (effect.age < 0.105f)
		{
			const float flash = 1.f - effect.age / 0.105f;
			const float beam = size * (9.f + 22.f * flash);
			const Color light = faded({ 255, 251, 204 }, flash);
			rectangle(sparks, center.x - beam, center.y - 1.f, 2.f * beam, 3.f * size, light);
			rectangle(sparks, center.x - 1.f, center.y - 0.65f * beam, 3.f * size, 1.3f * beam, light);
			puff(sparks, center, size * (2.f + 5.f * flash), light);
		}
	}
}

BattleRenderer::BattleRenderer(const std::filesystem::path& arcadeDirectory)
	: coastLayers(arcadeDirectory / "coast-parallax.png"), enemyAtlas(arcadeDirectory / "enemies.png"),
	burstAtlas(arcadeDirectory / "burst.png"), battleshipHull(arcadeDirectory / "battleship-hull.png"),
	battleshipFlashMask(whiteSilhouette(battleshipHull)), carrierHull(arcadeDirectory / "carrier-hull.png"),
	droneHull(arcadeDirectory / "orbital-drone.png")
{
	coastLayers.setRepeated(true);
}

void BattleRenderer::drawBackground(sf::RenderTarget& target, const Combat& combat)
{
	const float elapsedSeconds = combat.elapsed();
	const double distance = combat.distance();
	const float night = nightfall(elapsedSeconds);
	const sf::RenderStates panorama(&coastLayers);

	textured.clear();
	const float skyOffset = wrap(distance * skyTravel, coastWidth);
	const float sunsetDrop = std::round(166.f * night);
	coastStrip(textured, -coastMargin, 0.f, 0.5f, 0.5f, skyOffset);
	// Stretch only the empty sky above the solar disc so it descends behind the ridges at its original size.
	coastStrip(textured, 0.f, 204.f + sunsetDrop, 0.f, 204.f, skyOffset);
	coastStrip(textured, 204.f + sunsetDrop, 427.f, 204.f, 427.f - sunsetDrop, skyOffset);
	drawBatch(target, textured, panorama);
	geometry.clear();
	nightSky(geometry, night);
	eveningStars(geometry, elapsedSeconds, distance, night);
	drawBatch(target, geometry);

	textured.clear();
	coastStrip(textured, 8.f, 213.f, 428.f, 633.f, distance * 0.050f, nightTint({123, 144, 192}, night));
	coastStrip(textured, 0.f, 333.f, 635.f, 968.f, distance * 0.107f, nightTint({53, 75, 137}, night));
	coastStrip(textured, 314.f, 427.f, 970.f, 1083.f, distance * 0.155f, nightTint({70, 83, 140}, night));
	const float townOffset = wrap(distance * townTravel, coastWidth);
	coastStrip(textured, 331.f, 427.f, 1085.f, 1181.f, townOffset, nightTint({66, 82, 131}, night));
	const Color seaTint = nightTint({54, 96, 149}, night);
	// Each depth band moves together so individual waves retain their shape over long runs.
	for (int y = 425; y < 540; y += 3)
	{
		const float top = static_cast<float>(y);
		const float bottom = std::min(top + 3.f, 540.f);
		const float travel = y < 449 ? 0.25f : y < 485 ? 0.48f : 0.78f;
		const float ripple = (0.6f + (top - 425.f) * 0.014f) * std::sin(elapsedSeconds * 1.7f + top * 0.19f);
		coastStrip(textured, top, bottom, 1182.f + top - 425.f, 1182.f + bottom - 425.f,
			distance * travel + ripple, seaTint);
	}
	// Mirror the last few wave rows into the overscan so shaking also exposes textured water.
	coastStrip(textured, 540.f, 540.f + coastMargin, 1297.f, 1297.f - coastMargin, distance * 0.78f, seaTint);
	drawBatch(target, textured, panorama);

	geometry.clear();
	sunsetReflection(geometry, elapsedSeconds, static_cast<float>(distance), night);
	const auto& raid = combat.carrierRaid();
	distantHarbourLights(geometry, elapsedSeconds, townOffset, night, raid);
	drawBatch(target, geometry);

	textured.clear();
	const float harbourOffset = wrap(distance * harbourTravel, coastWidth);
	const Color portTint = nightTint({94, 104, 142}, night);
	coastStrip(textured, 228.f, 444.f, 1300.f, 1516.f, harbourOffset, portTint);
	for (int y = 444; y < 534; y += 5)
	{
		const float top = static_cast<float>(y);
		const float ripple = (top - 444.f) * 0.045f * std::sin(elapsedSeconds * 2.f + top * 0.28f);
		coastStrip(textured, top, top + 5.f, 1516.f + top - 444.f, 1521.f + top - 444.f,
			harbourOffset + ripple, portTint);
	}
	drawBatch(target, textured, panorama);
	geometry.clear();
	harbourLights(geometry, elapsedSeconds, harbourOffset, night, raid);
	water(geometry, elapsedSeconds, distance, night);
	if (raid.phase == CarrierPhase::Approach || raid.phase == CarrierPhase::Battle)
	{
		drawBatch(target, geometry);
		geometry.clear();
		drawCarrierSky(target, combat);
	}
	for (const auto& enemy : combat.enemies())
		if (enemy.flight == FlightPath::Pursuit || enemy.flight == FlightPath::Breakaway)
			hunterSignal(geometry, enemy);
	for (const Explosion& effect : combat.explosions())
		if (effect.kind == ExplosionKind::Gunship)
			gunshipFlash(geometry, effect);
	if (!combat.gameOver())
	{
		for (const Shot& shot : combat.shots())
			if (shot.hasTrail())
				curvedTrail(geometry, shot, 0.f);
	}
	for (const auto& trail : combat.fadingTrails())
		curvedTrail(geometry, trail.shot, trail.age);
	drawBatch(target, geometry);
}

void BattleRenderer::drawEnemies(sf::RenderTarget& target, const Combat& combat)
{
	geometry.clear();
	textured.clear();
	foreground.clear();
	const float night = nightfall(combat.elapsed());
	drawCarrier(target, combat, carrierHull);
	if (const auto& ship = combat.battleship())
		drawBattleship(target, foreground, battleshipHull, battleshipFlashMask, combat, *ship, night);
	for (const Enemy& enemy : combat.enemies())
	{
		if (enemy.kind == EnemyKind::BattleshipTurret || enemy.kind == EnemyKind::CarrierPart ||
		    enemy.kind == EnemyKind::CarrierHull)
			continue;
		if (enemy.kind == EnemyKind::PatrolBoat)
		{
			patrolBoat(foreground, enemy, night);
			continue;
		}
		enemyFrame(geometry, enemy, Color::White);
		if (enemy.hitFlash > 0.f)
			enemyFrame(textured, enemy, faded(Color::White, enemy.hitFlash / 0.09f));
	}
	sf::RenderStates states(&enemyAtlas);
	drawBatch(target, foreground);
	drawBatch(target, geometry, states);
	states.blendMode = sf::BlendAdd;
	drawBatch(target, textured, states);
}

void BattleRenderer::drawEffects(sf::RenderTarget& target, const Combat& combat)
{
	geometry.clear();
	textured.clear();
	foreground.clear();
	if (!combat.gameOver())
	{
		const auto& drones = combat.drones();
		for (std::size_t i = 0; i < drones.size(); ++i)
			drawDrone(target, geometry, droneHull, drones[i], droneColors[i], combat.elapsed());
		if (combat.weaponMode() == WeaponMode::Arsenal)
			arsenalShield(geometry, combat);
	}
	for (const Explosion& effect : combat.explosions())
	{
		if (effect.kind == ExplosionKind::Gunship)
			gunshipShockwave(geometry, effect);
		if (effect.kind == ExplosionKind::Wave)
			waveBurst(foreground, effect);
		else
			burst(geometry, textured, foreground, effect);
	}
	// Hostile rounds stay readable over every flame, shockwave and friendly shot.
	std::array<float, 2> emitterFlash{};
	for (const Shot& shot : combat.shots())
		if (shot.isFriendly())
		{
			projectile(foreground, shot);
			if (shot.kind == ShotKind::Salvo)
			{
				auto& flash = emitterFlash[shot.waveSign > 0.f ? 0 : 1];
				flash = std::max(flash, 1.f - shot.age / 0.035f);
			}
		}
	if (!combat.gameOver() && combat.weaponMode() == WeaponMode::Arsenal)
		for (unsigned i = 0; i < emitterFlash.size(); ++i)
			drawOscillator(foreground, combat.arsenalMuzzle() + combat.arsenalEmitterOffset(i), i, emitterFlash[i]);
	for (const auto& ray : combat.laserPulses())
		laserFlash(foreground, ray);
	if (const auto& pickup = combat.fireRatePickup())
		fireRateBadge(foreground, *pickup);
	if (const auto* enemy = combat.guidanceTarget())
		guidanceReticle(foreground, *enemy);
	for (const Shot& shot : combat.shots())
		if (!shot.isFriendly())
			projectile(foreground, shot);
	drawBatch(target, geometry);
	drawBatch(target, textured, &burstAtlas);
	drawBatch(target, foreground);
}
