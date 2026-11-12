import argparse
import json
import re
import struct
from pathlib import Path


ASSIGNMENT = re.compile(r"= (\w+); // (0x[0-9a-f]+)")
FUNCTION_NAME = re.compile(r'\b\w+_0x[0-9a-f]+\b')


def write_changed(path, content):
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def stage(source, runner, ids):
    metadata = json.loads((source / "overlays.json").read_text())
    functions = ['#include "runtime/guest_overlay.h"', '#include <algorithm>', '#include <array>',
                 '#include <cstdlib>', '#include <cstring>', '#include <iostream>', '']
    variants = []
    for item in metadata:
        number = item["number"]
        if number not in ids:
            continue
        directory = source / f"code-{number}"
        assignments = ASSIGNMENT.findall((directory / "register_functions.cpp").read_text())
        if not assignments:
            raise ValueError(f"No function entries in {directory}")
        overlay_functions = sorted({function for function, _ in assignments})
        prefix = f"pes_overlay_{number}_"
        names = {name: prefix + name for name in overlay_functions}
        declarations = [f"void {prefix}{function}(uint8_t *, R5900Context *, PS2Runtime *);"
                        for function in overlay_functions]
        header = f"{prefix}functions.h"
        write_changed(runner / header, '#pragma once\n#include "ps2_runtime.h"\n\n' +
                      '\n'.join(declarations) + '\n')
        staged = set()
        for index in range(0, len(overlay_functions), 24):
            batch = [f'#include "{header}"']
            for function in overlay_functions[index:index + 24]:
                contents = (directory / f"{function}.cpp").read_text()
                if number == 19 and function == "sub_006C0030_0x6c0030":
                    # The formation overlay branches to the delay slot independently.
                    # Keep that entry and execute the same instruction on the call path.
                    call = "    SET_GPR_U32(ctx, 31, 0x6C007Cu);\n"
                    assert contents.count(call) == 1
                    contents = contents.replace(call, call +
                        "    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 29), 192));\n")
                batch.append(FUNCTION_NAME.sub(lambda match: names.get(match[0], match[0]), contents))
            path = runner / f"{prefix}{index // 24:03d}.cpp"
            write_changed(path, "\n".join(batch))
            staged.add(path)
        for stale in runner.glob(f'{prefix}[0-9][0-9][0-9].cpp'):
            if stale not in staged:
                stale.unlink()
        functions.append(f'#include "{header}"')
        image = (source / item.get("fingerprint_file", f'{number:02d}-{item["name"]}.elf')).read_bytes()
        fingerprint = struct.unpack_from("<4I", image, 0x100)
        variants.append((item, prefix, assignments, fingerprint))

    functions.extend(['', 'namespace', '{', '    struct Entry', '    {', '        uint32_t pc;',
                      '        PS2Runtime::RecompiledFunction function;', '    };', ''])
    for item, prefix, assignments, _ in variants:
        functions.append(f'    constexpr Entry {prefix}entries[] = {{')
        functions.extend(f'        {{{address}u, {prefix}{function}}},'
                         for function, address in sorted(assignments, key=lambda row: int(row[1], 16)))
        functions.extend(['    };', ''])
    functions.extend([
        '    template <size_t count>',
        '    PS2Runtime::RecompiledFunction findEntry(const Entry (&entries)[count], uint32_t pc)',
        '    {',
        '        const auto entry = std::lower_bound(std::begin(entries), std::end(entries), pc,',
        '                                            [](const Entry &item, uint32_t value) { return item.pc < value; });',
        '        return entry != std::end(entries) && entry->pc == pc ? entry->function : nullptr;',
        '    }',
        '}',
        '',
        'PS2Runtime::RecompiledFunction resolvePesOverlay(const uint8_t *rdram, uint32_t pc)',
        '{',
        '    if (!rdram)',
        '        return nullptr;',
        '',
    ])
    for item, prefix, _, fingerprint in variants:
        start = item["base"] + 0x80
        end = item["base"] + 0x40 + item["code_size"]
        fingerprint_base = item.get("fingerprint_base", start)
        words = ', '.join(f'0x{value:08x}u' for value in fingerprint)
        functions.extend([
            f'    if (pc >= 0x{start:x}u && pc < 0x{end:x}u)',
            '    {',
            f'        constexpr std::array<uint32_t, 4> fingerprint = {{{words}}};',
            f'        if (std::memcmp(rdram + 0x{fingerprint_base:x}u, fingerprint.data(), sizeof(fingerprint)) == 0)',
            '        {',
            '            static const bool traced = [] {',
            f'                if (std::getenv("PES2014_BOOT_DIAGNOSTICS")) std::cerr << "[PES overlay] {item["name"]}\\n";',
            '                return true;',
            '            }();',
            '            (void)traced;',
            f'            return findEntry({prefix}entries, pc);',
            '        }',
            '    }',
        ])
    functions.extend(['    return nullptr;', '}', ''])
    write_changed(runner / 'pes_overlay_dispatch.cpp', '\n'.join(functions))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('runner', type=Path)
    parser.add_argument('ids', type=int, nargs='+')
    args = parser.parse_args()
    stage(args.source, args.runner, args.ids)
