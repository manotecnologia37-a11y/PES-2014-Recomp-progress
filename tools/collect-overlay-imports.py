import argparse
import re
import struct
from pathlib import Path


def code_sections(image):
    section_offset, = struct.unpack_from('<I', image, 32)
    section_size, count = struct.unpack_from('<2H', image, 46)
    for index in range(count):
        _, kind, flags, base, offset, size, *_ = struct.unpack_from(
            '<10I', image, section_offset + index * section_size)
        if kind == 1 and flags & 4 and size:
            yield base, image[offset:offset + size]


def find_imports(ranges, ids):
    main = (ranges.parent / 'SLES_556.76').read_bytes()
    main_code = [(base, base + len(body)) for base, body in code_sections(main)]
    known = {int(match, 16) for match in re.findall(
        r'; // (0x[0-9a-f]+)', (ranges / 'output/register_functions.cpp').read_text())}
    missing = set()
    for number in ids:
        overlay = next((ranges / 'overlays').glob(f'{number:02d}-*.elf')).read_bytes()
        for base, body in code_sections(overlay):
            for index, (opcode,) in enumerate(struct.iter_unpack('<I', body)):
                if opcode >> 26 not in (2, 3):
                    continue
                address = ((base + index * 4 + 4) & 0xf0000000) | ((opcode & 0x3ffffff) << 2)
                if address not in known and any(start <= address < end for start, end in main_code):
                    missing.add(address)
    return sorted(missing)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('ranges', type=Path)
    parser.add_argument('ids', type=int, nargs='+')
    args = parser.parse_args()
    for address in find_imports(args.ranges, args.ids):
        print(f'{address:x}')
