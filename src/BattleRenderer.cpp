#include "BattleRenderer.hpp"
#include "Combat.hpp"

#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>

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

	float wrap(float value, float period)
	{
		return value - period * std::floor(value / period);
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

	void water(Batch& batch, float time)
	{
		// A closer band of moving glints and navigation buoys tracks faster than the coast.
		for (unsigned i = 0; i < 24; ++i)
		{
			const float depth = static_cast<float>((i * 37) % 31) / 30.f;
			const float x = wrap(static_cast<float>(i * 367) - time * (42.f + 38.f * depth),
				sceneWidth + 30.f) - 15.f;
			const float y = 504.f + depth * 34.f;
			const float twinkle = 0.5f + 0.5f * std::sin(1.6f * time + i * 2.3f);
			rectangle(batch, x, y, 3.f + 10.f * depth * twinkle, 1.f,
				faded({ 137, 224, 215 }, 0.15f + 0.27f * twinkle));
		}

		for (int i = 0; i < 2; ++i)
		{
			const float x = wrap(190.f + i * 585.f - time * 85.f, 1170.f) - 14.f;
			const float y = 526.f + std::round(std::sin(time * 2.5f + i * 3.f));
			rectangle(batch, x - 8.f, y + 1.f, 16.f, 2.f, { 55, 126, 138 });
			rectangle(batch, x - 5.f, y - 2.f, 10.f, 3.f, { 28, 49, 71 });
			rectangle(batch, x - 3.f, y - 4.f, 6.f, 2.f, { 209, 124, 96 });
			rectangle(batch, x - 1.f, y - 9.f, 2.f, 5.f, { 46, 62, 74 });
			const bool beacon = wrap(time + 0.6f * i, 2.4f) < 0.24f;
			rectangle(batch, x - 1.f, y - 10.f, 2.f, 2.f,
				beacon ? Color{ 255, 229, 151 } : Color{ 178, 103, 90 });
		}

		const float pier = std::round(wrap(1410.f - time * 145.f, 1960.f) - 165.f);
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

	void patrolBoat(Batch& batch, const Enemy& boat)
	{
		const Vec p{ std::round(boat.position.x), std::round(boat.position.y) };
		const auto part = [&](float x, float y, float w, float h, Color color) {
			rectangle(batch, p.x + x, p.y + y, w, h, color);
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
	}

	void drawBattleship(sf::RenderTarget& target, Batch& batch, const sf::Texture& hull,
						const Combat& combat, const Battleship& ship)
	{
		const Vec p{ std::round(ship.position.x), std::round(ship.position.y) };
		const float sinking = ship.sinking ? std::min(1.f, ship.sinkAge / Battleship::sinkDuration) : 0.f;
		battleshipWake(batch, p, ship.age, 1.f - sinking);
		drawBatch(target, batch);
		batch.clear();

		const Vec topLeft = p - Vec{ 129.f, 87.f };
		const float waterline = p.y + 21.f;
		const auto descent = [&](float x) {
			return 121.f * sinking * sinking + (p.x - x) * 0.07f * sinking;
		};
		const Vec size(hull.getSize());
		const auto edge = [&](float u) {
			const float x = topLeft.x + u;
			const float top = std::min(waterline, topLeft.y + descent(x));
			const float visible = std::min(size.y, waterline - top);
			return std::array<sf::Vertex, 2>{
				sf::Vertex{ { x, top }, Color::White, { u, 0.f } },
				sf::Vertex{ { x, top + visible }, Color::White, { u, visible } }
			};
		};
		const auto left = edge(0.f);
		const auto right = edge(size.x);
		quad(batch, left[0], right[0], right[1], left[1]);
		drawBatch(target, batch, &hull);
		batch.clear();

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
			for (auto& vertex : batch)
			{
				auto& pos = vertex.position;
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

	void projectile(Batch& batch, const Shot& shot)
	{
		const Vec p = shot.position;
		const float r = shot.radius;
		const bool friendly = shot.isFriendly();
		const Vec direction = shot.velocity != Vec{} ? shot.velocity.normalized() : Vec{ 1.f, 0.f };

		if (shot.kind == ShotKind::Missile)
		{
			const Vec side = direction.perpendicular();
			const Vec tail = p - direction * 11.f;
			const float flameLength = 8.f + 3.f * std::cos(51.f * shot.age);
			for (int i = 4; i > 0; --i)
			{
				const float lookback = i * 0.035f;
				if (lookback > shot.age)
					continue;
				const auto past = shot.trailSample(lookback);
				puff(batch, past.position - past.velocity.normalized() * 12.f, 2.f + i * 0.65f,
				     faded({173, 189, 184}, 0.58f - 0.08f * i));
			}
			batch.insert(batch.end(), {
				{tail + side * 3.f, {255, 186, 72}}, {tail - direction * flameLength, {251, 87, 42}},
				{tail - side * 3.f, {255, 223, 126}},
				{p + direction * 2.f, {105, 140, 144}}, {tail + side * 7.f, {222, 97, 55}},
				{tail - side * 7.f, {157, 66, 48}}
			});
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
		const float length = wave ? 22.f : friendly ? 17.f : 8.f;
		const Vec tail = p - direction * length;
		const Vec head = p + direction * (friendly ? 4.f : 2.f);
		const Color shadow = friendly ? Color{ 24, 81, 82 } : Color{ 70, 17, 61 };
		const Color rim = friendly ? (wave ? Color{ 73, 244, 204 } : Color{ 255, 183, 44 })
			: Color{ 255, 53, 111 };
		const Color core = friendly ? Color{ 255, 255, 206 } : Color{ 255, 220, 219 };
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
		streak(batch, pulse.origin, pulse.end, direction, 5.f + glow * 2.f, faded({63, 142, 255}, 0.33f * glow));
		streak(batch, pulse.origin, pulse.end, direction, LaserPulse::radius, faded({92, 222, 255}, glow));
		streak(batch, pulse.origin, pulse.end, direction, 1.3f, faded({245, 255, 254}, glow));
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
		const float size = effect.kind == ExplosionKind::Player ? 1.6f : 1.f;
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
	: coast(arcadeDirectory / "coast.png"), enemyAtlas(arcadeDirectory / "enemies.png"),
	burstAtlas(arcadeDirectory / "burst.png"), battleshipHull(arcadeDirectory / "battleship-hull.png")
{
	coast.setRepeated(true);
}

void BattleRenderer::drawBackground(sf::RenderTarget& target, const Combat& combat)
{
	const float elapsedSeconds = combat.elapsed();
	const float scroll = std::floor(wrap(elapsedSeconds * 24.f, sceneWidth));
	constexpr float margin = Combat::gunshipShakeAmplitude + 1.f;
	const float bottom = static_cast<float>(coast.getSize().y);
	std::array<sf::Vertex, 8> landscape;
	const auto row = [&](std::size_t first, float y, float textureY) {
		landscape[first] = { { -margin, y }, Color::White, { scroll - margin, textureY } };
		landscape[first + 1] = { { sceneWidth + margin, y }, Color::White, { scroll + sceneWidth + margin, textureY } };
	};
	// Extend the sky and water edge texels vertically while horizontal scrolling wraps.
	row(0, -margin, 0.5f);
	row(2, 0.5f, 0.5f);
	row(4, bottom - 0.5f, bottom - 0.5f);
	row(6, bottom + margin, bottom - 0.5f);
	target.draw(landscape.data(), landscape.size(), sf::PrimitiveType::TriangleStrip, &coast);
	geometry.clear();
	water(geometry, elapsedSeconds);
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
	if (const auto& ship = combat.battleship())
		drawBattleship(target, foreground, battleshipHull, combat, *ship);
	for (const Enemy& enemy : combat.enemies())
	{
		if (enemy.kind == EnemyKind::BattleshipTurret)
			continue;
		if (enemy.kind == EnemyKind::PatrolBoat)
		{
			patrolBoat(foreground, enemy);
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
	for (const Shot& shot : combat.shots())
		if (shot.isFriendly())
			projectile(foreground, shot);
	if (const auto& laser = combat.laserPulse())
		laserFlash(foreground, *laser);
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
