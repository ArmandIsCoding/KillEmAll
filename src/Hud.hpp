#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <bitset>
#include <cstdint>
#include <filesystem>

class Combat;
namespace sf { class RenderTarget; }

class Hud
{
public:
    explicit Hud(const std::filesystem::path& arcadeDir);
    void draw(sf::RenderTarget& target, const Combat& combat, bool paused, bool muted,
              std::uint64_t bestScore, bool powerNotice, bool pickupNotice,
              bool saveFailed = false, bool restored = false) const;
    void drawCampaignMenu(sf::RenderTarget& target, unsigned selectedOption, unsigned highest, unsigned unlockedCount,
                          bool saveFailed) const;
    void drawLevelSelect(sf::RenderTarget& target, unsigned selectedLevel, const std::bitset<10>& unlockedLevels,
                         bool saveFailed) const;

private:
    sf::Font font;
    sf::Texture lifeTexture;
    sf::Texture portPreview;
    sf::Texture campaignPreviews;
};
