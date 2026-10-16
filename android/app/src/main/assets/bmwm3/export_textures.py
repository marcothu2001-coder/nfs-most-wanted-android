"""
Exportador de texturas de TEXTURES.BIN para a BMW M3
"""
import struct
import os
import json

def export_textures():
    bin_path = 'D:/Jogo/extracted/bmwm3/TEXTURES.BIN'
    out_dir = 'D:/Jogo/extracted/bmwm3/textures'
    os.makedirs(out_dir, exist_ok=True)
    
    with open(bin_path, 'rb') as f:
        data = f.read()

    # Localizar metadados 0x33310003
    pos_meta = data.find(b'\x03\x00\x31\x33')
    cid, meta_len = struct.unpack('<II', data[pos_meta:pos_meta+8])
    meta_bytes = data[pos_meta+8 : pos_meta+8+meta_len]
    num_textures = meta_len // 24
    
    # Localizar payload 0x33320002
    pos_payload = data.find(b'\x02\x00\x32\x33')
    cid, pay_len = struct.unpack('<II', data[pos_payload:pos_payload+8])
    payload = data[pos_payload+8 : pos_payload+8+pay_len]
    
    print(f"Extraindo {num_textures} texturas do contêiner TPK...")
    
    manifest = []
    for idx in range(num_textures):
        rec = meta_bytes[idx*24 : (idx+1)*24]
        tex_hash, offset, size, fmt_flags, mip_flags, _ = struct.unpack('<IIIIII', rec)
        
        tex_data = payload[offset : offset + size]
        bin_filename = f"texture_{idx:02d}_0x{tex_hash:08x}.bin"
        bin_filepath = os.path.join(out_dir, bin_filename)
        
        with open(bin_filepath, 'wb') as out_f:
            out_f.write(tex_data)
            
        info = {
            "index": idx,
            "hash": f"0x{tex_hash:08x}",
            "offset": offset,
            "size": size,
            "flags": f"0x{fmt_flags:08x}",
            "file": bin_filename
        }
        manifest.append(info)
        print(f"  [Tex #{idx}] Hash: 0x{tex_hash:08x} -> {bin_filename} ({size:,} bytes)")
        
    manifest_path = os.path.join(out_dir, "textures_manifest.json")
    with open(manifest_path, 'w') as mf:
        json.dump(manifest, mf, indent=2)
        
    print(f"\nManifesto de texturas gerado em: {manifest_path}")

if __name__ == '__main__':
    export_textures()
