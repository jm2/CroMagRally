cmake_minimum_required(VERSION 3.21)

set(GAME_TARGET				"CroMagRally")
set(GAME_FULL_NAME			"Cro-Mag Rally")
set(GAME_VERSION			"3.1.0")
set(GAME_IDENTIFIER			"io.jor.cromagrally")
set(GAME_COPYRIGHT			"© 2000 Pangea Software, Inc., © 2025 Iliyas Jorio.")

set(GAME_DATADIR			"${CMAKE_SOURCE_DIR}/Data")
set(GAME_SRCDIR				"${CMAKE_SOURCE_DIR}/Source")

set(CMAKE_CXX_STANDARD		20)
set(CMAKE_C_STANDARD		17)

# Platform-specific deployment targets
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS deployment version")
	set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target iOS architectures")
elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum tvOS deployment version")
	set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target tvOS architectures")
else()
	set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
	set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target macOS architectures")
endif()

project(${GAME_TARGET}
	VERSION ${GAME_VERSION}
	LANGUAGES C CXX)

option(BUILD_SDL_FROM_SOURCE "Build SDL from the extern/SDL3 submodule" ON)
set(CMR_SDL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/extern/SDL3" CACHE PATH
	"Path to the SDL source tree used when BUILD_SDL_FROM_SOURCE is enabled")

set(_sdl_static_default OFF)
if(APPLE AND BUILD_SDL_FROM_SOURCE)
	# Source-built SDL is easiest to package as part of an Apple app bundle when linked
	# statically. Mobile builds require this, and it also removes the desktop build's
	# dependency on a separately downloaded SDL3.framework.
	set(_sdl_static_default ON)
endif()
option(SDL_STATIC "Static link SDL" ${_sdl_static_default})
unset(_sdl_static_default)

option(SANITIZE "Build with asan/ubsan" OFF)

include(CTest)

if(WIN32 OR APPLE)
	# Don't warn
elseif(SANITIZE)
	message("Sanitizers enabled")
else()
	message("Sanitizers disabled (pass -DSANITIZE=1 to enable)")
endif()

#------------------------------------------------------------------------------
# GLOBAL OPTIONS (BEFORE ADDING SUBDIRECTORIES)
#------------------------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(MSVC)
	# This lets us detect that MSVC supports C++20 features via the __cplusplus macro
	# (See: https://docs.microsoft.com/en-us/cpp/build/reference/zc-cplusplus)
	add_compile_options(/Zc:__cplusplus)
	add_compile_definitions(UNICODE _UNICODE)

	set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${GAME_TARGET})
elseif(APPLE AND NOT BUILD_SDL_FROM_SOURCE)
	# Let the game (and Pomme) find headers in an explicitly requested prebuilt framework.
	set(CMAKE_XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS extern)
	add_compile_options("-F${CMAKE_SOURCE_DIR}/extern")  # for non-xcode IDEs
endif()

set(POMME_NO_GRAPHICS	true)
set(POMME_NO_INPUT		true)
set(POMME_NO_MP3		true)
set(POMME_NO_QD3D		true)
set(POMME_NO_VIDEO		true)
add_compile_definitions(POMME_CASE_SENSITIVE_FSSPEC=1)

# CMR7 determinism: forbid the compiler from contracting a*b+c into a fused multiply-add.
# FMA rounds differently on ARM vs x86, so a contracted expression can flip a comparison and
# change how many synced-RNG values a frame draws — which trips the lockstep per-frame seed
# check between heterogeneous peers (e.g. an Apple-Silicon host and an x86 client). MSVC does
# not contract by default, so this only applies to GCC / Clang / Apple-clang.
if(NOT MSVC)
	add_compile_options(-ffp-contract=off)
endif()

# Android 16 KB page-size support. Google Play requires API-35+ apps to support 16 KB
# memory pages, and devices already running in 16 KB page mode crash when loading a
# 4 KB-aligned shared object. NDK r27 still links 4 KB-aligned by default, so force
# 16 KB ELF segment alignment for every shared object we ship -- the game's
# libCroMagRally.so and the from-source libSDL3.so. Directory-scoped, so it propagates
# to the SDL subdirectory added below; a harmless no-op for the static libs.
if(ANDROID)
	add_link_options("-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
endif()

#------------------------------------------------------------------------------
# FIND SDL
#------------------------------------------------------------------------------

if(NOT BUILD_SDL_FROM_SOURCE)
	find_package(SDL3 CONFIG REQUIRED)
	message("Found external SDL3 package")
else()
	# Build the same submodule checkout on every supported platform. Keep the binary
	# directory stable so packaging scripts never depend on an SDL release number.
	if(NOT EXISTS "${CMR_SDL_SOURCE_DIR}/CMakeLists.txt")
		message(FATAL_ERROR
			"SDL3 source is missing at ${CMR_SDL_SOURCE_DIR}. "
			"Run: git submodule update --init --recursive")
	endif()

	set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
	set(SDL_TESTS OFF CACHE BOOL "" FORCE)
	set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
	if(SDL_STATIC)
		set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	else()
		set(SDL_SHARED ON CACHE BOOL "" FORCE)
	endif()

	message("Building SDL3 from source: " ${CMR_SDL_SOURCE_DIR})
	add_subdirectory("${CMR_SDL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/extern/SDL3" EXCLUDE_FROM_ALL)
endif()

#------------------------------------------------------------------------------
# DEPENDENCIES
#------------------------------------------------------------------------------

if(NOT DEFINED POMME_BUILD_TESTING)
	set(POMME_BUILD_TESTING OFF)
	if(BUILD_TESTING AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
		set(POMME_BUILD_TESTING ON)
	endif()
endif()
add_subdirectory(extern/Pomme)

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	# Configure gl4es for mobile (OpenGL ES 2.0)
	set(STATICLIB ON CACHE BOOL "" FORCE)
	set(DEFAULT_ES 2 CACHE STRING "" FORCE)
	set(NO_GBM ON CACHE BOOL "" FORCE)
	set(NOX11 ON CACHE BOOL "" FORCE)
	set(NO_INIT_CONSTRUCTOR ON CACHE BOOL "" FORCE)
	set(NOEGL ON CACHE BOOL "" FORCE)
	
	if(ANDROID)
		set(ANDROID ON CACHE BOOL "" FORCE)
		set(USE_ANDROID_LOG ON CACHE BOOL "" FORCE)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
		set(IOS ON CACHE BOOL "" FORCE)
		add_compile_definitions(__IOS__)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
		set(TVOS ON CACHE BOOL "" FORCE)
		add_compile_definitions(__TVOS__)
	endif()
	
	# Build gl4es as a static lib
	add_subdirectory(extern/gl4es)

	# gl4es hard-codes its archive output to ${CMAKE_SOURCE_DIR}/lib, which resolves to the
	# repo root here -- a single directory shared by every per-arch/SDK build tree, so one
	# configuration's libGL.a would clobber another's. (The build scripts used to paper over
	# this with `rm -rf lib`.) Redirect the static lib into this build tree so each
	# configuration keeps its own copy and no stray repo-root lib/ is ever created.
	if(TARGET GL)
		set_target_properties(GL PROPERTIES
			ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gl4es"
			LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gl4es")
	endif()
endif()

if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	find_package(OpenGL REQUIRED)
endif()

#------------------------------------------------------------------------------
# EXECUTABLE TARGET
#------------------------------------------------------------------------------

# Write header file containing version info
configure_file(${GAME_SRCDIR}/Headers/version.h.in ${GAME_SRCDIR}/Headers/version.h)

# Glob all source files
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${GAME_SRCDIR}/*.c ${GAME_SRCDIR}/*.cpp ${GAME_SRCDIR}/*.h)

# Make source groups match file tree
source_group(TREE ${GAME_SRCDIR} PREFIX "" FILES ${GAME_SOURCES})

if(WIN32)
	# Windows resource file
	configure_file(${CMAKE_SOURCE_DIR}/packaging/win32.rc.in ${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.exe.rc)
	list(APPEND GAME_SOURCES ${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.exe.rc)
elseif(APPLE)
	list(APPEND GAME_SOURCES ${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.icns)
	set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

if(ANDROID)
	add_library(${GAME_TARGET} SHARED ${GAME_SOURCES})
else()
	add_executable(${GAME_TARGET} ${GAME_SOURCES})
endif()

target_include_directories(${GAME_TARGET} PRIVATE ${GAME_SRCDIR}/Headers)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	target_include_directories(${GAME_TARGET} PRIVATE extern/gl4es/include)
endif()

#------------------------------------------------------------------------------
# COMPILER/LINKER OPTIONS
#------------------------------------------------------------------------------

add_compile_definitions(
	"$<$<CONFIG:DEBUG>:_DEBUG>"
)

target_compile_definitions(${GAME_TARGET} PRIVATE
	HQ_TERRAIN=1
	ENABLE_LOADING_SCREEN=0
	GL_SILENCE_DEPRECATION)

if(NOT MSVC)
	target_compile_options(${GAME_TARGET} PRIVATE
		-fexceptions
		-Wall
		-Wextra
		-Wshadow
		-Werror=return-type
		$<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>
		-Wno-multichar
		-Wno-unknown-pragmas
		-Wno-unused-parameter
		-Wstrict-aliasing=2
	)

	# Sanitizers in debug mode (Linux only)
	# When using a debugger, you should export LSAN_OPTIONS=detect_leaks=0
	if(SANITIZE)
		target_compile_options(${GAME_TARGET} PRIVATE
			-fsanitize=alignment
			-fsanitize=address
			-fsanitize=leak
			-fsanitize=undefined
			-fno-omit-frame-pointer
		)
	endif()
else()
	target_compile_definitions(${GAME_TARGET} PRIVATE
		WIN32_LEAN_AND_MEAN
		NOGDI				# don't clash with Mac function names
		NOUSER				# don't clash with Mac function names
	)

	target_compile_options(${GAME_TARGET} PRIVATE
		/EHs	# synchronous exceptions; also, extern "C" functions may throw exceptions
		/W4
		/wd4068 # ignore unrecognized pragmas
		/wd4100 # unreferenced formal parameters
		/wd4200 # nonstandard extension (zero-sized array in struct/union)
		/wd4201 # nonstandard extension (nameless struct)
		/wd4244 # conversion from double to float
		/wd4305 # truncation from double to float
		/wd5105 # see https://developercommunity.visualstudio.com/t/1249671
		/MP		# multiprocessor build
		/Zi		# output info to PDB
	)

	# Let executable be debugged with PDB, even in Release builds
	target_link_options(${GAME_TARGET} PRIVATE /DEBUG)

	# Enable console for debug builds
	set_target_properties(${GAME_TARGET} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
	set_target_properties(${GAME_TARGET} PROPERTIES COMPILE_DEFINITIONS_DEBUG "_CONSOLE")
endif()

#------------------------------------------------------------------------------
# LINK LIBRARIES
#------------------------------------------------------------------------------

# Link sanitizers first
if(SANITIZE)
	target_link_libraries(${GAME_TARGET} PRIVATE asan ubsan)
endif()

# Explicitly link math lib on Linux
if(NOT APPLE AND NOT WIN32)
	target_link_libraries(${GAME_TARGET} PRIVATE m)
endif()

# Link SDL
if(NOT SDL_STATIC)
	target_link_libraries(${GAME_TARGET} PRIVATE SDL3::SDL3)
else()
	target_link_libraries(${GAME_TARGET} PRIVATE SDL3::SDL3-static)
endif()

# Link core dependencies
target_link_libraries(${GAME_TARGET} PRIVATE Pomme)
if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	target_link_libraries(${GAME_TARGET} PRIVATE OpenGL::GL)
endif()

if(WIN32)
	target_link_libraries(${GAME_TARGET} PRIVATE ws2_32 wlanapi)
endif()

# Add required frameworks for KillMacMouseAcceleration and WiFi Detection
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	target_link_libraries(${GAME_TARGET} PRIVATE "-framework Foundation" "-framework IOKit" "-framework SystemConfiguration")
endif()

# iOS-specific frameworks and libraries
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	target_link_libraries(${GAME_TARGET} PRIVATE
		"-framework Foundation"
		"-framework UIKit"
		"-framework OpenGLES"
		"-framework QuartzCore"
		"-framework CoreMotion"
		"-framework GameController"
		"-framework AudioToolbox"
		"-framework AVFoundation"
		"-framework CoreAudio"
		"-framework CoreGraphics"
		"-framework Metal"
	)
	# Link gl4es static library
	target_link_libraries(${GAME_TARGET} PRIVATE GL)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	target_link_libraries(${GAME_TARGET} PRIVATE
		"-framework Foundation"
		"-framework UIKit"
		"-framework OpenGLES"
		"-framework QuartzCore"

		"-framework GameController"
		"-framework AudioToolbox"
		"-framework AVFoundation"
		"-framework CoreAudio"
		"-framework CoreGraphics"
		"-framework Metal"
	)
	# Link gl4es static library
	target_link_libraries(${GAME_TARGET} PRIVATE GL)
endif()

if(ANDROID)
	# Link gl4es static library
	target_link_libraries(${GAME_TARGET} PRIVATE log android OpenSLES GL dl)
endif()

#------------------------------------------------------------------------------
# PLATFORM-SPECIFIC PROPERTIES
#------------------------------------------------------------------------------

set_target_properties(${GAME_TARGET} PROPERTIES
	#--------------------------------------------------------------------------
	# MSVC/WIN32
	#--------------------------------------------------------------------------

	WIN32_EXECUTABLE					TRUE					# GUI application instead of console application
	VS_DEBUGGER_WORKING_DIRECTORY		"${CMAKE_SOURCE_DIR}"
	VS_DPI_AWARE						"PerMonitor"

	#--------------------------------------------------------------------------
	# APPLE
	#--------------------------------------------------------------------------

	# Build it as an .app bundle
	MACOSX_BUNDLE						TRUE

	# Set framework search path to (App bundle)/Contents/Frameworks for legacy prebuilt-framework builds.
	XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/../Frameworks"

	# Explicitly turn off code signing, otherwise downloaded app will be quarantined forever
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""

	# Set up Info.plist values
	MACOSX_BUNDLE_ICON_FILE				"${GAME_TARGET}.icns"	# CFBundleIconFile
	MACOSX_BUNDLE_EXECUTABLE_NAME		${GAME_TARGET}			# CFBundleExecutable - executable name inside the bundle
	MACOSX_BUNDLE_SHORT_VERSION_STRING	${GAME_VERSION}			# CFBundleShortVersionString
	MACOSX_BUNDLE_COPYRIGHT				${GAME_COPYRIGHT}		# NSHumanReadableCopyright (supersedes CFBundleGetInfoString (MACOSX_BUNDLE_INFO_STRING))
	MACOSX_BUNDLE_BUNDLE_NAME			${GAME_FULL_NAME}		# CFBundleName - user-visible short name for the bundle, up to 15 characters
	MACOSX_BUNDLE_GUI_IDENTIFIER		${GAME_IDENTIFIER}		# CFBundleIdentifier - unique bundle ID in reverse-DNS format

	# If CODE_SIGN_IDENTITY is NOT empty: tell Xcode to codesign the app properly
	# Otherwise, if it's empty: explicitly turn off code signing, otherwise downloaded app will be quarantined forever
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${CODE_SIGN_IDENTITY}"
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY[variant=Debug] ""		# don't bother signing debug build

	# Bundle ID required for code signing - must match CFBundleIdentifier otherwise xcode will complain
	XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${GAME_IDENTIFIER}

	# Don't bother with universal builds when we're working on the debug version
	XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"

	XCODE_ATTRIBUTE_COPY_PHASE_STRIP[variant=Debug]	"NO"		# avoid "skipping copy phase strip" warning while working on Debug config
)

# A prebuilt Apple framework must be embedded. Source builds are static on Apple.
if(APPLE AND NOT BUILD_SDL_FROM_SOURCE AND NOT SDL_STATIC)
	set_target_properties(${GAME_TARGET} PROPERTIES
		XCODE_EMBED_FRAMEWORKS "extern/SDL3.framework"
		XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY "YES"
		XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY "YES"
	)
endif()

# iOS-specific bundle properties
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	set_target_properties(${GAME_TARGET} PROPERTIES
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/iOSBuild/CroMagRally/Info.plist"
		XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"	# iPhone and iPad
		XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "13.0"
		XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
		XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchStoryboardName "LaunchScreen"
		XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${GAME_IDENTIFIER}
	)
	
	# Add iOS resources to bundle
	target_sources(${GAME_TARGET} PRIVATE
		"${CMAKE_SOURCE_DIR}/iOSBuild/CroMagRally/LaunchScreen.storyboard"
		"${CMAKE_SOURCE_DIR}/iOSBuild/CroMagRally/Assets.xcassets"
		"${GAME_DATADIR}"
	)
	set_source_files_properties(
		"${CMAKE_SOURCE_DIR}/iOSBuild/CroMagRally/LaunchScreen.storyboard"
		"${CMAKE_SOURCE_DIR}/iOSBuild/CroMagRally/Assets.xcassets"
		"${GAME_DATADIR}"
		PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
	)
endif()

# tvOS-specific bundle properties
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	set_target_properties(${GAME_TARGET} PROPERTIES
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/tvOSBuild/CroMagRally/Info.plist"
		XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "3"	# Apple TV
		XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET "13.0"
		XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "App Icon & Top Shelf Image"
		XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchStoryboardName "LaunchScreen"
		XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${GAME_IDENTIFIER}
	)
	
	# Add tvOS resources to bundle
	target_sources(${GAME_TARGET} PRIVATE
		"${CMAKE_SOURCE_DIR}/tvOSBuild/CroMagRally/LaunchScreen.storyboard"
		"${CMAKE_SOURCE_DIR}/tvOSBuild/CroMagRally/Assets.xcassets"
		"${GAME_DATADIR}"
	)
	set_source_files_properties(
		"${CMAKE_SOURCE_DIR}/tvOSBuild/CroMagRally/LaunchScreen.storyboard"
		"${CMAKE_SOURCE_DIR}/tvOSBuild/CroMagRally/Assets.xcassets"
		"${GAME_DATADIR}"
		PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
	)
endif()

#------------------------------------------------------------------------------
# POST-BUILD (COPY ASSETS, ETC.)
#------------------------------------------------------------------------------

if(APPLE)
	if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
		set(GAME_DATA_TARGET_LOCATION "$<TARGET_FILE_DIR:${GAME_TARGET}>")
	else()
		set(GAME_DATA_TARGET_LOCATION "$<TARGET_FILE_DIR:${GAME_TARGET}>/../Resources")
	endif()
else()
	set(GAME_DATA_TARGET_LOCATION "$<TARGET_FILE_DIR:${GAME_TARGET}>/Data")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${GAME_DATADIR}" "${GAME_DATA_TARGET_LOCATION}")
endif()

#------------------------------------------------------------------------------
# TESTS
#------------------------------------------------------------------------------

if(BUILD_TESTING AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	add_executable(CroMagRallyValidationTests
		Tests/ValidationTests.c
		Source/Network/NetValidation.c
		Source/System/DeterministicMath.c
		Source/System/UserDataValidation.c
		Source/System/TextUtils.c)
	target_include_directories(CroMagRallyValidationTests PRIVATE ${GAME_SRCDIR}/Headers)
	if(MSVC)
		target_compile_definitions(CroMagRallyValidationTests PRIVATE
			WIN32_LEAN_AND_MEAN
			NOGDI
			NOUSER)
	endif()
	if(SANITIZE)
		target_compile_options(CroMagRallyValidationTests PRIVATE
			-fsanitize=alignment,address,leak,undefined
			-fno-omit-frame-pointer)
		target_link_libraries(CroMagRallyValidationTests PRIVATE asan ubsan)
	endif()
	target_link_libraries(CroMagRallyValidationTests PRIVATE Pomme SDL3::SDL3)
	if(NOT APPLE AND NOT WIN32)
		target_link_libraries(CroMagRallyValidationTests PRIVATE m)
	endif()
	add_test(NAME validation COMMAND CroMagRallyValidationTests)
endif()

# macOS-specific post-build (skip for iOS/tvOS)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
	set(INFO_PLIST "${GAME_DATA_TARGET_LOCATION}/../Info.plist")

	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		# Copy extra resources to app bundle
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/packaging/mainmenu.nib ${GAME_DATA_TARGET_LOCATION}/mainmenu.nib

		# Info.plist: high-DPI support, Game Mode, honor mainmenu.nib
		COMMAND plutil -replace NSHighResolutionCapable -bool true ${INFO_PLIST}
		COMMAND plutil -replace LSApplicationCategoryType -string "public.app-category.games" ${INFO_PLIST}
		COMMAND plutil -replace NSMainNibFile -string "mainmenu" ${INFO_PLIST}
	)
endif()

# Windows-specific libraries
if(WIN32)
	# Source-built SDL lives in a subdirectory. Put its runtime DLL beside every
	# executable so local runs, CTest, and packaging all use the same layout.
	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:${GAME_TARGET}> $<TARGET_FILE_DIR:${GAME_TARGET}>
		COMMAND_EXPAND_LISTS)
	if(TARGET CroMagRallyValidationTests)
		add_custom_command(TARGET CroMagRallyValidationTests POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_RUNTIME_DLLS:CroMagRallyValidationTests> $<TARGET_FILE_DIR:CroMagRallyValidationTests>
			COMMAND_EXPAND_LISTS)
	endif()

	# When installing (cmake --install), copy Visual Studio redistributable DLLs to install location
	include(InstallRequiredSystemLibraries)
endif()

# Copy documentation to output folder
configure_file(${CMAKE_SOURCE_DIR}/packaging/ReadMe.txt.in ${CMAKE_CURRENT_BINARY_DIR}/ReadMe.txt)

#------------------------------------------------------------------------------
# LINUX FHS INSTALL + deb/rpm PACKAGING (opt-in; no effect on other builds)
#------------------------------------------------------------------------------
# Enabled with -DCMR_INSTALL=ON for the Linux native packages: deb/rpm via CPack,
# Arch via packaging/PKGBUILD, Flatpak via packaging/flatpak/io.jor.cromagrally.yml.
# When OFF (the default) nothing below runs, so the AppImage / Windows / macOS /
# iOS / tvOS / Android builds are byte-for-byte unaffected.
option(CMR_INSTALL "Generate FHS install rules + deb/rpm (CPack) packaging" OFF)

# Copy SDL3 into the package's own private libdir. ON for self-contained packages that
# build the SDL submodule from source. Turn OFF when a system
# SDL3 is used -- Arch ships 'sdl3'; Flatpak's SDL3 module already lives in /app/lib --
# so we never ship a stray copy of a library the platform already provides.
option(CMR_BUNDLE_SDL "Bundle SDL3 into the package's private libdir" ON)

if(CMR_INSTALL)
	if(NOT UNIX OR APPLE)
		message(FATAL_ERROR "CMR_INSTALL is only supported for Linux (FHS) packaging")
	endif()

	# Deliberately the literal "lib/cromagrally", NOT GNUInstallDirs/CMAKE_INSTALL_LIBDIR:
	# on Fedora/Arch the latter is "lib64", but the launcher wrapper hard-codes "lib" and
	# the $ORIGIN rpath makes the lib-vs-lib64 distinction irrelevant for the private payload.
	set(CMR_PRIVATE_LIBDIR "lib/cromagrally")

	# The installed binary finds its sibling/bundled SDL via $ORIGIN; for system-SDL builds
	# (Arch/Flatpak) $ORIGIN is harmless and the loader falls through to the standard paths.
	set_target_properties(${GAME_TARGET} PROPERTIES
		INSTALL_RPATH "$ORIGIN"
		INSTALL_RPATH_USE_LINK_PATH OFF		# don't bake the CI's from-source SDL path into the binary
		BUILD_WITH_INSTALL_RPATH OFF)		# keep the build-tree rpath so in-place runs still work

	# Executable (arch-dependent payload)
	install(TARGETS ${GAME_TARGET} RUNTIME DESTINATION ${CMR_PRIVATE_LIBDIR})

	# Bundled SDL3 (self-contained packages only). A source build exposes the real
	# SDL3-shared target; a system package exposes an imported SDL3::SDL3-shared target.
	if(CMR_BUNDLE_SDL AND TARGET SDL3-shared)
		install(TARGETS SDL3-shared
			LIBRARY DESTINATION ${CMR_PRIVATE_LIBDIR}
			RUNTIME DESTINATION ${CMR_PRIVATE_LIBDIR})
	elseif(CMR_BUNDLE_SDL AND TARGET SDL3::SDL3-shared)
		set(_sdl_so "")
		get_target_property(_sdl_cfgs SDL3::SDL3-shared IMPORTED_CONFIGURATIONS)
		if(_sdl_cfgs)
			foreach(_c IN LISTS _sdl_cfgs)
				get_target_property(_loc SDL3::SDL3-shared IMPORTED_LOCATION_${_c})
				if(_loc)
					set(_sdl_so "${_loc}")
					break()
				endif()
			endforeach()
		endif()
		if(NOT _sdl_so)
			get_target_property(_sdl_so SDL3::SDL3-shared IMPORTED_LOCATION)
		endif()
		if(_sdl_so)
			get_filename_component(_sdl_libdir "${_sdl_so}" DIRECTORY)
			install(DIRECTORY "${_sdl_libdir}/" DESTINATION ${CMR_PRIVATE_LIBDIR}
				FILES_MATCHING PATTERN "libSDL3.so*")
		else()
			message(WARNING "CMR_BUNDLE_SDL is ON but SDL3::SDL3-shared has no IMPORTED_LOCATION; SDL will not be bundled")
		endif()
	endif()

	# Launcher wrapper -> <prefix>/bin/cromagrally
	install(PROGRAMS "${CMAKE_SOURCE_DIR}/packaging/cromagrally-launcher"
		DESTINATION bin RENAME cromagrally)

	# Game assets -> <prefix>/share/cromagrally/Data (no trailing slash keeps the Data/ level,
	# which is exactly what the launcher's cwd-relative lookup expects). Normalize permissions
	# so packaged files are a clean 0644 / dirs 0755 regardless of the source tree.
	install(DIRECTORY "${GAME_DATADIR}" DESTINATION share/cromagrally
		FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
		DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

	# Desktop entry: patch Exec=CroMagRally -> Exec=cromagrally (the installed wrapper). Patch a
	# build-dir copy so the committed .desktop stays intact for the AppImage path in build.py.
	file(READ "${CMAKE_SOURCE_DIR}/packaging/${GAME_IDENTIFIER}.desktop" _cmr_desktop)
	string(REPLACE "Exec=${GAME_TARGET}" "Exec=cromagrally" _cmr_desktop "${_cmr_desktop}")
	file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/xdg/${GAME_IDENTIFIER}.desktop" CONTENT "${_cmr_desktop}")
	install(FILES "${CMAKE_BINARY_DIR}/xdg/${GAME_IDENTIFIER}.desktop" DESTINATION share/applications)

	# AppStream metainfo (modern .metainfo.xml name)
	install(FILES "${CMAKE_SOURCE_DIR}/packaging/${GAME_IDENTIFIER}.appdata.xml"
		DESTINATION share/metainfo RENAME "${GAME_IDENTIFIER}.metainfo.xml")

	# Icon: the 512x512 "alternate" (wheel/name) artwork, installed under the app-id name so
	# the desktop file's Icon=io.jor.cromagrally resolves via the hicolor theme.
	install(FILES "${CMAKE_SOURCE_DIR}/packaging/${GAME_IDENTIFIER}-alternateicon.png"
		DESTINATION share/icons/hicolor/512x512/apps RENAME "${GAME_IDENTIFIER}.png")

	# Docs + license
	install(FILES "${CMAKE_SOURCE_DIR}/CHANGELOG.md" "${CMAKE_SOURCE_DIR}/README.md"
		DESTINATION share/doc/cromagrally)
	install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.md"
		DESTINATION share/licenses/cromagrally)		# Arch convention; harmless elsewhere

	#--------------------------------------------------------------------------
	# CPack: one config emits both .deb and .rpm (Arch/Flatpak don't use CPack)
	#--------------------------------------------------------------------------
	set(CPACK_PACKAGE_NAME "cromagrally")
	set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
	set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The wildest racing game since man invented the wheel!")
	set(CPACK_PACKAGE_VENDOR "Pangea Software, Inc. / Iliyas Jorio")
	set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jorio/CroMagRally")
	set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.md")
	set(CPACK_STRIP_FILES ON)
	set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
	# DEB + RPM both require a non-empty maintainer/contact. Override at configure time with
	# -DCPACK_PACKAGE_CONTACT="Your Name <you@example.com>" when packaging from a fork.
	if(NOT CPACK_PACKAGE_CONTACT)
		set(CPACK_PACKAGE_CONTACT "John-Michael Mulesa <jmulesa@gmail.com>")
	endif()

	# ---- DEB ----
	set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
	set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
	set(CPACK_DEBIAN_PACKAGE_SECTION "games")
	set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
	set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
	# CRITICAL: dpkg-shlibdeps would abort generation on the private, non-system libSDL3.so.0,
	# so it's off and we declare runtime deps by hand.
	set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
	set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libgcc-s1, libgl1")
	# SDL dlopen()s these at runtime (not in DT_NEEDED), and only one of X11/Wayland is needed
	# on any given host, so they are Recommends rather than hard Depends.
	set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libx11-6, libxext6, libxcursor1, libxi6, libxrandr2, libxfixes3, libwayland-client0, libxkbcommon0, libasound2 | libasound2t64, libpulse0")

	# ---- RPM ----
	set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
	set(CPACK_RPM_PACKAGE_LICENSE "CC-BY-NC-SA-4.0")
	set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
	set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
	# Keep autoreqprov ON: the bundled libSDL3's self-Provides cancels the exe's self-Require,
	# and we still get correct libc/libstdc++/libGL auto-Requires for free. The dlopen'd sonames
	# aren't in DT_NEEDED, so append them by hand (the (64bit) marker covers x86_64 and aarch64).
	set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
	set(CPACK_RPM_PACKAGE_REQUIRES "libX11.so.6()(64bit), libwayland-client.so.0()(64bit), libxkbcommon.so.0()(64bit), libasound.so.2()(64bit), libpulse.so.0()(64bit)")

	set(CPACK_GENERATOR "DEB;RPM")
	include(CPack)
endif()
