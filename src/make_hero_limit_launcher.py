"""Create the optional Hero Limit executable from an installed HotA 1.8.1."""

import argparse
import hashlib
from pathlib import Path
import struct


CODE_SHA256 = "eb96d2ece7f047587ca430cbb421db678fad71c49929818ede6a3b5a5d35ce62"
HOTA_SHA256 = "1fa9ad209e756d55e9578b290c4fe6c9ba030d3b52af2085e530416a61ac10c2"


def redirect_version_import(image):
    pe, = struct.unpack_from("<I", image, 0x3C)
    if image[:2] != b"MZ" or image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE header")
    machine, count = struct.unpack_from("<HH", image, pe + 4)
    optional_size, = struct.unpack_from("<H", image, pe + 20)
    optional = pe + 24
    magic, = struct.unpack_from("<H", image, optional)
    base, = struct.unpack_from("<I", image, optional + 28)
    directories, = struct.unpack_from("<I", image, optional + 92)
    if machine != 0x14C or magic != 0x10B or base != 0x400000 or optional_size < 224 or directories < 2:
        raise ValueError("Expected a Win32 HotA executable")

    sections = []
    code_verified = False
    for index in range(count):
        section = optional + optional_size + index * 40
        name, _, start, size, offset = struct.unpack_from("<8s4I", image, section)
        if offset + size > len(image):
            raise ValueError("Invalid PE section")
        sections.append((start, size, offset))
        if name.rstrip(b"\0") == b".text":
            code_verified = start == 0x1000 and (
                hashlib.sha256(image[offset:offset + size]).hexdigest() == CODE_SHA256
            )
    if not code_verified:
        raise ValueError("This executable's game code does not match HotA 1.8.1")

    def file_offset(rva, length):
        for start, size, offset in sections:
            if start <= rva and rva - start + length <= size:
                return offset + rva - start
        raise ValueError(f"Invalid file section for RVA {rva:#x}")

    imports_rva, imports_size = struct.unpack_from("<II", image, optional + 104)
    old = b"VERSION.dll\0"
    new = b"HeroLim.dll\0"
    for offset in range(0, imports_size - 19, 20):
        descriptor = file_offset(imports_rva + offset, 20)
        lookup, _, _, name, _ = struct.unpack_from("<IIIII", image, descriptor)
        if not name:
            break
        location = file_offset(name, len(old))
        if image[location:location + len(old)] == old and lookup:
            image[location:location + len(old)] = new
            struct.pack_into("<I", image, descriptor + 4, 0)
            return image
    raise ValueError("The original VERSION.dll import was not found")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="location of h3hota HD.exe")
    args = parser.parse_args()
    source = args.game.resolve()
    folder = source.parent
    if source.name.casefold() != "h3hota hd.exe":
        parser.exit(1, "Select the installed h3hota HD.exe file.\n")
    try:
        if hashlib.sha256((folder / "HotA.dll").read_bytes()).hexdigest() != HOTA_SHA256:
            raise ValueError("Unrecognized HotA.dll build")
        if not (folder / "HeroLim.dll").is_file():
            raise ValueError("Copy HeroLim.dll into the HotA folder first")
        image = redirect_version_import(bytearray(source.read_bytes()))
        target = folder / "h3hota HeroLimit.exe"
        created = False
        try:
            with target.open("xb") as output:
                created = True
                output.write(image)
        except FileExistsError:
            if target.read_bytes() != image:
                raise
        except OSError:
            if created:
                target.unlink(missing_ok=True)
            raise
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"{error}\n")
    print(target)


if __name__ == "__main__":
    main()
