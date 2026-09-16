"""Use a native Ninja executable when PlatformIO's package is not runnable."""

import os
import platform as host_platform
import shutil
from types import MethodType

from SCons.Script import Import

Import("env")

if host_platform.machine() == "arm64":
    ninja_path = shutil.which("ninja")
    if not ninja_path:
        raise RuntimeError(
            "A native Ninja executable is required on Apple Silicon. "
            "Install it with `brew install ninja`."
        )

    pio_platform = env.PioPlatform()
    original_get_package_dir = pio_platform.get_package_dir

    def get_package_dir(self, name):
        if name == "tool-ninja":
            return os.path.dirname(ninja_path)
        return original_get_package_dir(name)

    pio_platform.get_package_dir = MethodType(get_package_dir, pio_platform)
