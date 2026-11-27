#include "Hud.hpp"
#include "Combat.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
constexpr std::array<const char32_t*, Combat::levelCount> levelNames{{
    U"COSTA INDUSTRIAL", U"EL DESGUACE", U"ARCHIPI\u00c9LAGO DE TORMENTAS", U"FIORDO POLAR",
    U"REFINER\u00cdA DEL DESIERTO", U"SELVA INUNDADA", U"CALDERA VOLC\u00c1NICA", U"CIUDAD NE\u00d3N",
    U"MAR DE NUBES", U"FORTALEZA DEL ECLIPSE"
}};

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

std::string scoreNumber(std::uint64_t points)
{
    auto number = std::to_string(points);
    if (number.size() < 6)
        number.insert(0, 6 - number.size(), '0');
    return number;
}

void bossMeter(sf::RenderTarget& target, const sf::Font& font, const Combat& combat)
{
    rect(target, 265, 63, 430, 25, {15, 27, 34, 242});
    rect(target, 265, 63, 3, 25, gold);
    label(target, font, "ACORAZADO", 277, 71, 10, ivory);

    rect(target, 378, 68, 262, 15, seam);
    rect(target, 380, 70, 258, 11, ink);
    const int health = combat.bossHealth();
    const float hullFill = std::ceil(258.f * health / Battleship::maxHealth);
    rect(target, 380, 70, 258, 7, {64, 46, 48});
    rect(target, 380, 70, hullFill, 7, combat.battleship()->hitFlash > 0.05f ? ivory : sf::Color{225, 101, 77});
    rect(target, 380, 70, hullFill, 2, {255, 191, 140});

    // The thinner gauges below the hull read from bow to stern.
    constexpr std::array sections{TurretKind::Cannon, TurretKind::Flak, TurretKind::Burst};
    for (std::size_t i = 0; i < sections.size(); ++i)
    {
        const float x = 380.f + 87.f * static_cast<float>(i);
        rect(target, x, 78, 84, 3, {64, 46, 48});
        if (const auto* gun = combat.bossTurret(sections[i]))
        {
            const float fill = std::ceil(84.f * gun->health / gun->maxHealth());
            rect(target, x, 78, fill, 3, gun->hitFlash > 0.f ? ivory : gold);
        }
    }
    const int percent = (100 * health + Battleship::maxHealth - 1) / Battleship::maxHealth;
    const auto value = std::to_string(percent) + "%";
    label(target, font, value, 687.f - 10.f * static_cast<float>(value.size()), 71, 10, ivory);
}

void lifeRecoveryNotice(sf::RenderTarget& target, const sf::Font& font, float flash)
{
    const sf::Color accent = flash > 0.8f ? ivory : cyan;
    rect(target, 16, 63, 240, 44, {15, 35, 39, 245});
    rect(target, 16, 63, 4, 44, accent);
    label(target, font, "+1 VIDA", 28, 73, 32, ink);
    label(target, font, "+1 VIDA", 26, 71, 32, accent);
}

void carrierMeter(sf::RenderTarget& target, const sf::Font& font, const Combat& combat)
{
    const auto& raid = combat.carrierRaid();
    rect(target, 265, 63, 430, 42, {15, 27, 34, 242});
    const bool secondPhase = raid.deckBurning() && raid.phase == CarrierPhase::Battle;
    rect(target, 265, 63, 3, 42, secondPhase ? gold : cyan);
    label(target, font, "PORTAAVIONES", 276, 69, 10, ivory);
    label(target, font, "CASCO", 398, 71, 7, ivory);
    constexpr float hullX = 442.f, hullWidth = 196.f;
    rect(target, hullX - 2, 68, hullWidth + 4, 12, seam);
    rect(target, hullX, 70, hullWidth, 8, ink);
    const float life = static_cast<float>(raid.health) / raid.maxHealth;
    const float fill = std::ceil(hullWidth * life);
    rect(target, hullX, 70, fill, 8, {190, 82, 68});
    rect(target, hullX, 70, fill, 2, {255, 186, 130});
    if (secondPhase)
        label(target, font, "FASE II", 574, 70, 8, gold);
    else if (raid.phase == CarrierPhase::Battle)
        rect(target, hullX + hullWidth * CarrierRaid::fireThreshold, 70, 2, 8, gold);
    const auto percent = std::to_string(static_cast<int>(std::ceil(100.f * life))) + "%";
    label(target, font, percent, 686.f - 10.f * static_cast<float>(percent.size()), 69, 10, ivory);

    struct Mount { CarrierPart part; const char* name; };
    constexpr std::array<Mount, 3> mounts{{
        {CarrierPart::Catapult, "CATAPULTA"}, {CarrierPart::ForwardLight, "FOCO PROA"},
        {CarrierPart::AftLight, "FOCO POPA"}
    }};
    for (std::size_t i = 0; i < mounts.size(); ++i)
    {
        const float x = 277.f + 139.f * static_cast<float>(i);
        const auto* part = combat.carrierPart(mounts[i].part);
        label(target, font, mounts[i].name, x, 88, 8, part ? ivory : dim);
        rect(target, x + 76, 89, 45, 5, seam);
        if (part)
            rect(target, x + 76, 89, std::ceil(45.f * part->health / part->maxHealth()), 5,
                 part->hitFlash > 0.f ? sf::Color::White : gold);
    }
}

void comboNotice(sf::RenderTarget& target, const sf::Font& font, Combo combo, float top)
{
    struct Tier { unsigned size; sf::Color color; };
    constexpr std::array<Tier, 9> tiers{{
        {14, cyan}, {17, {184, 237, 126}}, {20, gold}, {23, {255, 140, 102}},
        {26, {255, 139, 197}}, {28, {255, 161, 216}}, {30, {255, 190, 221}},
        {31, {255, 216, 210}}, {32, ivory}
    }};
    const auto [size, color] = tiers[std::min(combo.kills - 2, static_cast<int>(tiers.size()) - 1)];
    const auto count = "COMBO x" + std::to_string(combo.kills);
    const auto bonus = "+" + std::to_string(combo.bonus);
    constexpr unsigned bonusSize = 12;
    constexpr float left = 22.f;
    const float height = size + 10.f;
    // Press Start 2P advances one character size per glyph.
    const float divider = left + 16.f + static_cast<float>(count.size() * size);
    const float width = divider - left + 16.f + static_cast<float>(bonus.size() * bonusSize);
    rect(target, left, top, width, height, {15, 27, 34, 236});
    rect(target, left, top, 3.f, height, color);
    rect(target, divider, top + 6.f, 1.f, height - 12.f, seam);
    label(target, font, count, left + 9.f, top + 6.f, size, color);
    label(target, font, bonus, divider + 8.f, top + std::round((height - bonusSize) * 0.5f) + 1.f, bonusSize, ivory);
}

void milestoneNotice(sf::RenderTarget& target, const sf::Font& font, int milestone, float top)
{
    const sf::String title = milestone == 10 ? U"\u00a1LEGENDARIO!" : U"\u00a1IMPARABLE!";
    const auto count = "x" + std::to_string(milestone);
    const sf::Color accent = milestone == 10 ? ivory : sf::Color{255, 140, 102};
    const float tagWidth = 10.f + static_cast<float>(count.size() * 12);
    const float tagX = 938.f - tagWidth;
    const float left = tagX - 20.f - static_cast<float>(title.getSize() * 15);
    rect(target, left, top, 938.f - left, 29, {39, 29, 41, 242});
    rect(target, left, top, 3, 29, accent);
    label(target, font, title, left + 10.f, top + 8.f, 15, accent);
    rect(target, tagX, top + 5.f, tagWidth - 3.f, 19, accent);
    label(target, font, count, tagX + 3.f, top + 9.f, 12, ink);
}

void abilityMeter(sf::RenderTarget& target, const sf::Font& font, const sf::String& key,
                  const sf::String& name, float y, float charge, bool active, bool recharging,
                  sf::Color accent, float flash)
{
    rect(target, 440, y, 68, 18, active ? accent : seam);
    rect(target, 441, y + 1, 66, 16, active ? accent : steel);
    const float keyWidth = static_cast<float>(key.getSize() * 8);
    label(target, font, key, 474.f - keyWidth * 0.5f, y + 5, 8, active ? ink : ivory);
    label(target, font, name, 516, y + 5, 9, active ? accent : dim);

    constexpr float width = 76.f;
    if (flash > 0.f)
        rect(target, 602, y + 1, width + 8, 16,
             {accent.r, accent.g, accent.b, static_cast<std::uint8_t>(180.f * flash)});
    rect(target, 604, y + 3, width + 4, 12, active ? ivory : seam);
    rect(target, 606, y + 5, width, 8, ink);
    const float fill = std::round(width * std::clamp(charge, 0.f, 1.f));
    if (fill > 0.f)
        rect(target, 606, y + 5, fill, 8, recharging ? seam : accent);
    if (flash > 0.f)
        rect(target, 606, y + 5, fill, 8, {255, 255, 245, static_cast<std::uint8_t>(255.f * flash)});
    for (float tick : {19.f, 38.f, 57.f})
        rect(target, 606 + tick, y + 5, 1, 8, ink);
    if (recharging)
        label(target, font, "RECARGA", 616, y + 5, 8, ivory);
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

void pausePanel(sf::RenderTarget& target, const sf::Font& font, bool muted, WeaponMode weapon)
{
    panel(target, 211, 145, 538, 357, cyan);
    label(target, font, "PAUSA", 243, 173, 32, ivory);
    if (weapon == WeaponMode::Arsenal)
        label(target, font, U"ATAJ\u00c1 UN TIRO CON CADA PUNTA PARA LANZAR LA TRENZA.", 243, 215, 9, cyan);
    rect(target, 243, 229, 474, 2, seam);

    const auto row = [&](float y, const sf::String& key, const sf::String& action) {
        keycap(target, font, key, 243, y, 178);
        label(target, font, action, 439, y + 5, 12, ivory);
    };
    row(248, "WASD / FLECHAS", "MOVERSE");
    row(286, "MANTENER SHIFT", U"C\u00c1MARA LENTA AL 45%");
    row(324, "MANTENER ESPACIO", U"GUIAR AL M\u00c1S FUERTE");
    row(362, "M", muted ? "ACTIVAR SONIDO" : "SILENCIAR SONIDO");
    row(400, "P", "REANUDAR");
    row(438, "ESC", "SALIR");
    label(target, font, U"SI AGOT\u00c1S UNA BARRA, ESPER\u00c1 LA RECARGA COMPLETA.", 243, 476, 9, dim);
}

void campaignBossMeter(sf::RenderTarget& target, const sf::Font& font, const StageBoss& boss)
{
    const sf::Color accent = boss.critical() ? sf::Color{253, 117, 92} : gold;
    rect(target, 265, 63, 430, 35, {15, 27, 34, 242});
    rect(target, 265, 63, 3, 35, accent);
    label(target, font, StageBoss::name(boss.level), 277, 70, 10, ivory);
    rect(target, 421, 69, 218, 9, seam);
    const float life = static_cast<float>(boss.health) / boss.maxHealth;
    rect(target, 423, 71, std::ceil(214.f * life), 5, boss.hitFlash > 0.f ? ivory : accent);
    const auto percent = std::to_string(static_cast<int>(std::ceil(100.f * life))) + "%";
    label(target, font, percent, 685.f - 9.f * percent.size(), 70, 9, ivory);
    label(target, font, U"BATER\u00cdAS", 277, 86, 7, dim);
    const float width = 214.f / boss.mountCount();
    for (unsigned i = 0; i < boss.mountCount(); ++i)
    {
        const float x = 423.f + width * i;
        rect(target, x, 85, width - 3.f, 5, seam);
        rect(target, x, 85, (width - 3.f) * boss.mountHealth[i] / boss.mountMaxHealth, 5, cyan);
    }
}

void endPanel(sf::RenderTarget& target, const sf::Font& font, const Combat& combat, std::uint64_t bestScore)
{
    panel(target, 216, 170, 528, 292, gold);
    label(target, font, combat.campaignWon() ? U"CAMPA\u00d1A COMPLETA" : U"FIN DE PARTIDA", 248, 192, 26, ivory);
    label(target, font, sf::String("NIVEL " + std::to_string(combat.level()) + "  /  ") +
          levelNames[combat.level() - 1], 248, 231, 9, cyan);
    rect(target, 248, 250, 464, 2, seam);
    label(target, font, "PUNTOS", 248, 273, 9, dim);
    const auto points = scoreNumber(combat.score());
    label(target, font, points, 248, 297, std::min(24u, 218u / static_cast<unsigned>(points.size())), gold);
    label(target, font, U"R\u00c9CORD DE SESI\u00d3N", 487, 273, 9, dim);
    const auto record = scoreNumber(std::max(combat.score(), bestScore));
    label(target, font, record, 487, 299, std::min(19u, 225u / static_cast<unsigned>(record.size())), ivory);
    label(target, font, "MEJOR COMBO", 248, 343, 10, dim);
    label(target, font, combat.bestCombo() > 0 ? "x" + std::to_string(combat.bestCombo()) : "NINGUNO", 388, 339, 16, cyan);
    rect(target, 248, 374, 464, 2, seam);
    keycap(target, font, "ENTER", 248, 397, 72);
    label(target, font, "VOLVER A JUGAR", 334, 403, 12, ivory);
    keycap(target, font, "ESC", 568, 397, 49);
    label(target, font, "SALIR", 632, 404, 10, dim);
}
}

Hud::Hud(const std::filesystem::path& arcadeDir)
    : font(arcadeDir / "font" / "PressStart2P-Regular.ttf"),
      lifeTexture(arcadeDir / "player" / "reposo.png")
{
    font.setSmooth(false);
}

void Hud::draw(sf::RenderTarget& target, const Combat& combat, bool paused, bool muted,
               std::uint64_t bestScore, bool powerNotice, bool pickupNotice) const
{
    rect(target, 0, 0, 960, 60, ink);
    rect(target, 0, 2, 960, 2, steel);
    rect(target, 0, 56, 960, 2, seam);
    rect(target, 0, 58, 960, 2, {149, 96, 55});
    for (float x : {168.f, 280.f, 428.f, 696.f, 836.f})
        rect(target, x, 11, 2, 34, seam);

    label(target, font, "PUNTOS", 16, 9, 9, dim);
    const auto score = scoreNumber(combat.score());
    label(target, font, score, 16, 25, std::min(21u, 144u / static_cast<unsigned>(score.size())), gold);
    const bool flying = !paused && !combat.finished();
    const bool bossVisible = combat.battleship().has_value() && !combat.gameOver();
    const auto raidPhase = combat.carrierRaid().phase;
    const bool carrierVisible = !combat.gameOver() &&
        (raidPhase == CarrierPhase::Battle || raidPhase == CarrierPhase::Sinking);
    const bool stageBossVisible = !combat.finished() && combat.stageBoss() && combat.stageBoss()->health > 0;
    if (stageBossVisible)
        campaignBossMeter(target, font, *combat.stageBoss());
    else if (carrierVisible)
        carrierMeter(target, font, combat);
    else if (bossVisible)
        bossMeter(target, font, combat);
    const float noticeTop = carrierVisible ? 108.f : stageBossVisible ? 101.f : bossVisible ? 91.f : 60.f;
    const float lifeFlash = flying ? combat.lifeRecoveryFlash() : 0.f;
    if (lifeFlash > 0.f)
        lifeRecoveryNotice(target, font, lifeFlash);
    const auto combo = combat.combo();
    if (combo.kills >= 2 && !paused)
        comboNotice(target, font, combo, lifeFlash > 0.f ? 111.f : noticeTop);
    const int milestone = combat.comboMilestone();
    if (milestone > 0 && !paused && !combat.gameOver())
        milestoneNotice(target, font, milestone, noticeTop);

    label(target, font, "VIDAS", 181, 9, 9, dim);
    sf::Sprite plane(lifeTexture);
    plane.setOrigin(sf::Vector2f(lifeTexture.getSize()) * 0.5f);
    plane.setScale({0.29f, 0.29f});
    for (int i = 0; i < Combat::maxLives; ++i)
    {
        plane.setPosition({193.f + i * 31.f, 31.f});
        plane.setColor(i < combat.lives() ? sf::Color::White : sf::Color{108, 137, 145, 135});
        target.draw(plane);
    }
    if (!combat.gameOver() && combat.lives() < Combat::maxLives)
    {
        const auto count = std::to_string(combat.recoveryKills()) + "/" + std::to_string(Combat::killsPerLife);
        sf::Color countColor = cyan;
        if (const auto phase = combat.recoveryBlinkPhase())
        {
            countColor = gold;
            if (*phase < 0.5f)
            {
                rect(target, 178, 41, 96, 13, gold);
                countColor = ink;
            }
        }
        label(target, font, "BAJAS " + count, 181, 44, 8, countColor);
    }

    const WeaponMode mode = combat.weaponMode();
    sf::String weaponName = U"CA\u00d1\u00d3N";
    sf::Color weaponColor = gold;
    if (mode == WeaponMode::Missiles)
    {
        weaponName = "MISILES";
        weaponColor = {255, 148, 93};
    }
    else if (mode == WeaponMode::Laser)
    {
        weaponName = U"L\u00c1SER";
        weaponColor = cyan;
    }
    else if (mode == WeaponMode::Arsenal)
    {
        weaponName = "ARSENAL";
        weaponColor = {147, 213, 255};
    }
    label(target, font, weaponName, 295, 8, 12, weaponColor);
    const std::string rank = "R" + std::to_string(combat.weaponRank());
    const auto rankLength = static_cast<unsigned>(rank.size());
    const auto rankSize = std::min(9u, 39u / rankLength);
    const float badgeWidth = std::max(33.f, 4.f + rankSize * static_cast<float>(rankLength));
    rect(target, 423.f - badgeWidth, 6, badgeWidth, 15, powerNotice ? weaponColor : steel);
    label(target, font, rank, 423.f - (badgeWidth + rankSize * rankLength) * 0.5f,
          9, rankSize, powerNotice ? ink : ivory);
    if (mode == WeaponMode::Arsenal)
    {
        const unsigned upgrades = combat.droneUpgrades();
        sf::String status;
        if (upgrades < Combat::secondDroneUpgrade)
            status = sf::String(U"2\u00ba ROBOT ") + std::to_string(upgrades) + "/" +
                     std::to_string(Combat::secondDroneUpgrade);
        else
            status = U"D\u00daO EN \u00d3RBITA";
        label(target, font, status, 295, 25, 9, weaponColor);
    }
    else
    {
        for (int i = 0; i < 5; ++i)
        {
            const float x = 295.f + i * 24.f;
            const bool active = i < combat.power();
            rect(target, x, 25, 20, 9, active ? weaponColor : seam);
            rect(target, x + 2, active ? 31.f : 27.f, 16, active ? 1.f : 5.f, active ? steel : ink);
        }
    }
    if (pickupNotice && mode == WeaponMode::Arsenal)
        label(target, font, combat.droneUpgrades() == Combat::secondDroneUpgrade ? "+1 ROBOT" : "MAYOR CADENCIA",
              295, 40, 8, gold);
    else if (powerNotice)
        label(target, font, "ARMA MEJORADA", 295, 40, 8, ivory);
    else if (pickupNotice)
        label(target, font, mode == WeaponMode::Cannon ? "CADENCIA +50%" : "POTENCIADOR", 295, 40, 8, cyan);
    else
    {
        const auto kills = combat.enemiesDestroyed();
        const auto next = combat.nextWeaponRankKills();
        auto progress = std::to_string(kills) + "/" + std::to_string(next) + " BAJAS";
        if (progress.size() > 16)
            progress = "FALTAN " + std::to_string(next - kills);
        label(target, font, progress, 295, 40, std::min(8u, 128u / static_cast<unsigned>(progress.size())), dim);
    }

    const auto refillFlash = [flying](float age) {
        return flying ? std::clamp((0.30f - age) / 0.24f, 0.f, 1.f) : 0.f;
    };
    abilityMeter(target, font, "SHIFT", U"PRECISI\u00d3N", 7, combat.precisionCharge(),
                 flying && combat.precisionActive(), combat.precisionRecharging(),
                 cyan, refillFlash(combat.precisionRefillAge()));
    abilityMeter(target, font, "ESPACIO", "GUIADO", 31, combat.guidanceCharge(),
                 flying && combat.guidanceActive(), combat.guidanceRecharging(),
                 gold, refillFlash(combat.guidanceRefillAge()));
    const auto level = "NIVEL " + std::to_string(combat.level()) + "/" + std::to_string(Combat::levelCount);
    label(target, font, level, 707, 9, 9, cyan);
    const auto meters = std::to_string(static_cast<long long>(combat.distance())) + " m";
    label(target, font, meters, 707, 28, std::min(15u, 121u / static_cast<unsigned>(meters.size())), ivory);
    if (!combat.finished())
    {
        keycap(target, font, "P", 857, 18, 27);
        label(target, font, paused ? "SEGUIR" : "PAUSA", 895, 25, 9, dim);
    }

    if (combat.finished())
        endPanel(target, font, combat, bestScore);
    else if (paused)
        pausePanel(target, font, muted, mode);
    else if (combat.levelTime() < 2.8f || combat.levelCleared())
    {
        rect(target, 22, 524, 410, 58, {14, 27, 33, 233});
        rect(target, 22, 524, 4, 58, combat.levelCleared() ? gold : cyan);
        label(target, font, levelNames[combat.level() - 1], 38, 534, 15, ivory);
        if (combat.levelCleared())
            label(target, font, "NIVEL SUPERADO", 38, 564, 10, gold);
        else if (combat.level() == 1)
            label(target, font, U"WASD / FLECHAS   DISPARO AUTOM\u00c1TICO", 38, 565, 9, dim);
        else
            label(target, font, std::string("OBJETIVO  /  ") + StageBoss::name(combat.level()), 38, 565, 9, cyan);
    }
    else if (combat.level() > 1 && !stageBossVisible && combat.bossArrival() - combat.levelTime() < 3.f)
    {
        rect(target, 22, 543, 300, 31, ink);
        rect(target, 22, 543, 4, 31, gold);
        label(target, font, std::string("ALERTA  /  ") + StageBoss::name(combat.level()), 36, 553, 11, gold);
    }
}
