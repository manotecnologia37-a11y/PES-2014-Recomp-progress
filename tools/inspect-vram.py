import argparse
import re
import struct
from pathlib import Path

from PIL import Image, ImageDraw


def table(source, name):
    table_body = source.split(name, 1)[1].split("}};", 1)[0]
    return [list(map(int, re.findall(r"\d+", row)))
            for row in re.findall(r"\{([\d,\s]+)\}", table_body)]


def decode(vram, tables, psm, fbp, width, height):
    bits = 32 if psm == "32" else 16
    page_width, page_height = 64, 32 if bits == 32 else 64
    block_width = 8 if bits == 32 else 16
    blocks, columns = tables[psm]
    image = bytearray(width * height * 3)
    for y in range(height):
        page_row, page_y = divmod(y, page_height)
        for x in range(width):
            page_column, page_x = divmod(x, page_width)
            page = page_row * (width // page_width) + page_column
            block = blocks[page_y // 8][page_x // block_width]
            column = columns[page_y % 8][page_x % block_width]
            address = ((fbp + page) * 8192 + block * 256 + column * (bits // 8)) & 0x3fffff
            pixel = (y * width + x) * 3
            if bits == 32:
                image[pixel:pixel + 3] = vram[address:address + 3]
            else:
                color, = struct.unpack_from("<H", vram, address)
                image[pixel:pixel + 3] = bytes(((color >> shift) & 31) * 255 // 31 for shift in (0, 5, 10))
    return Image.frombytes("RGB", (width, height), image)


def decode_indexed(vram, blocks, columns, bits, base, tbw, size=256):
    page_width = 128
    page_height = 64 if bits == 8 else 128
    block_width = 16 if bits == 8 else 32
    image = bytearray(size * size)
    for y in range(size):
        page_row, page_y = divmod(y, page_height)
        for x in range(size):
            page_column, page_x = divmod(x, page_width)
            page = page_row * (tbw // 2) + page_column
            block = blocks[page_y // 16][page_x // block_width]
            column = columns[page_y % 16][page_x % block_width]
            address = (base * 256 + page * 8192 + block * 256 + column * bits // 8) & 0x3fffff
            raw = vram[address]
            image[y * size + x] = raw if bits == 8 else ((raw >> (column % 2 * 4)) & 15) * 17
    return Image.frombytes("L", (size, size), image)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("frame", type=int)
    args = parser.parse_args()
    ranges = Path(__file__).parent
    source = (ranges.parents[1] / "PS2Recomp/ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp").read_text()
    tables = {psm: (table(source, f" BlockTableC{psm}\n"), table(source, f" ColumnTable{psm[:2]}\n"))
              for psm in ("32", "16", "16S")}
    vram = (ranges / f"boot-vram-{args.frame}.bin").read_bytes()
    montage = Image.new("RGB", (768, 544))
    draw = ImageDraw.Draw(montage)
    for row, fbp in enumerate((0, 128)):
        for column, psm in enumerate(tables):
            size = 512
            picture = decode(vram, tables, psm, fbp, size, size)
            x, y = column * 256, row * 272
            montage.paste(picture.resize((256, 256), Image.Resampling.NEAREST), (x, y + 16))
            draw.text((x + 2, y), f"FBP {fbp}  CT{psm}", fill="white")
    montage.save(ranges / f"vram-inspect-{args.frame}.png")
    montage = Image.new("RGB", (768, 544))
    draw = ImageDraw.Draw(montage)
    for row, (bits, base) in enumerate(((4, 8256), (8, 8224))):
        blocks = table(source, f" BlockTableP{bits}\n")
        columns = table(source, f" ColumnTable{bits}\n")
        for column, tbw in enumerate((2, 4, 8)):
            x, y = column * 256, row * 272
            montage.paste(decode_indexed(vram, blocks, columns, bits, base, tbw), (x, y + 16))
            draw.text((x + 2, y), f"TBP {base}  P{bits}  TBW {tbw}", fill="white")
    montage.save(ranges / f"vram-textures-{args.frame}.png")


if __name__ == "__main__":
    main()
