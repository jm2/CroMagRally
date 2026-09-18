#pragma once

#include "CompilerSupport/filesystem.h"

// Read APK assets through SDL and atomically replace the extracted Data directory
// when the generated content identity differs from the last successful extraction.
void ExtractAndroidAssets(const fs::path& destinationRoot);
