"""Prepare a separate, version-checked HotA copy for the offline hire test."""

import hashlib
from pathlib import Path
import shutil
import subprocess

from src.make_hero_limit_launcher import redirect_version_import


PROJECT = Path(__file__).resolve().parent
SANDBOX = PROJECT / ".sandbox"
EXPECTED = {
    "h3hota HD.exe": "5aaab925f06cccf23bb09814767590a95b84a557eb33d244800520be4f1f18de",
    "HotA.dll": "1fa9ad209e756d55e9578b290c4fe6c9ba030d3b52af2085e530416a61ac10c2",
    "HD_HOTA.dll": "aa9e94d2129a8d7fc7000a0966bfc454140dcc4c30a11513ba159a9c08686378",
    "_HD3_.dll": "16b806fb96cb47057a58ac951efef20d4aee4ba9f80b9aa403304a1b1c4fdc66",
}


def main():
    source = SANDBOX / "game-cx-runtime"
    target = SANDBOX / "game-cx-limit"
    if target.exists():
        raise SystemExit(f"Lab directory already exists: {target}")
    for name, expected in EXPECTED.items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != expected:
            raise SystemExit(f"Unexpected HotA build: {name}")
    image = redirect_version_import(bytearray((source / "h3hota HD.exe").read_bytes()))
    dll = SANDBOX / "build" / "HeroLim.dll"
    if not dll.is_file():
        raise SystemExit("Build HeroLim.dll first")
    subprocess.run(
        ["rsync", "-a", "--exclude=/games/", "--exclude=/random_maps/", str(source) + "/",
         str(target) + "/"],
        check=True,
    )
    (target / "games").mkdir()
    (target / "h3hota HD.exe").write_bytes(image)
    shutil.copyfile(dll, target / "HeroLim.dll")
    shutil.copyfile(PROJECT / "HeroLimit.ini", target / "HeroLimit.ini")
    ini = target / "_HD3_Data" / "Settings" / "hota.ini"
    config = ini.read_bytes().replace(b"<Update.CheckAtStart> = 1", b"<Update.CheckAtStart> = 0")
    ini.write_bytes(config)
    print(target)


if __name__ == "__main__":
    main()
