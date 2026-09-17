#include "assetextract.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static void Write(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    Check(file.good(), "fixture write failed");
}

static std::string Read(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "scratch directory required");
        fs::current_path(argv[1]);
        const auto destination = fs::current_path() / "installed";
        Write("Data/System/gamecontrollerdb.txt", "controllers");
        Write("Data/asset.bin", "old bytes");
        Write("Data/files.txt", "Data/System/gamecontrollerdb.txt\nData/asset.bin\n");
        Write("Data/content.sha256", std::string(64, 'a'));
        ExtractAndroidAssets(destination);
        Check(Read(destination / "Data/asset.bin") == "old bytes", "initial extraction failed");
        Write(destination / "Data/retained", "same content ID");
        ExtractAndroidAssets(destination);
        Check(fs::exists(destination / "Data/retained"), "same-content launch unnecessarily extracted");

        // Marketing version never changes: only the packaged bytes/content ID do.
        Write("Data/asset.bin", "new bytes");
        Write("Data/content.sha256", std::string(64, 'b'));
        fs::remove("Data/System/gamecontrollerdb.txt");
        bool failed = false;
        try { ExtractAndroidAssets(destination); } catch (const std::exception&) { failed = true; }
        Check(failed, "incomplete extraction unexpectedly succeeded");
        Check(Read(destination / "Data/asset.bin") == "old bytes", "failure lost old installation");
        Check(Read(destination / "Data/.asset-content") == std::string(64, 'a'), "failure marked new assets current");
        Write("Data/System/gamecontrollerdb.txt", "controllers");
        ExtractAndroidAssets(destination);
        Check(Read(destination / "Data/asset.bin") == "new bytes", "same-version update left stale bytes");
        Check(!fs::exists(destination / "Data/retained"), "update retained deleted assets");

        fs::rename(destination / "Data", destination / "Data.old");
        ExtractAndroidAssets(destination);
        Check(Read(destination / "Data/asset.bin") == "new bytes", "interrupted swap not recovered");

        Write("Data/content.sha256", std::string(64, 'c'));
        Write("Data/files.txt", "Data/../../outside\n");
        failed = false;
        try { ExtractAndroidAssets(destination); } catch (const std::exception&) { failed = true; }
        Check(failed, "unsafe manifest accepted");
        Check(Read(destination / "Data/asset.bin") == "new bytes", "unsafe manifest lost installed data");
        std::cout << "Asset extraction tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
