#include "assetextract.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static std::string LoadAssetText(const char* path)
{
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> bytes(SDL_LoadFile(path, &size), SDL_free);
    if (!bytes)
        throw std::runtime_error(std::string("Couldn't read asset: ") + path);
    return std::string(static_cast<const char*>(bytes.get()), size);
}

void ExtractAndroidAssets(const fs::path& destinationRoot)
{
    const std::string contentId = LoadAssetText("Data/content.sha256");
    if (contentId.size() != 64 || !std::all_of(contentId.begin(), contentId.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        throw std::runtime_error("Invalid Android asset content identity");

    const fs::path destination = destinationRoot / "Data";
    const fs::path staging = destinationRoot / "Data.new";
    const fs::path backup = destinationRoot / "Data.old";
    // Recover an interrupted directory swap before deciding whether to extract.
    if (!fs::exists(destination) && fs::exists(backup))
        fs::rename(backup, destination);

    const fs::path marker = destination / ".asset-content";
    bool current = false;
    if (fs::exists(marker) && fs::exists(destination / "System" / "gamecontrollerdb.txt"))
    {
        try
        {
            current = LoadAssetText(reinterpret_cast<const char*>(marker.u8string().c_str())) == contentId;
        }
        catch (const std::runtime_error&)
        {
            // An unreadable installed marker is a cache miss. The packaged
            // identity above remains mandatory so a damaged APK cannot be used.
        }
    }
    if (current)
    {
        fs::remove_all(backup);
        fs::remove_all(staging);
        return;
    }

    fs::remove_all(staging);
    fs::create_directories(staging);
    try
    {
        std::istringstream manifest(LoadAssetText("Data/files.txt"));
        std::string line;
        while (std::getline(manifest, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            fs::path asset = fs::path(line).lexically_normal();
            fs::path relative;
            auto component = asset.begin();
            bool safe = line.find('\0') == std::string::npos && !asset.is_absolute()
                && component != asset.end() && *component == "Data";
            if (safe)
            {
                for (++component; component != asset.end(); ++component)
                {
                    if (component->empty() || *component == "." || *component == "..")
                    {
                        safe = false;
                        break;
                    }
                    relative /= *component;
                }
            }
            if (!safe || relative.empty() || relative == ".asset-content")
                throw std::runtime_error("Unsafe asset path in manifest: " + line);

            const fs::path output = staging / relative;
            fs::create_directories(output.parent_path());
            const std::string bytes = LoadAssetText(asset.generic_string().c_str());
            if (!SDL_SaveFile(reinterpret_cast<const char*>(output.u8string().c_str()), bytes.data(), bytes.size()))
                throw std::runtime_error("Couldn't save extracted asset: " + line);
        }
        if (!fs::exists(staging / "System" / "gamecontrollerdb.txt"))
            throw std::runtime_error("Asset manifest omits gamecontrollerdb.txt");

        const fs::path stagingMarker = staging / ".asset-content";
        if (!SDL_SaveFile(reinterpret_cast<const char*>(stagingMarker.u8string().c_str()), contentId.data(), contentId.size()))
            throw std::runtime_error("Couldn't save Android asset content identity");

        fs::remove_all(backup);
        if (fs::exists(destination))
            fs::rename(destination, backup);
        try
        {
            fs::rename(staging, destination);
        }
        catch (...)
        {
            if (fs::exists(backup))
                fs::rename(backup, destination);
            throw;
        }
        fs::remove_all(backup);
    }
    catch (...)
    {
        fs::remove_all(staging);
        throw;
    }
}
