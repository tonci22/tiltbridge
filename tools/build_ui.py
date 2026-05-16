"""
PlatformIO pre-action: builds the Vue/Vite web UI in tiltbridge_web_ui/ and
copies the generated artifacts into data/ before the LittleFS filesystem image
is built.

Hooked only to the filesystem binary target — regular firmware builds don't
touch it, so `pio run -e <env>` without `--target buildfs` skips the UI build.

The entire contents of data/ are replaced with the Vite dist/ output, except
for data/wifiui/ which is owned by the esp_wifi_config library and must be
preserved across UI rebuilds.

Requires Node.js and npm on PATH. If they are missing, this script exits with
a clear error so new contributors know what to install.
"""

Import("env")  # noqa: F821  (provided by PlatformIO)

import os
import shutil
import subprocess
import sys

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
UI_DIR = os.path.join(PROJECT_DIR, "tiltbridge_web_ui")
DATA_DIR = os.path.join(PROJECT_DIR, "data")
DIST_DIR = os.path.join(UI_DIR, "dist")

# Subdirectories of data/ that are NOT produced by the UI build and must be
# preserved across rebuilds. Owned by the esp_wifi_config library.
PRESERVE_ENTRIES = ("wifiui",)

BANNER = "=" * 72


def _banner(msg):
    print("")
    print(BANNER)
    print(msg)
    print(BANNER)


def _abort(msg):
    print("")
    print("ERROR: " + msg)
    print("")
    print("The LittleFS filesystem image bundles a Vue/Vite web UI that must be")
    print("built before the image can be assembled. Install Node.js (LTS) from")
    print("https://nodejs.org/ — this provides the `npm` command — then retry.")
    print("")
    print("If you only want to build firmware (no filesystem), use:")
    print("    pio run -e <env>          # firmware only, UI build is skipped")
    print("")
    sys.exit(1)


def _clear_data_dir():
    """Remove everything in data/ except PRESERVE_ENTRIES."""
    if not os.path.isdir(DATA_DIR):
        return
    for name in os.listdir(DATA_DIR):
        if name in PRESERVE_ENTRIES:
            continue
        path = os.path.join(DATA_DIR, name)
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path)
        else:
            os.remove(path)


def _copy_tree(src, dst):
    """Recursively copy src into dst, preserving PRESERVE_ENTRIES at the top level."""
    for entry in os.listdir(src):
        if entry in PRESERVE_ENTRIES:
            # Extremely defensive: the Vue build shouldn't produce anything
            # named wifiui, but if it does, skip it to avoid clobbering.
            print("  ! skipping '{}' from dist (reserved for esp_wifi_config)".format(entry))
            continue
        s = os.path.join(src, entry)
        d = os.path.join(dst, entry)
        if os.path.isdir(s):
            shutil.copytree(s, d)
        else:
            shutil.copy2(s, d)
        print("  + " + entry)


def build_ui(source, target, env):
    _banner("Building web UI (tiltbridge_web_ui/ -> data/) for filesystem image")

    if not os.path.isdir(UI_DIR):
        _abort("tiltbridge_web_ui/ directory not found at {}".format(UI_DIR))

    npm = shutil.which("npm")
    if npm is None:
        _abort("`npm` not found on PATH.")

    node_modules = os.path.join(UI_DIR, "node_modules")
    if not os.path.isdir(node_modules):
        print("First-time setup: installing UI dependencies (npm ci)...")
        print("This can take a minute. Subsequent buildfs runs will be fast.")
        subprocess.check_call([npm, "ci"], cwd=UI_DIR)

    print("Running `npm run build` in tiltbridge_web_ui/ ...")
    subprocess.check_call([npm, "run", "build"], cwd=UI_DIR)

    if not os.path.isdir(DIST_DIR):
        _abort("vite build did not produce tiltbridge_web_ui/dist/")

    os.makedirs(DATA_DIR, exist_ok=True)
    print("Clearing data/ (preserving: {})".format(", ".join(PRESERVE_ENTRIES)))
    _clear_data_dir()

    print("Copying dist/ into data/:")
    _copy_tree(DIST_DIR, DATA_DIR)

    print("UI build complete.")
    print("")


# Hook the filesystem binary node rather than the "buildfs"/"uploadfs" aliases.
# Alias pre-actions fire AFTER the alias's dependencies are built, which would
# mean mklittlefs runs with stale data/ and our UI copy happens too late.
# Hooking the .bin target guarantees we run before mklittlefs.
env.AddPreAction("$BUILD_DIR/${ESP32_FS_IMAGE_NAME}.bin", build_ui)  # noqa: F821
