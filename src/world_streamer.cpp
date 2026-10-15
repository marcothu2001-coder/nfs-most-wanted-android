#include "world_streamer.h"
#include <iostream>
#include <cmath>

WorldStreamer::WorldStreamer() {
    // Inicialização dos setores clássicos de Rockport City
    m_sectors = {
        { 0x0035fc77, "Camden_Beach",    { -1200.0f, 0.0f, -800.0f }, 450.0f, false, 1, 0x0002f805, 18309040 },
        { 0x07b41eba, "Downtown_Rockport",{     0.0f, 0.0f,    0.0f }, 600.0f, false, 1, 0x0002264d, 18336452 },
        { 0x121fda56, "Rosewood_Center", {  1500.0f, 0.0f,  950.0f }, 500.0f, false, 1, 0x0002d4eb, 18402364 },
        { 0x199dfc99, "Rockport_Turnpike",{   400.0f, 0.0f, 1800.0f }, 700.0f, false, 1, 0x00020366, 18298608 },
        { 0x4ef66b8c, "Highway_Zone_01", { -2500.0f, 0.0f,  200.0f }, 650.0f, false, 0, 0x00000000, 52428800 }
    };
}

bool WorldStreamer::Init(const std::string& zdirPath) {
    std::cout << "[WorldStreamer] Sistema de streaming de Rockport inicializado.\n";
    std::cout << "[WorldStreamer] Raio de carregamento continuo: " << m_streamingRadius << " metros.\n";
    return true;
}

void WorldStreamer::Update(const Vector3& playerPosition) {
    for (auto& sector : m_sectors) {
        f32 dx = playerPosition.x - sector.center.x;
        f32 dz = playerPosition.z - sector.center.z;
        f32 dist = std::sqrt(dx * dx + dz * dz);

        if (dist <= m_streamingRadius && !sector.isLoaded) {
            LoadSector(sector);
        } else if (dist > (m_streamingRadius + 150.0f) && sector.isLoaded) {
            UnloadSector(sector);
        }
    }
}

void WorldStreamer::LoadSector(CitySector& sector) {
    sector.isLoaded = true;
    std::cout << "[WorldStreamer] STREAM IN: Setor '" << sector.zoneName 
              << "' carregado da fatia ZZDATA" << sector.zzdataArchive 
              << ".BIN (" << (sector.uncompressedSize / 1024 / 1024) << " MB).\n";
}

void WorldStreamer::UnloadSector(CitySector& sector) {
    sector.isLoaded = false;
    std::cout << "[WorldStreamer] STREAM OUT: Setor '" << sector.zoneName 
              << "' liberado da memoria RAM.\n";
}

bool WorldStreamer::RaycastRoad(const Vector3& origin, const Vector3& direction, Vector3& outHitPoint, Vector3& outHitNormal) {
    // Simulação do asfalto de Rockport na altura Y = 0.0
    if (origin.y >= 0.0f && direction.y < 0.0f) {
        f32 t = -origin.y / direction.y;
        outHitPoint = { origin.x + direction.x * t, 0.0f, origin.z + direction.z * t };
        outHitNormal = { 0.0f, 1.0f, 0.0f };
        return true;
    }
    return false;
}
