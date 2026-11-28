#pragma once

#include "Combat.hpp"

#include <array>
#include <bitset>
#include <filesystem>
#include <optional>

class CampaignProgress
{
public:
    CampaignProgress();
    explicit CampaignProgress(std::filesystem::path directory);

    unsigned highestUnlocked() const;
    std::bitset<Combat::levelCount> unlockedLevels() const;
    // Level one always starts with a fresh aircraft. Later levels have an entry checkpoint.
    const Combat::Checkpoint* checkpoint(unsigned level) const;
    bool save(const Combat::Checkpoint& checkpoint);
    bool flush();

private:
    std::filesystem::path directory_;
    std::array<std::optional<Combat::Checkpoint>, Combat::levelCount - 1> slots_{};
    std::array<bool, Combat::levelCount - 1> pending_{};
};
