// User-data loading. Built twice: with CMR_USE_TVOS_STORAGE (NSUserDefaults stubbed
// below, plus the legacy Caches file) and without it (plain prefs-folder files).
#include "Pomme.h"
#include "PommeFiles.h"
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <stdexcept>
#include <string>

extern "C" {
#include "game.h"
short gPrefsFolderVRefNum;
long gPrefsFolderDirID;
OSErr InitPrefsFolder(bool) { return noErr; }
}

static void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static fs::path gFolder;

static void WriteFile(const char* name, const std::string& bytes)
{
    std::ofstream output(gFolder / name, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), bytes.size());
    Check(output.good(), "fixture write failed");
}

template <typename T>
static std::string Blob(const char* magic, const T& payload)
{
    std::string bytes(magic, strlen(magic) + 1);
    bytes.append(reinterpret_cast<const char*>(&payload), sizeof(payload));
    return bytes;
}

#if defined(CMR_USE_TVOS_STORAGE)
#include "TVOSStorage.h"

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

static void ResetStorage()
{
    storedResult = kTVOSStorage_NotFound;
    failWrite = false;
    writes = 0;
    stored.clear();
}

// Where the previous build saved its data: NSUserDefaults.
static void WriteSaved(const char*, const std::string& bytes)
{
    stored = bytes;
    storedResult = kTVOSStorage_OK;
}

static void TestLegacyCacheMigration()
{
    const auto legacy = gFolder / "migration-test";
    const std::string valid("MAGIC\0old!", 10);
    char payload[4] = {};
    auto load = [&] { return LoadUserDataFile("migration-test", "MAGIC", 4, payload); };
    Check(load() == fnfErr && writes == 0, "first launch should have no data to migrate");
    for (const auto& corrupt : {std::string("short"), std::string("WRONG\0old!", 10)})
    {
        WriteFile("migration-test", corrupt);
        Check(load() == badFileFormat && writes == 0, "corrupt legacy data was migrated");
    }
    WriteFile("migration-test", valid);
    failWrite = true;
    Check(load() == noErr && std::string(payload, 4) == "old!", "failed persistence lost usable legacy save");
    Check(fs::exists(legacy) && storedResult == kTVOSStorage_NotFound, "write failure lost retry source");
    failWrite = false;
    Check(load() == noErr && writes == 2 && stored == valid, "legacy save did not retry migration");
    Check(fs::exists(legacy), "asynchronous defaults write deleted the recovery file");
    WriteFile("migration-test", std::string("MAGIC\0stale", 11));
    Check(load() == noErr && std::string(payload, 4) == "old!" && writes == 2, "canonical data did not take precedence");
    storedResult = kTVOSStorage_Corrupt;
    Check(load() == badFileFormat && writes == 2, "corrupt canonical save was replaced by stale legacy data");
    Check(LoadUserDataFile("migration-test", "MAGIC", LONG_MAX, payload) == paramErr, "overflow length accepted");
}
#else
static void ResetStorage() {}

// Where the previous build saved its data: the prefs folder.
static void WriteSaved(const char* name, const std::string& bytes)
{
    WriteFile(name, bytes);
}
#endif

// Every v1 setting differs from the zeroed test defaults, so a field the upgrade drops
// shows. Filled in place: padding must stay zero for the byte comparisons below.
static void MakeCustomV1Prefs(PrefsTypeV1& v1)
{
    memset(&v1, 0, sizeof(v1));
    v1.difficulty = 3;
    v1.splitScreenMode2P = 4;
    v1.splitScreenMode3P = 5;
    v1.language = 6;
    v1.tagDuration = 7;
    v1.antialiasingLevel = 8;
    v1.fullscreen = 9;
    v1.displayNumMinus1 = 10;
    v1.musicVolumePercent = 11;
    v1.sfxVolumePercent = 12;
    v1.raceTimer = 13;
    unsigned char* bindingBytes = reinterpret_cast<unsigned char*>(v1.bindings);
    for (size_t i = 0; i < sizeof(v1.bindings); i++)
        bindingBytes[i] = (unsigned char)(i * 7 + 1);
    v1.gamepadRumble = 14;
    v1.tournamentProgression.numTracksCompleted = 15;
    for (int track = 0; track < NUM_RACE_TRACKS; track++)
        for (int lap = 0; lap < LAPS_PER_RACE; lap++)
            v1.tournamentProgression.tournamentLapTimes[track][lap] = 60.0f + track * LAPS_PER_RACE + lap;
    SDL_strlcpy(v1.playerName, "GRAG THE GREAT", sizeof(v1.playerName));
}

// v2 only appended fields, so a v1 payload is the start of a PrefsType.
static_assert(offsetof(PrefsType, cpuFill) == sizeof(PrefsTypeV1), "v2 appends to the v1 layout");

static void TestPrefsMigration()
{
    ResetStorage();
    PrefsTypeV1 v1;
    MakeCustomV1Prefs(v1);
    PrefsType defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.cpuFill = false;
    PrefsType prefs;
    PrefsType reloaded;
    Boolean upgraded = true;

    Check(LoadPrefsFile("prefs-test", &prefs, &defaults, &upgraded) == fnfErr && !upgraded,
        "first launch should find no prefs");

    // A v1 save keeps every setting; the new ones start from their defaults.
    WriteSaved("prefs-test", Blob(PREFS_MAGIC_V1, v1));
    memset(&prefs, 0xA5, sizeof(prefs));
    Check(LoadPrefsFile("prefs-test", &prefs, &defaults, &upgraded) == noErr && upgraded, "v1 prefs did not load");
    Check(memcmp(&prefs, &v1, sizeof(v1)) == 0, "the upgrade lost a v1 setting");
    Check(prefs.cpuFill == false, "upgraded prefs must keep CPU fill off");

    // The current layout round-trips unchanged.
    prefs.cpuFill = true;
    Check(SaveUserDataFile("prefs-test", PREFS_MAGIC, sizeof(prefs), (Ptr) &prefs) == noErr, "v2 save failed");
    Check(LoadPrefsFile("prefs-test", &reloaded, &defaults, &upgraded) == noErr && !upgraded, "v2 prefs did not load");
    Check(memcmp(&reloaded, &prefs, sizeof(prefs)) == 0, "v2 prefs did not round-trip");

    // Short, mislabeled or mis-sized saves are rejected, not half-loaded.
    const std::string current = Blob(PREFS_MAGIC, prefs);
    const std::string old = Blob(PREFS_MAGIC_V1, v1);
    for (const auto& corrupt : {
            std::string(),
            std::string("CMR"),
            current.substr(0, current.size() - 1),
            old.substr(0, old.size() - 1),
            old + std::string(1, '\0'),
            Blob(PREFS_MAGIC_V1, prefs),
            Blob(PREFS_MAGIC, v1),
            Blob("CMR Prefs v3   ", prefs)})
    {
        WriteSaved("prefs-test", corrupt);
        Check(LoadPrefsFile("prefs-test", &reloaded, &defaults, &upgraded) == badFileFormat && !upgraded,
            "corrupt prefs were accepted");
    }

#if defined(CMR_USE_TVOS_STORAGE)
    // A v1 save still in the legacy Caches file migrates too. It is copied to
    // NSUserDefaults as is (the file stays as the recovery copy) and upgraded in memory.
    ResetStorage();
    WriteFile("prefs-test", old);
    Check(LoadPrefsFile("prefs-test", &prefs, &defaults, &upgraded) == noErr && upgraded,
        "legacy v1 prefs did not load");
    Check(memcmp(&prefs, &v1, sizeof(v1)) == 0 && prefs.cpuFill == false, "legacy v1 upgrade lost a setting");
    Check(writes == 1 && stored == old, "legacy v1 prefs were not persisted");
    Check(fs::exists(gFolder / "prefs-test"), "legacy prefs file was deleted");

    // Once the upgraded prefs are saved, they take precedence over the legacy file.
    Check(SaveUserDataFile("prefs-test", PREFS_MAGIC, sizeof(prefs), (Ptr) &prefs) == noErr, "v2 save failed");
    Check(LoadPrefsFile("prefs-test", &reloaded, &defaults, &upgraded) == noErr && !upgraded
        && memcmp(&reloaded, &prefs, sizeof(prefs)) == 0, "saved v2 prefs did not take precedence");

    storedResult = kTVOSStorage_Corrupt;
    Check(LoadPrefsFile("prefs-test", &reloaded, &defaults, &upgraded) == badFileFormat && !upgraded,
        "corrupt canonical prefs were replaced by legacy data");
#endif
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "scratch directory required");
        const fs::path root = argv[1];
        // Match the production prefs subdirectory, supplied by the CMake target.
        gFolder = root / CMR_TEST_PREFS_FOLDER;
        fs::create_directories(gFolder);
        Pomme::Files::Init();
        const FSSpec parent = Pomme::Files::HostPathToFSSpec(root / "placeholder");
        gPrefsFolderVRefNum = parent.vRefNum;
        gPrefsFolderDirID = parent.parID;
#if defined(CMR_USE_TVOS_STORAGE)
        TestLegacyCacheMigration();
        std::cout << "tvOS migration tests passed\n";
#endif
        TestPrefsMigration();
        std::cout << "prefs migration tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
