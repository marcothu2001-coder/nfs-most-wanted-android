import struct
import os
import sys

ISO_PATH = r"D:\Jogo\need\Need for Speed - Most Wanted - Black Edition (BR).iso"
ELF_PATH = r"D:\Jogo\SLUS_213.51"
ASSETS_DIR = r"D:\Jogo\android\app\src\main\assets"

os.makedirs(os.path.join(ASSETS_DIR, "rockport"), exist_ok=True)
os.makedirs(os.path.join(ASSETS_DIR, "textures"), exist_ok=True)
os.makedirs(os.path.join(ASSETS_DIR, "copmidsize"), exist_ok=True)
os.makedirs(os.path.join(ASSETS_DIR, "bmwm3gtre46"), exist_ok=True)

# 1. Setup R5900 MIPS CPU emulator with SLUS_213.51 for HUFF / JDLZ decompression
with open(ELF_PATH, "rb") as f:
    elf = f.read()

mips_mem = bytearray(16 * 1024 * 1024)
mips_mem[0x100000 : 0x100000 + 0x4ABBDC] = elf[0x1000 : 0x1000 + 0x4ABBDC]

def s32(v):
    return (v & 0xFFFFFFFF) - 0x100000000 if (v & 0x80000000) else (v & 0xFFFFFFFF)

def u32(v):
    return v & 0xFFFFFFFF

def run_mips(pc, a0, a1):
    r = [0] * 32
    r[4] = a0
    r[5] = a1
    r[29] = 0xF00000
    r[31] = 0xDEAD0000
    
    def exec_alu_mem(w, pc):
        op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; sh = (w >> 6) & 31; fn = w & 63
        imm = w & 0xFFFF; simm = imm - 0x10000 if (imm & 0x8000) else imm
        if w == 0: pass
        elif op == 0:
            if fn == 0x00: r[rd] = u32(r[rt] << sh)
            elif fn == 0x02: r[rd] = u32(r[rt]) >> sh
            elif fn == 0x03: r[rd] = u32(s32(r[rt]) >> sh)
            elif fn == 0x04: r[rd] = u32(r[rt] << (r[rs] & 31))
            elif fn == 0x06: r[rd] = u32(r[rt]) >> (r[rs] & 31)
            elif fn == 0x07: r[rd] = u32(s32(r[rt]) >> (r[rs] & 31))
            elif fn == 0x0A:
                if r[rt] == 0: r[rd] = r[rs]
            elif fn == 0x0B:
                if r[rt] != 0: r[rd] = r[rs]
            elif fn in (0x20, 0x21, 0x2D): r[rd] = u32(r[rs] + r[rt])
            elif fn in (0x22, 0x23, 0x2F): r[rd] = u32(r[rs] - r[rt])
            elif fn == 0x24: r[rd] = r[rs] & r[rt]
            elif fn == 0x25: r[rd] = r[rs] | r[rt]
            elif fn == 0x26: r[rd] = r[rs] ^ r[rt]
            elif fn == 0x27: r[rd] = u32(~(r[rs] | r[rt]))
            elif fn == 0x2A: r[rd] = 1 if s32(r[rs]) < s32(r[rt]) else 0
            elif fn == 0x2B: r[rd] = 1 if u32(r[rs]) < u32(r[rt]) else 0
        elif op in (8, 9, 0x18, 0x19): r[rt] = u32(r[rs] + simm)
        elif op == 10: r[rt] = 1 if s32(r[rs]) < simm else 0
        elif op == 11: r[rt] = 1 if u32(r[rs]) < u32(simm) else 0
        elif op == 12: r[rt] = r[rs] & imm
        elif op == 13: r[rt] = r[rs] | imm
        elif op == 14: r[rt] = r[rs] ^ imm
        elif op == 15: r[rt] = u32(imm << 16)
        elif op == 0x20:
            b = mips_mem[u32(r[rs] + simm)]
            r[rt] = u32(b - 256 if b >= 128 else b)
        elif op == 0x21: r[rt] = u32(struct.unpack_from('<h', mips_mem, u32(r[rs] + simm))[0])
        elif op == 0x23: r[rt] = struct.unpack_from('<I', mips_mem, u32(r[rs] + simm))[0]
        elif op == 0x24: r[rt] = mips_mem[u32(r[rs] + simm)]
        elif op == 0x25: r[rt] = struct.unpack_from('<H', mips_mem, u32(r[rs] + simm))[0]
        elif op == 0x28: mips_mem[u32(r[rs] + simm)] = r[rt] & 0xFF
        elif op == 0x29: struct.pack_into('<H', mips_mem, u32(r[rs] + simm), r[rt] & 0xFFFF)
        elif op == 0x2B: struct.pack_into('<I', mips_mem, u32(r[rs] + simm), u32(r[rt]))
        elif op == 0x37: r[rt] = struct.unpack_from('<I', mips_mem, u32(r[rs] + simm))[0]
        elif op == 0x3F: struct.pack_into('<I', mips_mem, u32(r[rs] + simm), u32(r[rt]))
        r[0] = 0

    while pc != 0xDEAD0000:
        w = struct.unpack_from('<I', mips_mem, pc)[0]
        op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; fn = w & 63
        imm = w & 0xFFFF; simm = imm - 0x10000 if (imm & 0x8000) else imm
        if op == 0 and fn in (0x08, 0x09):
            target = r[rs]
            if fn == 0x09: r[rd] = pc + 8
            exec_alu_mem(struct.unpack_from('<I', mips_mem, pc + 4)[0], pc + 4); pc = target
        elif op in (2, 3):
            target = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if op == 3: r[31] = pc + 8
            exec_alu_mem(struct.unpack_from('<I', mips_mem, pc + 4)[0], pc + 4); pc = target
        elif op in (1, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17):
            likely = op in (0x14, 0x15, 0x16, 0x17) or (op == 1 and rt in (2, 3))
            cond = False
            if op in (4, 0x14): cond = (r[rs] == r[rt])
            elif op in (5, 0x15): cond = (r[rs] != r[rt])
            elif op in (6, 0x16): cond = (s32(r[rs]) <= 0)
            elif op in (7, 0x17): cond = (s32(r[rs]) > 0)
            elif op == 1:
                if rt in (0, 2): cond = (s32(r[rs]) < 0)
                elif rt in (1, 3): cond = (s32(r[rs]) >= 0)
            if cond:
                target = pc + 4 + (simm << 2)
                exec_alu_mem(struct.unpack_from('<I', mips_mem, pc + 4)[0], pc + 4); pc = target
            else:
                if not likely: exec_alu_mem(struct.unpack_from('<I', mips_mem, pc + 4)[0], pc + 4)
                pc += 8
        else: exec_alu_mem(w, pc); pc += 4
    return r[2]

def decompress_mips(comp_bytes, expected_uncomp_size):
    mips_mem[0x800000 : 0x800000 + len(comp_bytes)] = comp_bytes
    run_mips(0x0031F878, 0x800000, 0x900000)
    return bytes(mips_mem[0x900000 : 0x900000 + expected_uncomp_size])

# 2. Hardware Unswizzlers
def unswizzle8(buf, width, height):
    out = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            block_location = (y & (~0xf)) * width + (x & (~0xf)) * 2
            swap_selector = (((y + 2) >> 2) & 0x1) * 4
            posY = (((y & (~3)) >> 1) + (y & 1)) & 0x7
            column_location = posY * width * 2 + ((x + swap_selector) & 0x7) * 4
            byte_num = ((y >> 1) & 1) + ((x >> 2) & 2)
            swizzleid = block_location + column_location + byte_num
            if swizzleid < len(buf):
                out[y * width + x] = buf[swizzleid]
    return out

def unswizzle4(buffer, width, height):
    pixels = bytearray(width * height)
    for i in range(min(len(buffer), width * height // 2)):
        index = buffer[i]
        pixels[i * 2] = index & 0xf
        pixels[i * 2 + 1] = (index >> 4) & 0xf
    return unswizzle8(pixels, width, height)

def convert_to_rgba32(pix_indices, palette_bytes, width, height, bpp):
    # Palette color conversion:
    num_colors = 16 if bpp == 4 else 256
    colors = []
    for i in range(num_colors):
        # 8bpp CSM1 CLUT swizzling
        ci = i
        if bpp == 8:
            if (i & 0x18) == 0x08: ci = i + 8
            elif (i & 0x18) == 0x10: ci = i - 8
        off = ci * 4
        if off + 4 <= len(palette_bytes):
            r, g, b, a = struct.unpack_from("<4B", palette_bytes, off)
            a_clamped = min(255, a * 2) if a <= 128 else 255
            colors.append((r, g, b, a_clamped))
        else:
            colors.append((0, 0, 0, 0))
            
    rgba = bytearray(width * height * 4)
    for idx, p in enumerate(pix_indices[:width * height]):
        c = colors[p % len(colors)]
        rgba[idx * 4 : idx * 4 + 4] = bytes(c)
    return rgba

print("=== 1. Opening ISO and Loading ZDIR.BIN ===")
with open(ISO_PATH, "rb") as iso:
    iso.seek(140141 * 2048)
    zdir = iso.read(34728)
    zz_lbas = [140158, 549758, 941549, 1304059]

    def read_rec(idx):
        h1, arch, sec, csz, usz, h2 = struct.unpack_from("<6I", zdir, idx * 24)
        iso.seek((zz_lbas[arch] + sec) * 2048)
        return iso.read(usz)

    # 3. Extract Rockport 3D World Scenery from Record #28 (eLabScenery_L2RA_TC_WestPark_Sunset.bin)
    print("=== 2. Extracting Rockport 3D World Scenery (Record #28) ===")
    h1, arch, sec, csz, usz, h2 = struct.unpack_from("<6I", zdir, 28 * 24)
    r28_base = (zz_lbas[arch] + sec) * 2048
    
    # Global Props (0x00000008 .. 0x00067D70, ~425 KB)
    iso.seek(r28_base)
    r28_props = iso.read(0x68000)
    
    # Rockport 3D Highway Scenery & Buildings (0x01E25808 .. + 4MB)
    iso.seek(r28_base + 0x01E25808)
    r28_buildings = iso.read(4 * 1024 * 1024)
    
    # Save combined SCENERY_L2RA.BIN
    scenery_path = os.path.join(ASSETS_DIR, "rockport", "SCENERY_L2RA.BIN")
    with open(scenery_path, "wb") as f:
        f.write(r28_props)
        f.write(r28_buildings)
    print(f"Saved {scenery_path} ({os.path.getsize(scenery_path):,} bytes)")

    # 4. Extract Original .TPK Files to assets/
    print("=== 3. Extracting Authentic .TPK Files to assets/ ===")
    
    # Global/CarTextures.tpk (Record #13)
    rec13 = read_rec(13)
    p13 = os.path.join(ASSETS_DIR, "global", "CARTEXTURES.TPK")
    with open(p13, "wb") as f: f.write(rec13)
    print(f"Saved {p13} ({len(rec13):,} bytes)")

    # CopMidsize Textures (Record #122)
    rec122 = read_rec(122)
    p122 = os.path.join(ASSETS_DIR, "copmidsize", "TEXTURES.TPK")
    with open(p122, "wb") as f: f.write(rec122)
    print(f"Saved {p122} ({len(rec122):,} bytes)")

    # InGameTextures.tpk (Record #515)
    rec515 = read_rec(515)
    p515 = os.path.join(ASSETS_DIR, "global", "INGAME_HUD.TPK")
    with open(p515, "wb") as f: f.write(rec515)
    print(f"Saved {p515} ({len(rec515):,} bytes)")

    # HudTexturesSingleRace.tpk (Record #1370)
    rec1370 = read_rec(1370)
    p1370 = os.path.join(ASSETS_DIR, "global", "HUD_RACETEXTURES.TPK")
    with open(p1370, "wb") as f: f.write(rec1370)
    print(f"Saved {p1370} ({len(rec1370):,} bytes)")

    # 5. Extract and Decompress Core Textures into Master RGBA Texture Pack
    print("=== 4. Decoding Master RGBA8888 PS2 Textures ===")
    decoded_textures = {} # name -> (w, h, rgba_bytes)

    # A) Rockport World Textures (from Record #28 embedded uncompressed TPKs)
    # Search TPKs at 0x68800, 0xB3400, 0x17B980, 0x1F2000
    r28_tpk_offsets = [0x0068800, 0x00B3400, 0x017B980, 0x01F2000]
    target_world_names = {
        "TRNS_ALL_LA_PAVEMENTA_Y", "TRNS_ALL_LB_PAVEMENTA_N", "TRN_OLDCONCRETE_SIMPLE",
        "OBJ_BARRIERCONCRETE_06_", "OBJ_BARRIERCONCRETE_01", "OBJ_SPIKEBELT_A",
        "ARC_FACTORYBRICK_01", "ARC_OLDBRICKA_WALLA_LL", "ARC_CAMPUS_HERITAGE4_WA",
        "ARC_CITYEASTBLD10WINDOW", "ARC_WHOUSE_WINDOW_WH", "ARC_HOSPITAL_GARAGEDOOR",
        "ORG_CT_TREELINE_FIR_256", "ORG_CEDAR_TRUNK_JG", "ORG_CEDAR_JG",
        "SGN_FWY_DOWNTOWNROCKPOR", "SGN_CHEVRONSQUARE", "SFX_TRACKBARRIERNEONARR",
        "PAN_COLLEGETOWN_MOUNT_F", "PAN_ISLAND_01A_KT", "PAN_ISLAND_02B_KT",
        "ADS_FITNESSB_BB_EA"
    }

    for tpk_off in r28_tpk_offsets:
        iso.seek(r28_base + tpk_off)
        cid, csz = struct.unpack("<II", iso.read(8))
        tpk = iso.read(csz)
        i4 = tpk.find(b"\x04\x00\x31\x33")
        _, s4 = struct.unpack_from("<II", tpk, i4)
        b4 = tpk[i4 + 8 : i4 + 8 + s4]

        i2 = tpk.find(b"\x02\x00\x32\x33")
        _, s2 = struct.unpack_from("<II", tpk, i2)
        vram = tpk[i2 + 8 + 0x78 : i2 + 8 + s2]

        for idx in range(len(b4) // 124):
            ent = b4[idx * 124 : (idx + 1) * 124]
            name = ent[12:36].split(b"\x00")[0].decode("ascii", "ignore")
            if name in target_world_names and name not in decoded_textures:
                ints = struct.unpack_from("<12I", ent, 36)
                pix_off = ints[3]; pal_off = ints[4]
                w = ints[8] & 0xFFFF; h = ints[8] >> 16
                bpp = (ints[9] >> 16) & 0xFF
                pix_data = vram[pix_off : pix_off + ints[7]]
                pal_data = vram[pal_off : pal_off + ints[6]]
                
                # Unswizzle
                if bpp == 4:
                    unswiz = unswizzle4(pix_data, w, h)
                else:
                    unswiz = unswizzle8(pix_data, w, h)
                rgba = convert_to_rgba32(unswiz, pal_data, w, h, bpp)
                decoded_textures[name] = (w, h, rgba)
                print(f"  Decoded World: {name:<26} {w:3d}x{h:3d} ({bpp}bpp)")

    # B) BMWM3GTRE46 Car Textures (from TEXTURES.BIN)
    with open(os.path.join(ASSETS_DIR, "bmwm3gtre46", "TEXTURES.BIN"), "rb") as f:
        bmw_tpk = f.read()
    i3 = bmw_tpk.find(b"\x03\x00\x31\x33")
    _, s3 = struct.unpack_from("<II", bmw_tpk, i3)
    for i in range(s3 // 24):
        eh, off, csz, usz, flg, u = struct.unpack_from("<6I", bmw_tpk, i3 + 8 + i * 24)
        dec = decompress_mips(bmw_tpk[off : off + csz], usz)
        hdr124 = dec[-208:-84]
        name = hdr124[12:36].split(b"\x00")[0].decode("ascii", "ignore")
        ints = struct.unpack_from("<12I", hdr124, 36)
        w = ints[8] & 0xFFFF; h = ints[8] >> 16; bpp = (ints[9] >> 16) & 0xFF
        pix_data = dec[: ints[7]]
        pal_data = dec[ints[5] : ints[5] + ints[6]]
        unswiz = unswizzle4(pix_data, w, h) if bpp == 4 else unswizzle8(pix_data, w, h)
        rgba = convert_to_rgba32(unswiz, pal_data, w, h, bpp)
        decoded_textures[name] = (w, h, rgba)
        print(f"  Decoded BMW M3: {name:<26} {w:3d}x{h:3d} ({bpp}bpp)")

    # C) BMWM3GTRE46 Hero Vinyl (BMWM3GTRE46_CE02 from Record #98)
    h1, arch, sec, csz, usz, h2 = struct.unpack_from("<6I", zdir, 98 * 24)
    iso.seek((zz_lbas[arch] + sec) * 2048)
    vtpk_data = iso.read(usz)
    i3 = vtpk_data.find(b"\x03\x00\x31\x33")
    _, s3 = struct.unpack_from("<II", vtpk_data, i3)
    for i in range(s3 // 24):
        eh, off, csz, usz, flg, u = struct.unpack_from("<6I", vtpk_data, i3 + 8 + i * 24)
        if off == 1729088: # BMWM3GTRE46_CE02
            dec = decompress_mips(vtpk_data[off : off + csz], usz)
            hdr124 = dec[-208:-84]
            name = "BMWM3GTRE46_CE02"
            ints = struct.unpack_from("<12I", hdr124, 36)
            w = 512; h = 512; bpp = 8
            pix_data = dec[: 512 * 512]
            pal_data = dec[512 * 512 : 512 * 512 + 1024]
            unswiz = unswizzle8(pix_data, w, h)
            rgba = convert_to_rgba32(unswiz, pal_data, w, h, bpp)
            decoded_textures[name] = (w, h, rgba)
            print(f"  Decoded Hero Vinyl: {name:<22} {w:3d}x{h:3d} ({bpp}bpp)")

    # D) Police Crown Victoria (COPMIDSIZE from Record #122)
    i3 = rec122.find(b"\x03\x00\x31\x33")
    _, s3 = struct.unpack_from("<II", rec122, i3)
    for i in range(s3 // 24):
        eh, off, csz, usz, flg, u = struct.unpack_from("<6I", rec122, i3 + 8 + i * 24)
        dec = decompress_mips(rec122[off : off + csz], usz)
        hdr124 = dec[-208:-84]
        name = hdr124[12:36].split(b"\x00")[0].decode("ascii", "ignore")
        ints = struct.unpack_from("<12I", hdr124, 36)
        w = ints[8] & 0xFFFF; h = ints[8] >> 16; bpp = (ints[9] >> 16) & 0xFF
        pix_data = dec[: ints[7]]
        pal_data = dec[ints[5] : ints[5] + ints[6]]
        unswiz = unswizzle4(pix_data, w, h) if bpp == 4 else unswizzle8(pix_data, w, h)
        rgba = convert_to_rgba32(unswiz, pal_data, w, h, bpp)
        decoded_textures[name] = (w, h, rgba)
        print(f"  Decoded Cop Car: {name:<24} {w:3d}x{h:3d} ({bpp}bpp)")

    # E) HUD & Speedometer (HUD_00 from Record #797 & InGameTextures from Record #515)
    rec797 = read_rec(797)
    i3 = rec797.find(b"\x03\x00\x31\x33")
    _, s3 = struct.unpack_from("<II", rec797, i3)
    for i in range(s3 // 24):
        eh, off, csz, usz, flg, u = struct.unpack_from("<6I", rec797, i3 + 8 + i * 24)
        dec = decompress_mips(rec797[off : off + csz], usz)
        hdr124 = dec[-208:-84]
        name = hdr124[12:36].split(b"\x00")[0].decode("ascii", "ignore")
        ints = struct.unpack_from("<12I", hdr124, 36)
        w = ints[8] & 0xFFFF; h = ints[8] >> 16; bpp = (ints[9] >> 16) & 0xFF
        pix_data = dec[: ints[7]]
        pal_data = dec[ints[5] : ints[5] + ints[6]]
        unswiz = unswizzle4(pix_data, w, h) if bpp == 4 else unswizzle8(pix_data, w, h)
        rgba = convert_to_rgba32(unswiz, pal_data, w, h, bpp)
        decoded_textures[name] = (w, h, rgba)
        print(f"  Decoded Tachometer: {name:<21} {w:3d}x{h:3d} ({bpp}bpp)")

    # InGame HUD Icons (from rec515 uncompressed)
    i4 = rec515.find(b"\x04\x00\x31\x33")
    _, s4 = struct.unpack_from("<II", rec515, i4)
    b4 = rec515[i4 + 8 : i4 + 8 + s4]
    i2 = rec515.find(b"\x02\x00\x32\x33")
    _, s2 = struct.unpack_from("<II", rec515, i2)
    vram = rec515[i2 + 8 + 0x78 : i2 + 8 + s2]
    target_hud = {"MAIN_ICON_BOUNTY", "ARROW", "SUNMAJORRAYS", "SUNCENTER", "WORLD_PURSUITBREAKER"}
    for idx in range(len(b4) // 124):
        ent = b4[idx * 124 : (idx + 1) * 124]
        name = ent[12:36].split(b"\x00")[0].decode("ascii", "ignore")
        if name in target_hud and name not in decoded_textures:
            ints = struct.unpack_from("<12I", ent, 36)
            w = ints[8] & 0xFFFF; h = ints[8] >> 16; bpp = (ints[9] >> 16) & 0xFF
            pix_data = vram[ints[3] : ints[3] + ints[7]]
            pal_data = vram[ints[4] : ints[4] + ints[6]]
            unswiz = unswizzle4(pix_data, w, h) if bpp == 4 else unswizzle8(pix_data, w, h)
            rgba = convert_to_rgba32(unswiz, pal_data, w, h, bpp)
            decoded_textures[name] = (w, h, rgba)
            print(f"  Decoded HUD Icon: {name:<23} {w:3d}x{h:3d} ({bpp}bpp)")

    # Speedometer backing (from rec1370)
    i4 = rec1370.find(b"\x04\x00\x31\x33")
    _, s4 = struct.unpack_from("<II", rec1370, i4)
    b4 = rec1370[i4 + 8 : i4 + 8 + s4]
    i2 = rec1370.find(b"\x02\x00\x32\x33")
    _, s2 = struct.unpack_from("<II", rec1370, i2)
    vram = rec1370[i2 + 8 + 0x78 : i2 + 8 + s2]
    target_race_hud = {"METER_BACKING", "PURSUIT_BACKING_TOP", "REDLINE_MASK"}
    for idx in range(len(b4) // 124):
        ent = b4[idx * 124 : (idx + 1) * 124]
        name = ent[12:36].split(b"\x00")[0].decode("ascii", "ignore")
        if name in target_race_hud and name not in decoded_textures:
            ints = struct.unpack_from("<12I", ent, 36)
            w = ints[8] & 0xFFFF; h = ints[8] >> 16; bpp = (ints[9] >> 16) & 0xFF
            pix_data = vram[ints[3] : ints[3] + ints[7]]
            pal_data = vram[ints[4] : ints[4] + ints[6]]
            unswiz = unswizzle4(pix_data, w, h) if bpp == 4 else unswizzle8(pix_data, w, h)
            rgba = convert_to_rgba32(unswiz, pal_data, w, h, bpp)
            decoded_textures[name] = (w, h, rgba)
            print(f"  Decoded Race HUD: {name:<23} {w:3d}x{h:3d} ({bpp}bpp)")

    # 6. Save Master Binary Texture Pack (PS2_TEXTURES.BIN)
    # Binary layout:
    # Header: "NFSWTEX1", uint32 num_textures
    # For each texture:
    #   char name[32] (null-padded)
    #   uint32 width, uint32 height, uint32 data_len
    #   byte rgba_pixels[data_len]
    out_tex_path = os.path.join(ASSETS_DIR, "textures", "PS2_TEXTURES.BIN")
    print(f"\n=== 5. Writing Master Texture Pack to {out_tex_path} ===")
    with open(out_tex_path, "wb") as f:
        f.write(b"NFSWTEX1")
        f.write(struct.pack("<I", len(decoded_textures)))
        for name, (w, h, rgba) in decoded_textures.items():
            name_bytes = name.encode("ascii")[:31].ljust(32, b"\x00")
            f.write(name_bytes)
            f.write(struct.pack("<3I", w, h, len(rgba)))
            f.write(rgba)
    
    print(f"SUCCESS! Wrote {len(decoded_textures)} authentic PS2 textures ({os.path.getsize(out_tex_path):,} bytes) to {out_tex_path}")
