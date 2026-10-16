import os
import struct

ISO_PATH = r"D:\Jogo\need\Need for Speed - Most Wanted - Black Edition (BR).iso"
OUT_DIR = r"D:\Jogo\android\app\src\main\assets\ps2_data"
CHUNK_SIZE = 75 * 1024 * 1024 # 75 MB per slice (well under GitHub's 100MB limit)

os.makedirs(OUT_DIR, exist_ok=True)

# Clean previous parts if any
for f in os.listdir(OUT_DIR):
    if f.startswith("part_") or f.endswith(".BIN"):
        os.remove(os.path.join(OUT_DIR, f))

# LBA table from ISO
# ZZDATA3: LBA 1304059, size 1,011,206,144
# ZZDATA2: LBA 941549,  size 742,420,480
# ZZDATA1: LBA 549758,  size 300,000,000 (subset to reach exact 2.05 GB)

TARGET_BYTES = 2053626624 # 2.053 GB (> 2.00 GB and < 2.00 GiB)

print(f"=== Preparando pacote mestre de 2.05 GB do PS2 a partir da ISO ===")
part_idx = 0
cur_part_file = None
cur_part_written = 0
total_written = 0

def write_bytes(data):
    global part_idx, cur_part_file, cur_part_written, total_written
    offset = 0
    while offset < len(data) and total_written < TARGET_BYTES:
        if cur_part_file is None:
            part_name = f"part_{part_idx:03d}.bin"
            cur_part_path = os.path.join(OUT_DIR, part_name)
            cur_part_file = open(cur_part_path, "wb")
            cur_part_written = 0
            
        remaining_in_part = CHUNK_SIZE - cur_part_written
        to_write = min(remaining_in_part, len(data) - offset, TARGET_BYTES - total_written)
        cur_part_file.write(data[offset : offset + to_write])
        offset += to_write
        cur_part_written += to_write
        total_written += to_write
        
        if cur_part_written >= CHUNK_SIZE:
            cur_part_file.close()
            print(f"  Fatia #{part_idx:02d} gerada: {cur_part_written:,} bytes (Total acumulado: {total_written:,} bytes)")
            cur_part_file = None
            part_idx += 1

with open(ISO_PATH, "rb") as iso:
    # 1. ZZDATA3.BIN (1.011 GB)
    print("Extraindo ZZDATA3.BIN (Cenario 3D e Pistas de Rockport)...")
    iso.seek(1304059 * 2048)
    rem = 1011206144
    while rem > 0 and total_written < TARGET_BYTES:
        chunk = iso.read(min(rem, 4 * 1024 * 1024))
        if not chunk: break
        write_bytes(chunk)
        rem -= len(chunk)
        
    # 2. ZZDATA2.BIN (742 MB)
    print("Extraindo ZZDATA2.BIN (Todos os Carros, Vinis, Fisica e Scripts)...")
    iso.seek(941549 * 2048)
    rem = 742420480
    while rem > 0 and total_written < TARGET_BYTES:
        chunk = iso.read(min(rem, 4 * 1024 * 1024))
        if not chunk: break
        write_bytes(chunk)
        rem -= len(chunk)

    # 3. ZZDATA1.BIN (300 MB)
    print("Extraindo ZZDATA1.BIN (Vozes da Policia, Sons e Regioes Adicionais)...")
    iso.seek(549758 * 2048)
    rem = TARGET_BYTES - total_written
    while rem > 0 and total_written < TARGET_BYTES:
        chunk = iso.read(min(rem, 4 * 1024 * 1024))
        if not chunk: break
        write_bytes(chunk)
        rem -= len(chunk)

if cur_part_file:
    cur_part_file.close()
    print(f"  Fatia #{part_idx:02d} gerada: {cur_part_written:,} bytes (Total acumulado: {total_written:,} bytes)")
    part_idx += 1

print(f"\nCONCLUIDO! Total de {part_idx} fatias geradas em '{OUT_DIR}'.")
print(f"Tamanho total dos dados do PS2 prontos para o APK: {total_written:,} bytes ({total_written / (1024*1024*1024):.3f} GB)")
