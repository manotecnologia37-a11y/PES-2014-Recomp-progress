import argparse
import json
import struct
import subprocess
from pathlib import Path


def sections(path):
    image = path.read_bytes()
    offset, = struct.unpack_from('<I', image, 32)
    size, count = struct.unpack_from('<2H', image, 46)
    for index in range(count):
        _, kind, flags, base, start, length, *_ = struct.unpack_from('<10I', image, offset + index * size)
        if kind == 1 and flags & 2:
            yield base, image[start:start + length], bool(flags & 4)


def external_references(paths, start, end, overlay):
    references = set()
    for path in paths:
        for base, data, is_code in sections(path):
            upper_halves = {}
            for index, (word,) in enumerate(struct.iter_unpack('<I', data[:len(data) & ~3])):
                source = base + index * 4
                operation = word >> 26
                if not is_code:
                    address = word
                elif operation in (2, 3):
                    address = ((source + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
                elif operation == 15:
                    register = (word >> 16) & 31
                    upper_halves[register] = (index, (word & 0xffff) << 16)
                    continue
                elif operation in (9, 13):
                    register = (word >> 21) & 31
                    previous, upper = upper_halves.get(register, (-20, 0))
                    if index - previous > 8:
                        continue
                    low = word & 0xffff
                    address = upper + (low - 0x10000 if operation == 9 and low & 0x8000 else low)
                else:
                    continue
                if not start <= address < end or address % 4:
                    continue
                opcode, = struct.unpack_from('<I', overlay, address - start)
                if opcode:
                    references.add(address)
    return references


def recompile(root, main, tool, ids, extra):
    metadata = json.loads((root / 'overlays.json').read_text())
    for item in metadata:
        number = item['number']
        if number not in ids:
            continue
        image = root / f'{number:02d}-{item["name"]}.elf'
        start, body, _ = next(section for section in sections(image) if section[2])
        # Overlays call into each other directly (e.g. select.ovl -> training_free.ovl),
        # so harvest references from every overlay image, not just the main ELF.
        inputs = [main] + sorted(root.glob('*.ovl.elf'))
        hints = external_references(inputs, start, start + len(body), body)
        hints.update(start + offset * 4 for offset, (opcode,) in enumerate(struct.iter_unpack('<I', body))
                     if opcode & 0xffff8000 == 0x27bd8000)
        hints.update(extra.get(str(number), ()))
        output = root / f'code-{number}'
        config = root / f'{number}.toml'
        config.write_text('[general]\n' + f'input = "{image.as_posix()}"\n' +
                          f'output = "{output.as_posix()}"\n' +
                          'single_file_output = false\npatch_syscalls = false\n' +
                          'patch_cop0 = true\npatch_cache = true\n' +
                          'entry_points = ' + json.dumps([hex(address) for address in sorted(hints)]) + '\n')
        with (root / f'{number}.log').open('w') as log:
            subprocess.run([tool, config], stdout=log, stderr=subprocess.STDOUT, check=True)
        print(f'Overlay {number}: {item["name"]}, {len(hints)} entry-point hints', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('main', type=Path)
    parser.add_argument('tool', type=Path)
    parser.add_argument('ids', type=int, nargs='+')
    args = parser.parse_args()
    root = args.root.resolve()
    hint_file = root / 'additional-entries.json'
    extra = json.loads(hint_file.read_text()) if hint_file.exists() else {}
    recompile(root, args.main.resolve(), args.tool.resolve(), args.ids, extra)
