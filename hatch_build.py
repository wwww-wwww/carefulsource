import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from hatchling.builders.hooks.plugin.interface import BuildHookInterface
from packaging import tags


class CustomHook(BuildHookInterface[Any]):
    source_dir = Path("build")
    target_dir = Path("vapoursynth/plugins")

    def initialize(self, version: str, build_data: dict[str, Any]) -> None:

        build_data["pure_python"] = False
        build_data["tag"] = f"py3-none-{next(tags.platform_tags())}"

        subprocess.run(
            [
                sys.executable,
                "-m",
                "mesonbuild.mesonmain",
                "setup",
                "build",
                "--vsenv",
                "-Dpy_ext=false",
            ],
            check=True,
        )
        subprocess.run(
            [sys.executable, "-m", "mesonbuild.mesonmain", "compile", "-C", "build"],
            check=True,
        )

        shutil.rmtree(self.target_dir.parent, ignore_errors=True)
        for file_path in self.source_dir.rglob("*"):
            if file_path.is_file() and file_path.suffix in [".dll", ".so", ".dylib"]:
                build_data.setdefault("force_include", {})[str(file_path)] = str(
                    self.target_dir / file_path.name
                )

    def finalize(
        self, version: str, build_data: dict[str, Any], artifact_path: str
    ) -> None:
        shutil.rmtree(self.target_dir.parent, ignore_errors=True)
