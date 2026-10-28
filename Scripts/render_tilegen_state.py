"""
Render the Model 3 tile-generator layer stored in a Supermodel save state.

The 2D HUD/menu layer is produced by four tile layers in a 496x384 surface.  A save state
stores the tile VRAM and the 64 tilegen registers, so the layer can be rebuilt without
starting the emulator.  This is useful when a state loads with a suspicious HUD: if the PNG
looks wrong too, the state itself carries the wrong 2D data; if it looks right, the fault is
in the load/render path.

Usage:
    python Scripts/render_tilegen_state.py <state.st> [out.png]

<state.st> may be a raw block file (the .vktmp.st inside a .zst container) or the container
itself.  Only the tile-generator block is read, same layout as CTileGen::SaveState():
0x120000 bytes of VRAM followed by 64 little-endian registers.
"""

import struct
import sys
import zlib

from PIL import Image

VRAM_SIZE = 0x120000
REG_COUNT = 64
SCREEN_W = 496
SCREEN_H = 384


def find_tilegen_block(data):
    pos = 0
    while pos + 12 <= len(data):
        block_len, name_len, comment_len = struct.unpack_from("<III", data, pos)
        if block_len < 12 or pos + block_len > len(data):
            break
        name = data[pos + 12:pos + 12 + name_len - 1].decode("ascii", "replace")
        data_off = pos + 12 + name_len + comment_len
        data_size = block_len - 12 - name_len - comment_len
        if name == "Tile Generator":
            return data[data_off:data_off + data_size]
        pos += block_len
    raise SystemExit("no 'Tile Generator' block found (not a raw block file?)")


def decompress_container(path):
    data = open(path, "rb").read()
    # HostCore's .zst container wraps the raw block file.  A raw state also starts with a
    # plausible block length, so probe both instead of trusting the extension.
    try:
        find_tilegen_block(data)
        return data
    except SystemExit:
        pass
    for wbits in (15, 31, 47):
        try:
            return zlib.decompress(data, wbits)
        except zlib.error:
            continue
    raise SystemExit(f"{path}: not a raw block file and zlib/zstd cannot be decoded here")


def colour32(palette, layer, data):
    alpha = 255 if ((data >> 15) & 1) == 0 else 0
    b = (((data >> 10) & 0x1F) * 255) // 31 & alpha
    g = (((data >> 5) & 0x1F) * 255) // 31 & alpha
    r = ((data & 0x1F) * 255) // 31 & alpha
    return r, g, b, alpha


class TileGen:
    def __init__(self, block):
        self.vram = block[:VRAM_SIZE]
        self.words = struct.unpack_from("<%dI" % (VRAM_SIZE // 4), self.vram, 0)
        self.regs = struct.unpack_from("<64I", block, VRAM_SIZE)
        self.pal = []
        for layer in range(2):
            offset_reg = self.regs[0x40 // 4 + layer]
            rb = ((offset_reg & 0xFF) ^ 0x80) - 0x80
            gb = (((offset_reg >> 8) & 0xFF) ^ 0x80) - 0x80
            bb = (((offset_reg >> 16) & 0xFF) ^ 0x80) - 0x80
            table = []
            for i in range(32768):
                r, g, b, a = colour32(None, 0, self.words[0x40000 + i])
                table.append((
                    max(0, min(255, r + rb * 2)),
                    max(0, min(255, g + gb * 2)),
                    max(0, min(255, b + bb * 2)),
                    a,
                ))
            self.pal.append(table)

    def enabled(self, layer):
        return (self.regs[0x60 // 4 + layer] & 0x80000000) != 0

    def above_3d(self, layer):
        return (self.regs[0x20 // 4] >> (8 + layer)) & 1

    def four_bit(self, layer):
        return (self.regs[0x20 // 4] >> (12 + layer)) & 1

    def line_scroll_mode(self, layer):
        return (self.regs[0x60 // 4 + layer] & 0x8000) != 0

    def x_scroll(self, layer):
        return self.regs[0x60 // 4 + layer] & 0x3FF

    def y_scroll(self, layer):
        return (self.regs[0x60 // 4 + layer] >> 16) & 0x1FF

    def line_scroll(self, layer, y):
        index = (0xF6000 // 4) + layer * 0x100 + (y // 2)
        shift = (1 - (y % 2)) * 16
        return (self.words[index] >> shift) & 0xFFFF

    def line_mask(self, layer, y):
        shift = 16 if layer < 2 else 0
        index = (0xF7000 // 4) + y
        return (self.words[index] >> shift) & 0xFFFF

    def tile_data(self, layer, tile_pair):
        return self.words[(0xF8000 // 4) + layer * 0x800 + tile_pair]

    def draw_line(self, y, surfaces):
        for pair in (1, 0):
            primary = pair * 2
            alt = primary + 1
            has_layer = (self.enabled(primary), self.enabled(alt))
            if not any(has_layer):
                continue

            line_mask = self.line_mask(primary, y)
            scroll_x = [
                self.line_scroll(primary, y) if self.line_scroll_mode(primary) else self.x_scroll(primary),
                self.line_scroll(alt, y) if self.line_scroll_mode(alt) else self.x_scroll(alt),
            ]
            scroll_y = [self.y_scroll(primary), self.y_scroll(alt)]

            x = 0
            while x < SCREEN_W:
                layer = ((line_mask & (1 << (15 - (x // 32)))) != 0) and 0 or 1
                # GetPixelMask returns 1 for primary.  The C++ flips it with (mask + 1) & 1,
                # so mask set -> layer 0 -> primary; mask clear -> layer 1 -> alt.
                mask_switch = ((line_mask >> (15 - (x // 32))) & 1) != ((line_mask >> (15 - ((x + 15) // 32))) & 1)
                if has_layer[layer]:
                    index = primary + layer
                    tile_pair = self.tile_pair_number(x - 16 if mask_switch else x, y, scroll_x[layer], scroll_y[layer])
                    h_start = (x + scroll_x[layer]) & 15
                    h_end = (16 - (x & 15)) if mask_switch else 16
                    v_fine = (y + scroll_y[layer]) & 7
                    tile_data = self.tile_data(index, tile_pair)
                    target = surfaces[self.above_3d(index)]
                    self.draw_tile_pair(target, y, x, tile_data, h_start, h_end, v_fine, index, layer)
                    x += h_end - h_start
                else:
                    x += 32 - ((x + 0) & 15)

    def tile_pair_number(self, x, y, x_scroll, y_scroll):
        return (((y + y_scroll) // 8) & 0x3F) * 32 + (((x + x_scroll) // 16) & 0x1F)

    def draw_tile_pair(self, target, y, x, tile_data, h_start, h_end, v_fine, index, layer):
        if self.four_bit(index):
            offsets = [
                ((((tile_data >> 16) & 0x3FFF) << 1) | ((tile_data >> 31) & 1)) * 8,
                (((tile_data & 0x3FFF) << 1) | ((tile_data >> 15) & 1)) * 8,
            ]
            palette_index = [(tile_data >> 16) & 0x7FF0, tile_data & 0x7FF0]
            patterns = [self.words[offsets[0] + v_fine], self.words[offsets[1] + v_fine]]
            table = self.pal[index // 2]
            for i in range(h_start, h_end):
                pattern = patterns[i // 8]
                p = (pattern >> ((7 - (i % 8)) * 4)) & 0xF
                colour = table[palette_index[i // 8] | p]
                if colour[3]:
                    target[y][x + i - h_start] = colour
        else:
            offsets = [
                ((tile_data >> 16) & 0x3FFF) * 16,
                (tile_data & 0x3FFF) * 16,
            ]
            palette_index = [((tile_data >> 16) & 0x7F00), (tile_data & 0x7F00)]
            patterns = [
                self.words[offsets[0] + v_fine * 2], self.words[offsets[0] + v_fine * 2 + 1],
                self.words[offsets[1] + v_fine * 2], self.words[offsets[1] + v_fine * 2 + 1],
            ]
            table = self.pal[index // 2]
            for i in range(h_start, h_end):
                pattern = patterns[i // 4]
                p = (pattern >> ((3 - (i % 4)) * 8)) & 0xFF
                colour = table[palette_index[i // 8] | p]
                if colour[3]:
                    target[y][x + i - h_start] = colour


def render(block, out_path):
    tg = TileGen(block)
    surfaces = [
        [[(0, 0, 0, 0)] * SCREEN_W for _ in range(SCREEN_H)],
        [[(0, 0, 0, 0)] * SCREEN_W for _ in range(SCREEN_H)],
    ]
    for y in range(SCREEN_H):
        tg.draw_line(y, surfaces)

    for index, surface in enumerate(surfaces):
        name = out_path if len(surfaces) == 1 else out_path.rsplit(".", 1)[0] + f"-layer{index}.png"
        image = Image.new("RGBA", (SCREEN_W, SCREEN_H))
        image.putdata([pixel for row in surface for pixel in row])
        image.save(name)
        print(f"{name}: {SCREEN_W}x{SCREEN_H}")

    regs = " ".join(f"{i*4:02X}={tg.regs[i]:08X}" for i in range(REG_COUNT) if tg.regs[i])
    print("regs:", regs)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "tilegen.png"
    render(find_tilegen_block(decompress_container(path)), out)
