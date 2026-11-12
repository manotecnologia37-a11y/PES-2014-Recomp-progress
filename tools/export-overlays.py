import argparse
import json
import struct
from pathlib import Path


def wrap_elf(code_address, code, data, bss_size):
    ident = b"\x7fELF\x01\x01\x01".ljust(16, b"\x00")
    code_offset = 0x100
    data_offset = code_offset + len(code)
    strings = b"\x00.text\x00.data\x00.bss\x00.shstrtab\x00"
    strings_offset = data_offset + len(data)
    section_offset = (strings_offset + len(strings) + 3) & ~3
    ehdr = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 8, 1, code_address, 52, section_offset,
                       0x20924001, 52, 32, 2, 40, 5, 4)
    data_address = code_address + len(code)
    segments = struct.pack("<16I", 1, code_offset, code_address, code_address, len(code), len(code), 5, 64,
                           1, data_offset, data_address, data_address, len(data),
                           len(data) + bss_size, 6, 64)
    sections = bytes(40)
    for row in (
        (1, 1, 6, code_address, code_offset, len(code), 0, 0, 16, 0),
        (7, 1, 3, data_address, data_offset, len(data), 0, 0, 16, 0),
        (13, 8, 3, data_address + len(data), strings_offset, bss_size, 0, 0, 16, 0),
        (18, 3, 0, 0, strings_offset, len(strings), 0, 0, 1, 0),
    ):
        sections += struct.pack("<10I", *row)
    return ((ehdr + segments).ljust(code_offset, b"\x00") + code + data + strings).ljust(
        section_offset, b"\x00") + sections


def export(archive, target):
    archive_bytes = archive.read_bytes()
    if archive_bytes[:4] != b"AFS\x00":
        raise ValueError(f"{archive} has no AFS header")
    count, = struct.unpack_from("<I", archive_bytes, 4)
    target.mkdir(parents=True, exist_ok=True)
    overlays = []
    for index in range(count):
        offset, size = struct.unpack_from("<II", archive_bytes, 8 + index * 8)
        entry = archive_bytes[offset:offset + size]
        magic, number, base, code_size, data_size, bss_size = struct.unpack_from("<4s5I", entry)
        if magic != b"MWo3" or len(entry) < 0x40 + code_size + data_size:
            raise ValueError(f"Invalid overlay header at archive entry {index}")
        name = entry[32:128].split(b"\x00", 1)[0].decode("ascii")
        code_end = 0x40 + code_size
        code = entry[0x80:code_end]
        data = entry[code_end:code_end + data_size]
        overlay = dict(index=index, number=number, name=name, base=base, code_size=code_size,
                       data_size=data_size, bss_size=bss_size)
        overlays.append(overlay)
        if code_size > 0xc0:
            (target / f"{number:02d}-{name}.elf").write_bytes(wrap_elf(base + 0x80, code, data, bss_size))
        if number == 37:
            # The game's constructor list points to executable code at the end of
            # the overlay's initialized data. Keep the intervening data out of .text.
            constructor_start = 0xd2c500
            constructor_list = 0xd2c890
            pointer, = struct.unpack_from("<I", entry, constructor_list - base)
            if pointer != constructor_start:
                raise ValueError("Unexpected game.ovl constructor address")
            constructors = entry[constructor_start - base:constructor_list - base]
            name = "game-init.ovl"
            (target / f"137-{name}.elf").write_bytes(wrap_elf(constructor_start, constructors, b"", 0))
            overlays.append(dict(index=index, number=137, name=name, base=constructor_start - 0x80,
                                 code_size=len(constructors) + 0x40, data_size=0, bss_size=0,
                                 fingerprint_base=base + 0x80, fingerprint_file="37-game.ovl.elf"))
    (target / "overlays.json").write_text(json.dumps(overlays, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    export(args.archive, args.target)
