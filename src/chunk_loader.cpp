#include "ea_chunks.h"
#include <iostream>
#include <vector>
#include <string>

struct CarPartMesh {
    u32 partHash;
    std::string name;
    u32 vertexCount;
    u32 triangleCount;
    std::vector<Vector3> positions;
    std::vector<u16> indices;
};

class EAChunkLoader {
public:
    static void InspectGeometry(const u8* data, size_t totalSize) {
        if (totalSize < sizeof(EAChunkHeader)) return;

        const auto* root = reinterpret_cast<const EAChunkHeader*>(data);
        if (root->id != CHUNK_CAR_GEOMETRY_ROOT) {
            std::cout << "[ChunkLoader] Cabeçalho raiz nao e CAR_GEOMETRY_ROOT.\n";
            return;
        }

        std::cout << "[ChunkLoader] CAR_GEOMETRY_ROOT identificado. Tamanho de dados: " 
                  << root->length << " bytes.\n";

        size_t offset = sizeof(EAChunkHeader);
        size_t end = sizeof(EAChunkHeader) + root->length;
        int partCount = 0;

        while (offset + sizeof(EAChunkHeader) <= end && offset < totalSize) {
            const auto* sub = reinterpret_cast<const EAChunkHeader*>(data + offset);
            u32 aligned = sub->getAlignedLength();

            if (sub->id == CHUNK_PART_MESH_DATA) {
                partCount++;
            }

            offset += sizeof(EAChunkHeader) + aligned;
        }

        std::cout << "[ChunkLoader] Total de partes 3D de malha detectadas: " << partCount << "\n";
    }
};
