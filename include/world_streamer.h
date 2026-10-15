#pragma once
#include "nfs_types.h"
#include <string>
#include <vector>
#include <unordered_map>

// Setor do mapa de Rockport
struct CitySector {
    u32 sectorHash;
    std::string zoneName; // Ex: "Downtown", "Camden_Beach", "Rosewood"
    Vector3 center;
    f32 radius;
    bool isLoaded;
    u32 zzdataArchive;
    u32 sectorOffset;
    size_t uncompressedSize;
};

class WorldStreamer {
public:
    WorldStreamer();
    ~WorldStreamer() = default;

    bool Init(const std::string& zdirPath);
    void Update(const Vector3& playerPosition);

    // Carregamento Assíncrono de Setores
    void LoadSector(CitySector& sector);
    void UnloadSector(CitySector& sector);

    // Consulta de Colisão com o Asfalto / Barreiras
    bool RaycastRoad(const Vector3& origin, const Vector3& direction, Vector3& outHitPoint, Vector3& outHitNormal);

    const std::vector<CitySector>& GetActiveSectors() const { return m_sectors; }

private:
    std::vector<CitySector> m_sectors;
    f32 m_streamingRadius = 600.0f; // Raio em metros para carregar blocos de pista
};
