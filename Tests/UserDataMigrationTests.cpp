#include "Pomme.h"
#include "PommeFiles.h"
#include "TVOSStorage.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <stdexcept>
#include <string>

extern "C" {
short gPrefsFolderVRefNum;
long gPrefsFolderDirID;
OSErr LoadUserDataFile(const char*, const char*, long, Ptr);
OSErr InitPrefsFolder(bool) { return noErr; }
}

static int storedResult = kTVOSStorage_NotFound;
static bool failWrite;
static int writes;
static std::string stored;

extern "C" int TVOS_LoadUserData(const char*, const void* magic, long magicLength, void* payload, long length)
{
    if (storedResult != kTVOSStorage_OK)
        return storedResult;
    if (stored.size() != (size_t)(magicLength + length) || memcmp(stored.data(), magic, magicLength))
        return kTVOSStorage_Corrupt;
    memcpy(payload, stored.data() + magicLength, length);
    return kTVOSStorage_OK;
}

extern "C" int TVOS_SaveUserData(const char*, const void* magic, long magicLength, const void* payload, long length)
{
    writes++;
    if (failWrite)
        return kTVOSStorage_Error;
    stored.assign((const char*)magic, magicLength);
    stored.append((const char*)payload, length);
    storedResult = kTVOSStorage_OK;
    return kTVOSStorage_OK;
}

static void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "scratch directory required");
        const fs::path root = argv[1];
        // Match the production prefs subdirectory, supplied by the CMake target.
        const auto folder = root / CMR_TEST_PREFS_FOLDER;
        fs::create_directories(folder);
        Pomme::Files::Init();
        const FSSpec parent = Pomme::Files::HostPathToFSSpec(root / "placeholder");
        gPrefsFolderVRefNum = parent.vRefNum;
        gPrefsFolderDirID = parent.parID;
        const auto legacy = folder / "migration-test";
        auto writeLegacy = [&](const std::string& bytes) {
            std::ofstream output(legacy, std::ios::binary);
            output.write(bytes.data(), bytes.size());
            Check(output.good(), "legacy fixture write failed");
        };
        const std::string valid("MAGIC\0old!", 10);
        char payload[4] = {};
        auto load = [&] { return LoadUserDataFile("migration-test", "MAGIC", 4, payload); };
        Check(load() == fnfErr && writes == 0, "first launch should have no data to migrate");
        for (const auto& corrupt : {std::string("short"), std::string("WRONG\0old!", 10)})
        {
            writeLegacy(corrupt);
            Check(load() == badFileFormat && writes == 0, "corrupt legacy data was migrated");
        }
        writeLegacy(valid);
        failWrite = true;
        Check(load() == noErr && std::string(payload, 4) == "old!", "failed persistence lost usable legacy save");
        Check(fs::exists(legacy) && storedResult == kTVOSStorage_NotFound, "write failure lost retry source");
        failWrite = false;
        Check(load() == noErr && writes == 2 && stored == valid, "legacy save did not retry migration");
        Check(fs::exists(legacy), "asynchronous defaults write deleted the recovery file");
        writeLegacy(std::string("MAGIC\0stale", 11));
        Check(load() == noErr && std::string(payload, 4) == "old!" && writes == 2, "canonical data did not take precedence");
        storedResult = kTVOSStorage_Corrupt;
        Check(load() == badFileFormat && writes == 2, "corrupt canonical save was replaced by stale legacy data");
        Check(LoadUserDataFile("migration-test", "MAGIC", LONG_MAX, payload) == paramErr, "overflow length accepted");
        std::cout << "tvOS migration tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
