"""
NFS Most Wanted (PS2) - Asset Lookup & Extractor Tool
Baseado na engenharia reversa do ZDIR.BIN e ZZDATA*.BIN da EA Black Box.
"""

import struct
import os
import sys

def b_string_hash(s: str) -> int:
    """Calcula o bStringHash de 32 bits usado pela EA Black Box."""
    h = 0xffffffff
    for c in s.upper().encode('latin-1'):
        h = ((h * 33) + c) & 0xffffffff
    return h

class ZDirCatalog:
    def __init__(self, zdir_path: str):
        self.records = []
        self.by_hash = {}
        with open(zdir_path, 'rb') as f:
            data = f.read()
            
        record_count = len(data) // 24
        for i in range(record_count):
            primary_hash, archive_idx, sector_offset, comp_sz, uncomp_sz, secondary_hash = struct.unpack('<IIIIII', data[i*24:(i+1)*24])
            rec = {
                'index': i,
                'archive': f'ZZDATA{archive_idx}.BIN',
                'archive_idx': archive_idx,
                'sector_offset': sector_offset,
                'byte_offset': sector_offset * 2048,
                'comp_size': comp_sz,
                'uncomp_size': uncomp_sz,
                'primary_hash': primary_hash,
                'secondary_hash': secondary_hash
            }
            self.records.append(rec)
            self.by_hash[primary_hash] = rec
            if secondary_hash not in self.by_hash:
                self.by_hash[secondary_hash] = rec

    def lookup(self, filename: str):
        h = b_string_hash(filename)
        return self.by_hash.get(h)

def main():
    zdir_path = os.path.join(os.path.dirname(__file__), 'ZDIR.BIN')
    if not os.path.exists(zdir_path):
        print(f"Erro: {zdir_path} nao encontrado.")
        return

    catalog = ZDirCatalog(zdir_path)
    print(f"Catalogo ZDIR carregado com sucesso: {len(catalog.records)} arquivos mapeados.")

    query = sys.argv[1] if len(sys.argv) > 1 else 'CARS\\BMWM3\\GEOMETRY.BIN'
    h = b_string_hash(query)
    print(f"\nBuscando arquivo: '{query}' (bStringHash: 0x{h:08x})...")
    
    match = catalog.lookup(query)
    if match:
        print(f"-> Localizado com sucesso no Registro #{match['index']}!")
        print(f"   Arquivo Contedor : {match['archive']}")
        print(f"   Setor no Disco   : Setor 0x{match['sector_offset']:08x}")
        print(f"   Offset em Bytes  : 0x{match['byte_offset']:08x} ({match['byte_offset']:,} bytes)")
        print(f"   Tam. Compactado  : {match['comp_size']:,} bytes")
        print(f"   Tam. Descompactado: {match['uncomp_size']:,} bytes")
        print(f"   Header Esperado  : 'VPAK' (0x4b415056)")
    else:
        print("-> Arquivo nao encontrado com este caminho exato.")

if __name__ == '__main__':
    main()
