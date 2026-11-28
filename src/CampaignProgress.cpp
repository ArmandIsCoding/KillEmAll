#include "CampaignProgress.hpp"

#include <atomic>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;
constexpr std::string_view header = "KillEmAll-checkpoint-v1 ";

#ifdef _WIN32
fs::path environmentPath(const wchar_t* name)
{
    const DWORD capacity = GetEnvironmentVariableW(name, nullptr, 0);
    if (capacity == 0)
        return {};
    std::wstring value(capacity, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), capacity);
    if (length == 0 || length >= capacity)
        return {};
    value.resize(length);
    return value;
}
#endif

fs::path saveDirectory()
{
#ifdef _WIN32
    if (auto local = environmentPath(L"LOCALAPPDATA"); !local.empty())
        return local / "KillEmAll";
    if (auto profile = environmentPath(L"USERPROFILE"); !profile.empty())
        return profile / "AppData" / "Local" / "KillEmAll";
#else
    if (const auto* data = std::getenv("XDG_DATA_HOME"); data && *data)
        return fs::path(data) / "KillEmAll";
    if (const auto* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".local" / "share" / "KillEmAll";
#endif
    return {};
}

fs::path slotPath(const fs::path& directory, unsigned level)
{
    return directory / ("level-" + std::to_string(level) + ".sav");
}

fs::path backupPath(fs::path primary)
{
    primary += ".bak";
    return primary;
}

std::uint32_t checksum(std::string_view data)
{
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : data)
        hash = (hash ^ byte) * 16777619u;
    return hash;
}

std::optional<Combat::Checkpoint> readCheckpoint(const fs::path& path, unsigned level)
{
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size <= header.size() || size > 16384)
        return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!file.read(bytes.data(), static_cast<std::streamsize>(size)) || file.peek() != std::char_traits<char>::eof())
        return std::nullopt;
    const auto newline = bytes.find('\n');
    if (bytes.compare(0, header.size(), header) != 0 || newline == std::string::npos)
        return std::nullopt;
    std::uint32_t expected = 0;
    const auto parsed = std::from_chars(bytes.data() + header.size(), bytes.data() + newline, expected);
    const auto payload = bytes.substr(newline + 1);
    if (parsed.ec != std::errc{} || parsed.ptr != bytes.data() + newline || checksum(payload) != expected)
        return std::nullopt;

    std::istringstream in(payload);
    in.imbue(std::locale::classic());
    Combat::Checkpoint entry;
    if (!(in >> entry) || entry.level != level || !(in >> std::ws).eof())
        return std::nullopt;
    return entry;
}

// Each slot has its own destination. Simultaneous games saving different levels cannot undo each other's unlocks.
bool writeAtomically(const fs::path& path, const std::string& bytes)
{
    static std::atomic<unsigned> sequence{0};
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    auto temporary = path;
    temporary += "." + std::to_string(pid) + "." + std::to_string(sequence++) + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    std::error_code error;
    if (out)
    {
#ifdef _WIN32
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
#else
        fs::rename(temporary, path, error);
        if (!error)
            return true;
#endif
    }
    fs::remove(temporary, error);
    return false;
}
}

CampaignProgress::CampaignProgress() : CampaignProgress(saveDirectory())
{
}

CampaignProgress::CampaignProgress(std::filesystem::path directory) : directory_(std::move(directory))
{
    if (directory_.empty())
        return;
    for (unsigned level = 2; level <= Combat::levelCount; ++level)
    {
        const auto path = slotPath(directory_, level);
        auto& slot = slots_[level - 2];
        slot = readCheckpoint(path, level);
        if (!slot)
            slot = readCheckpoint(backupPath(path), level);
    }
}

unsigned CampaignProgress::highestUnlocked() const
{
    for (unsigned level = Combat::levelCount; level > 1; --level)
        if (checkpoint(level))
            return level;
    return 1;
}

std::bitset<Combat::levelCount> CampaignProgress::unlockedLevels() const
{
    std::bitset<Combat::levelCount> available;
    available.set(0);
    for (unsigned level = 2; level <= Combat::levelCount; ++level)
        available.set(level - 1, checkpoint(level) != nullptr);
    return available;
}

const Combat::Checkpoint* CampaignProgress::checkpoint(unsigned level) const
{
    if (level < 2 || level > Combat::levelCount)
        return nullptr;
    const auto& slot = slots_[level - 2];
    return slot ? &*slot : nullptr;
}

bool CampaignProgress::save(const Combat::Checkpoint& entry)
{
    if (entry.level < 2 || entry.level > Combat::levelCount || (entry.level > 2 && !checkpoint(entry.level - 1)) ||
        !entry.valid())
        return false;
    slots_[entry.level - 2] = entry;
    pending_[entry.level - 2] = true;
    return flush();
}

bool CampaignProgress::flush()
{
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        if (!pending_[i])
            continue;
        if (directory_.empty())
            return false;
        std::error_code error;
        fs::create_directories(directory_, error);
        if (error)
            return false;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << *slots_[i];
        const auto payload = out.str();
        const auto bytes = std::string(header) + std::to_string(checksum(payload)) + '\n' + payload;
        const auto path = slotPath(directory_, slots_[i]->level);
        if (!out || !writeAtomically(path, bytes))
            return false;
        // A damaged primary can be reconstructed even after the first visit to this level.
        writeAtomically(backupPath(path), bytes);
        pending_[i] = false;
    }
    return true;
}
