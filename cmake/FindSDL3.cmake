#
# FindSDL3.cmake
#
# Wraps the find_package call.
# If SDL3::SDL3 target exists (from add_subdirectory), it sets SDL3_FOUND to TRUE.
# Otherwise it falls back to standard config search.
#

if(TARGET SDL3::SDL3)
    message(STATUS "FindSDL3: Found existing target SDL3::SDL3")
    set(SDL3_FOUND TRUE)
    # SDL3Config usually sets these, Pomme might assume them?
    # Actually Pomme just links to SDL3::SDL3, so we might only need the target.
    # But let's check what variables find_package often provides.
    return()
endif()

# Fallback to standard search if target doesn't exist (e.g. system install)
find_package(SDL3 CONFIG)
