#pragma once

#include <filesystem>

// Read APK assets through SDL and atomically replace the extracted Data directory
// when the generated content identity differs from the last successful extraction.
void ExtractAndroidAssets(const std::filesystem::path& destinationRoot);
