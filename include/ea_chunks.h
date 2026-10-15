#pragma once
#include "nfs_types.h"

// IDs de Chunks conhecidos da EA Black Box (NFS Most Wanted / Carbon / UG2)
enum EAChunkID : u32 {
    // Chunks de Geometria de Carros
    CHUNK_CAR_GEOMETRY_ROOT   = 0x80134000,
    CHUNK_CAR_PART_HEADER     = 0x80134001,
    CHUNK_GEOMETRY_INFO       = 0x00134002,
    CHUNK_PART_HASH_LIST      = 0x00134003,
    CHUNK_PART_MESH_DATA      = 0x80134010,
    CHUNK_VERTEX_BUFFER       = 0x00134011,
    CHUNK_INDEX_BUFFER        = 0x00134012,
    CHUNK_MATERIAL_INFO       = 0x00134013,
    CHUNK_MESH_PAYLOAD        = 0x80134100,

    // Chunks de Texturas
    CHUNK_TEXTURE_PACK_ROOT   = 0xb3300000,
    CHUNK_TEXTURE_HEADER_GRP  = 0xb3310000,
    CHUNK_TEXTURE_HEADER      = 0x33310001,
    CHUNK_TEXTURE_DATA_INFO   = 0x33310002,
    CHUNK_TEXTURE_PAYLOAD_GRP = 0xb3320000,
    CHUNK_TEXTURE_PAYLOAD     = 0x33320002
};

#pragma pack(push, 1)

// Cabeçalho de 8 bytes de qualquer Chunk da EA
struct EAChunkHeader {
    u32 id;
    u32 length; // Tamanho dos dados subsequentes ao cabeçalho

    inline bool isContainer() const {
        return (id & 0x80000000) != 0 || id == CHUNK_TEXTURE_PACK_ROOT;
    }

    inline u32 getAlignedLength() const {
        return (length + 3) & ~3;
    }
};

// Registro de 24 bytes do ZDIR.BIN
struct ZDirRecord {
    u32 primary_hash;    // bStringHash do caminho
    u32 archive_idx;     // 0 = ZZDATA0, 1 = ZZDATA1, etc.
    u32 sector_offset;   // Offset em setores de 2048 bytes
    u32 compressed_sz;   // Tamanho compactado
    u32 uncompressed_sz; // Tamanho original
    u32 secondary_hash;  // Hash auxiliar / CRC
};

#pragma pack(pop)
