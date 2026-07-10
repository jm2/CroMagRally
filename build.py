#!/usr/bin/env python3

import argparse
import contextlib
import hashlib
import glob
import multiprocessing
import os
import os.path
import platform
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

def parse_properties(path):
    properties = {}
    with open(path, encoding="utf-8") as property_file:
        for line_number, raw_line in enumerate(property_file, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            key, separator, value = line.partition("=")
            if not separator or not key or not value:
                raise ValueError(f"Malformed property at {path}:{line_number}")
            if key in properties:
                raise ValueError(f"Duplicate property {key} at {path}:{line_number}")
            properties[key] = value
    return properties

#----------------------------------------------------------------

root_dir            = os.path.dirname(os.path.abspath(__file__))
src_dir             = os.path.abspath(root_dir + "/Source")
libs_dir            = os.path.abspath(root_dir + "/extern")
dist_dir            = os.path.abspath(root_dir + "/dist")
build_dir           = os.path.abspath(root_dir + "/build")
cache_dir           = os.path.abspath(tempfile.gettempdir() + "/pangea-games-build-cache")

game_metadata       = parse_properties(root_dir + "/version.properties")
required_metadata   = ("GAME_NAME", "GAME_FULL_NAME", "GAME_IDENTIFIER", "GAME_VERSION")
missing_metadata    = [key for key in required_metadata if not game_metadata.get(key)]
if missing_metadata:
    raise ValueError(f"Missing required metadata: {', '.join(missing_metadata)}")
game_name           = game_metadata["GAME_NAME"]  # no spaces
game_name_human     = game_metadata["GAME_FULL_NAME"]  # spaces and other special characters allowed
game_package        = game_metadata["GAME_IDENTIFIER"]  # unique package identifier
game_ver            = game_metadata["GAME_VERSION"]
if len(game_ver.split(".")) != 3 or not all(part.isdigit() for part in game_ver.split(".")):
    raise ValueError(f"GAME_VERSION must be a three-part numeric version, got {game_ver!r}")
game_docs           = ["README.md", "CHANGELOG.md", "docs/Manual"]

appimagetool_ver    = "1.9.0"

lib_hashes = {  # sha-256
    "appimagetool-aarch64.AppImage":    "04f45ea45b5aa07bb2b071aed9dbf7a5185d3953b11b47358c1311f11ea94a96",
    "appimagetool-x86_64.AppImage":     "46fdd785094c7f6e545b61afcfb0f3d98d8eab243f644b4b17698c01d06083d1",
}

NPROC = multiprocessing.cpu_count()
SYSTEM = platform.system()
MACHINE = platform.machine()

if SYSTEM == "Windows":
    os.system("")  # hack to get ANSI color escapes to work

#----------------------------------------------------------------

parser = argparse.ArgumentParser(description=f"Configure, build, and package {game_name_human} {game_ver}")

if SYSTEM == "Darwin":
    default_generator = "Xcode"
    default_architecture = None
    help_configure = "generate Xcode project"
    help_build = "build app from Xcode project"
    help_package = "package up the game into a DMG"
elif SYSTEM == "Windows":
    default_generator = "Visual Studio 17 2022"
    if MACHINE == "ARM64":
        default_architecture = "ARM64"
    else:
        default_architecture = "x64"
    help_configure = f"generate {default_generator} solution"
    help_build = f"build exe from {default_generator} solution"
    help_package = "package up the game into a ZIP"
else:
    default_generator = None
    default_architecture = None
    help_configure = "generate project"
    help_build = "build binary"
    help_package = "package up the game into an AppImage"

parser.add_argument("-1", "--dependencies", default=False, action="store_true", help="step 1: validate source submodules")
parser.add_argument("-2", "--configure", default=False, action="store_true", help=f"step 2: {help_configure}")
parser.add_argument("-3", "--build", default=False, action="store_true", help=f"step 3: {help_build}")
parser.add_argument("-4", "--package", default=False, action="store_true", help=f"step 4: {help_package}")

parser.add_argument("-G", metavar="<generator>", default=default_generator,
        help=f"cmake project generator for step 2 (default: {default_generator})")

parser.add_argument("-A", metavar="<arch>", default=default_architecture,
        help=f"cmake platform name for step 2 (default: {default_architecture})")

parser.add_argument("-B", metavar="<dir>", dest="build_dir", default=build_dir,
        help=f"where to create the build directory (default: {build_dir})")

parser.add_argument("--dist-dir", metavar="<dir>", default=dist_dir,
        help=f"where to store build artifacts in step 4 (default: {os.path.relpath(dist_dir)})")

parser.add_argument("--print-artifact-name", default=False, action="store_true",
        help="print artifact name and exit")

if SYSTEM == "Darwin":
    parser.add_argument("--macos-signing-identity", metavar="<identity>",
        default="",
        help="code-sign the macOS app with this identity (default: unsigned)")

if SYSTEM == "Linux":
    parser.add_argument("--system-sdl", default=False, action="store_true",
        help="use system SDL instead of building SDL from scratch")

    parser.add_argument("--no-appimage", default=False, action="store_true",
        help="don't generate an AppImage in step 4")

    parser.add_argument("--deb", default=False, action="store_true",
        help="package as a .deb via CPack (implies a full build if no steps are given)")

    parser.add_argument("--rpm", default=False, action="store_true",
        help="package as an .rpm via CPack (implies a full build if no steps are given)")

args = parser.parse_args()

dist_dir = os.path.abspath(args.dist_dir)
build_dir = os.path.abspath(args.build_dir)

#----------------------------------------------------------------

def die(message):
    print(f"\x1b[1;31m{message}\x1b[0m", file=sys.stderr)
    sys.exit(1)

def log(message):
    print(message, file=sys.stderr)

def fatlog(message):
    starbar = len(message) * '*'
    print(f"\n{starbar}\n{message}\n{starbar}", file=sys.stderr)

def hash_file(path):
    hasher = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(64*1024)
            if not chunk:
                break
            hasher.update(chunk)
    return hasher.hexdigest()

def get_package(url):
    name = url[url.rfind('/')+1:]

    if name in lib_hashes:
        reference_hash = lib_hashes[name]
    else:
        die(f"Build script lacks reference checksum for {name}")

    path = os.path.normpath(f"{cache_dir}/{name}")
    if os.path.exists(path):
        log(f"Not redownloading: {path}")
    else:
        log(f"Downloading: {url}")
        os.makedirs(cache_dir, exist_ok=True)
        urllib.request.urlretrieve(url, path)

    actual_hash = hash_file(path)
    if reference_hash != actual_hash:
        die(f"Bad checksum for {name}: expected {reference_hash}, got {actual_hash}")

    return path

def call(cmd, **kwargs):
    cmdstr = ""
    for token in cmd:
        cmdstr += " "
        if " " in token:
            cmdstr += f"\"{token}\""
        else:
            cmdstr += token

    log(f">{cmdstr}")
    try:
        return subprocess.run(cmd, check=True, **kwargs)
    except subprocess.CalledProcessError as e:
        die(f"Aborting setup because: {e}")

def rmtree_if_exists(path):
    if os.path.exists(path):
        shutil.rmtree(path)

def rm_if_exists(path):
    with contextlib.suppress(FileNotFoundError):
        os.remove(path)

def zipdir(zipname, topleveldir, arc_topleveldir):
    with zipfile.ZipFile(zipname, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zipf:
        for root, dirs, files in os.walk(topleveldir):
            for file in files:
                filepath = os.path.join(root, file)
                arcpath = os.path.join(arc_topleveldir, filepath[len(topleveldir)+1:])
                log(f"Zipping: {arcpath}")
                zipf.write(filepath, arcpath)

#----------------------------------------------------------------

class Project:
    def __init__(self, dir_name):
        self.dir_name = dir_name
        self.gen_args = [
            "-DBUILD_SDL_FROM_SOURCE=ON",
            f"-DCMR_SDL_SOURCE_DIR={libs_dir}/SDL3",
        ]
        self.build_configs = []
        self.build_args = []

    def prepare_dependencies(self):
        pass

    def configure(self):
        fatlog(f"Configuring {self.dir_name}")

        if os.path.exists(self.dir_name):
            if not os.path.exists(self.dir_name + "/CMakeCache.txt"):
                die(f"Path exists and isn't an old build directory: {self.dir_name}")
            shutil.rmtree(self.dir_name)

        call(["cmake", "-S", ".", "-B", self.dir_name] + self.gen_args)

    def build(self):
        build_command = ["cmake", "--build", self.dir_name]

        if self.build_configs:
            build_command += ["--config", self.build_configs[0]]

        if self.build_args:
            build_command += ["--"] + self.build_args

        call(build_command)

    def package(self):
        raise NotImplementedError("package not implemented for this platform")

    def get_artifact_path(self):
        return os.path.join(dist_dir, self.get_artifact_name())

    def get_artifact_name(self):
        raise NotImplementedError("get_artifact_name not implemented for this platform")

    def copy_documentation(self, appdir, everything=True):
        shutil.copy(f"{self.dir_name}/ReadMe.txt", f"{appdir}")
        shutil.copy(f"LICENSE.md", f"{appdir}/License.txt")
        shutil.copy("THIRD-PARTY-LICENSES.md", appdir)
        if not everything:
            return
        os.makedirs(f"{appdir}/Documentation")
        for pattern in game_docs:
            for docfile in glob.glob(pattern):
                if os.path.isdir(docfile):
                    shutil.copytree(docfile, f"{appdir}/Documentation/{os.path.basename(docfile)}")
                else:
                    shutil.copy(docfile, f"{appdir}/Documentation")


class WindowsProject(Project):
    def __init__(self, dir_name="build-msvc"):
        super().__init__(dir_name)
        # On Windows, ship a PDB file along with the Release build.
        # Avoid RelWithDebInfo because bottom-of-the-barrel AVs may raise a false positive with such builds.
        self.build_configs = ["Release", "Debug"]
        self.build_args = ["-m"]  # multiprocessor compilation

    def get_artifact_name(self):
        return f"{game_name}-{game_ver}-windows-{args.A}.zip"

    def package(self):
        release_config = self.build_configs[0]
        windows_dlls = ["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"]

        # Prep Visual Studio redistributable DLLs with cmake (copied to {cache_dir}/install/bin)
        call(["cmake", "--install", self.dir_name, "--prefix", f"{cache_dir}/install"])

        appdir = f"{cache_dir}/{game_name}-{game_ver}"
        rmtree_if_exists(appdir)
        os.makedirs(f"{appdir}", exist_ok=True)

        # Copy executable, PDB, assets and libs
        shutil.copy(f"{self.dir_name}/{release_config}/{game_name}.exe", appdir)
        shutil.copy(f"{self.dir_name}/{release_config}/{game_name}.pdb", appdir)
        shutil.copy(f"{self.dir_name}/{release_config}/SDL3.dll", appdir)
        shutil.copytree("Data", f"{appdir}/Data")
        for dll in windows_dlls:
            shutil.copy(f"{cache_dir}/install/bin/{dll}", appdir)

        self.copy_documentation(appdir)

        rm_if_exists(self.get_artifact_path())
        zipdir(self.get_artifact_path(), appdir, f"{game_name}-{game_ver}")


class MacProject(Project):
    def __init__(self, dir_name="build-xcode"):
        super().__init__(dir_name)
        self.build_configs = ["RelWithDebInfo"]
        self.build_args += ["-j", str(NPROC), "-quiet"]
        self.gen_args += ["-DSDL_STATIC=ON", "-DSDL_SHARED=OFF"]
        self.signing_identity = args.macos_signing_identity
        self.gen_args += [f"-DCMR_MACOS_CODE_SIGN_IDENTITY={self.signing_identity}"]

    def get_configured_signing_identity(self):
        if self.signing_identity:
            return self.signing_identity

        cache_path = os.path.join(self.dir_name, "CMakeCache.txt")
        with contextlib.suppress(FileNotFoundError):
            with open(cache_path, encoding="utf-8") as cache_file:
                prefix = "CMR_MACOS_CODE_SIGN_IDENTITY:STRING="
                for line in cache_file:
                    if line.startswith(prefix):
                        return line[len(prefix):].rstrip("\r\n")

        return ""

    def verify_code_signature(self, app_path):
        if not self.get_configured_signing_identity():
            return

        call(["codesign", "--verify", "--deep", "--strict",
            "--all-architectures", "--verbose=2", app_path])
        call(["codesign", "--display", "--verbose=2", app_path])

    def build(self):
        super().build()
        release_config = self.build_configs[0]
        self.verify_code_signature(
            f"{self.dir_name}/{release_config}/{game_name}.app")

    def get_artifact_name(self):
        return f"{game_name}-{game_ver}-mac.dmg"

    def package(self):
        release_config = self.build_configs[0]
        source_app = f"{self.dir_name}/{release_config}/{game_name}.app"

        rm_if_exists(self.get_artifact_path())
        os.makedirs(cache_dir, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=f"{game_name}-dmg-", dir=cache_dir) as staging_dir:
            staged_app = os.path.join(staging_dir, f"{game_name_human}.app")
            shutil.copytree(source_app, staged_app, symlinks=True)
            self.copy_documentation(staging_dir)

            self.verify_code_signature(staged_app)

            call(["hdiutil", "create",
                "-ov",
                "-format", "UDZO",
                "-imagekey", "zlib-level=9",
                "-fs", "HFS+",
                "-srcfolder", staging_dir,
                "-volname", f"{game_name_human} {game_ver}",
                self.get_artifact_path()])


class LinuxProject(Project):
    # package_format: "appimage" (default), "appdir" (--no-appimage), "deb", or "rpm"
    def __init__(self, dir_name, config_name, use_system_sdl, package_format):
        super().__init__(dir_name)

        self.gen_args += ["-DCMAKE_BUILD_TYPE=" + config_name]
        self.build_args += ["-j", str(NPROC)]
        self.build_configs = [config_name]

        self.use_system_sdl = use_system_sdl
        if self.use_system_sdl:
            self.gen_args += ["-DBUILD_SDL_FROM_SOURCE=OFF"]

        self.package_format = package_format

        # deb/rpm are produced by CPack from the FHS install rules, which are gated behind
        # -DCMR_INSTALL=ON. They bundle the pinned, from-source SDL we just built; if the
        # user opted into system SDL, don't ship a private copy of it.
        if package_format in ("deb", "rpm"):
            self.gen_args += ["-DCMR_INSTALL=ON"]
            if self.use_system_sdl:
                self.gen_args += ["-DCMR_BUNDLE_SDL=OFF"]

    def get_artifact_name(self, extension=None):
        if extension is None:
            extension = {
                "appimage": ".AppImage",
                "appdir":   ".AppDir",
                "deb":      ".deb",
                "rpm":      ".rpm",
            }[self.package_format]

        return f"{game_name}-{game_ver}-linux-{MACHINE}{extension}"

    def package(self):
        if self.package_format in ("deb", "rpm"):
            self.package_cpack()
            return

        if self.package_format == "appimage":
            appdir = cache_dir + "/" + self.get_artifact_name(extension=".AppDir")
        else:
            appdir = self.get_artifact_path()

        assert appdir.endswith(".AppDir")
        rmtree_if_exists(appdir)

        # Prepare directory tree before copying files
        for d in ["", "usr/bin", "usr/lib", "usr/share/metainfo", "usr/share/applications"]:
            os.makedirs(f"{appdir}/{d}", exist_ok=True)

        # Copy executable and assets
        shutil.copy(f"{self.dir_name}/{game_name}", f"{appdir}/usr/bin")  # executable
        shutil.copytree("Data", f"{appdir}/Data")
        self.copy_documentation(appdir, everything=False)

        # Copy XDG stuff
        shutil.copy(f"packaging/{game_package}.desktop", appdir)
        shutil.copy(f"packaging/{game_package}.png", appdir)
        shutil.copy(f"packaging/{game_package}.appdata.xml", f"{appdir}/usr/share/metainfo")
        shutil.copy(f"packaging/{game_package}.desktop", f"{appdir}/usr/share/applications")  # must copy desktop file there as well, for validation

        # Copy AppImage kicker script
        shutil.copy(f"packaging/AppRun", appdir)
        os.chmod(f"{appdir}/AppRun", 0o755)

        # Copy SDL (if not using system SDL)
        if not self.use_system_sdl:
            sdl_libraries = glob.glob(f"{self.dir_name}/extern/SDL3/libSDL3*.so*")
            if not sdl_libraries:
                die(f"No source-built SDL3 library found in {self.dir_name}/extern/SDL3")
            for file in sdl_libraries:
                shutil.copy(file, f"{appdir}/usr/lib", follow_symlinks=False)

        # Invoke appimagetool
        if self.package_format == "appimage":
            appimagetool_path = get_package(f"https://github.com/AppImage/appimagetool/releases/download/{appimagetool_ver}/appimagetool-{MACHINE}.AppImage")
            os.chmod(appimagetool_path, 0o755)

            rm_if_exists(self.get_artifact_path())
            call([appimagetool_path, appdir, self.get_artifact_path()])

    def package_cpack(self):
        # CPack runs the FHS install() rules into a staging tree and builds the package.
        # The CPackConfig.cmake was written into the build dir by include(CPack), so run
        # cpack from there. We bundle the source-submodule SDL, so the resulting deb/rpm
        # is self-contained (see the CMR_INSTALL block in CMakeLists.txt).
        generator = "DEB" if self.package_format == "deb" else "RPM"
        ext = self.package_format  # "deb" or "rpm"

        # Clear any package left by a previous run so the glob below is unambiguous.
        for old in glob.glob(f"{self.dir_name}/*.{ext}"):
            rm_if_exists(old)

        call(["cpack", "-G", generator], cwd=self.dir_name)

        produced = glob.glob(f"{self.dir_name}/*.{ext}")
        if not produced:
            die(f"CPack did not produce a .{ext} in {self.dir_name}")

        rm_if_exists(self.get_artifact_path())
        shutil.move(produced[0], self.get_artifact_path())
        log(f"Created {self.get_artifact_path()}")

#----------------------------------------------------------------

if __name__ == "__main__":
    # Make sure we're running from the correct directory...
    os.chdir(root_dir)

    #----------------------------------------------------------------
    # Set up project metadata

    if SYSTEM == "Windows":
        project = WindowsProject(build_dir)

    elif SYSTEM == "Darwin":
        project = MacProject(build_dir)

    elif SYSTEM == "Linux":
        if args.deb:
            package_format = "deb"
        elif args.rpm:
            package_format = "rpm"
        elif args.no_appimage:
            package_format = "appdir"
        else:
            package_format = "appimage"
        project = LinuxProject(build_dir, "RelWithDebInfo", args.system_sdl, package_format)
    else:
        die(f"Unsupported system for configure step: {SYSTEM}")

    common_gen_args = []
    if args.G:
        common_gen_args += ["-G", args.G]
    if args.A:
        common_gen_args += ["-A", args.A]

    project.gen_args += common_gen_args

    #----------------------------------------------------------------
    # Gather build steps

    if args.print_artifact_name:
        print(project.get_artifact_name())
        sys.exit(0)

    fatlog(f"{game_name} {game_ver} build script")

    # --deb / --rpm (Linux) select the package format and imply the package step; on their
    # own they run the whole pipeline so `build.py --deb` just works.
    if getattr(args, "deb", False) or getattr(args, "rpm", False):
        args.package = True
        if not (args.dependencies or args.configure or args.build):
            args.dependencies = True
            args.configure = True
            args.build = True

    if not (args.dependencies or args.configure or args.build or args.package):
        log("No build steps specified, running all of them.")
        args.dependencies = True
        args.configure = True
        args.build = True
        args.package = True

    #----------------------------------------------------------------
    # Prepare dependencies

    if args.dependencies:
        fatlog("Setting up dependencies")

        # Check that our source dependencies are present before CMake emits a less helpful error.
        required_submodules = ["extern/Pomme/CMakeLists.txt"]
        if not getattr(project, "use_system_sdl", False):
            required_submodules.append("extern/SDL3/CMakeLists.txt")
        if not all(os.path.exists(path) for path in required_submodules):
            die("Submodules appear to be missing.\n"
                + "Did you clone the submodules recursively? Try this:    git submodule update --init --recursive")

        project.prepare_dependencies()

    #----------------------------------------------------------------
    # Configure projects

    if args.configure:
        project.configure()

    #----------------------------------------------------------------
    # Build the game

    if args.build:
        fatlog(f"Building the game: {project.dir_name}")
        project.build()

    #----------------------------------------------------------------
    # Package the game

    if args.package:
        fatlog(f"Packaging the game")
        os.makedirs(dist_dir, exist_ok=True)
        project.package()
