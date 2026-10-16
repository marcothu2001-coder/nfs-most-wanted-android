#pragma once
#include "nfs_types.h"
#include <string>
#include <vector>

// Níveis de Pressão Policial (Heat Level 1 a 5+)
enum HeatLevel : int {
    HEAT_1_PATROL       = 1, // Viaturas locais Crown Victoria
    HEAT_2_UNDERCOVER   = 2, // Viaturas pretas descaracterizadas
    HEAT_3_STATE_GTO    = 3, // Viaturas estaduais Pontiac GTO + Bloqueios leves
    HEAT_4_POLICE_VETTE = 4, // Corvettes de perseguição + Tapetes de pregos
    HEAT_5_CROSS_RHINO  = 5  // Sargento Cross + Helicóptero + SUVs Pesados de frente
};

// Estados da IA Policial
enum CopState {
    COP_PATROL,        // Patrulhando rotas normais
    COP_ENGAGE,        // Perseguição ativa (cola na traseira)
    COP_RAM,           // Manobra de abalroamento (bater na lateral para rodar)
    COP_BOX_IN,        // Manobra de cerco (cercar os 4 lados do jogador)
    COP_ROADBLOCK,     // Posicionado em barreira física
    COP_BUSTED,        // Jogador imobilizado (barra de prisão cheia)
    COP_WRECKED        // Viatura destruída / fora de combate
};

struct PoliceUnit {
    int unitId;
    std::string vehicleModel; // Ex: "COPMIDSIZE", "COPSPORT", "COPSUV"
    Vector3 position;
    Vector3 velocity;
    CopState state;
    f32 health;
    f32 aggressionFactor;
};

class PursuitSystem {
public:
    PursuitSystem();
    ~PursuitSystem() = default;

    void SetHeatLevel(HeatLevel level);
    void Update(f32 dt, const Vector3& playerPos, const Vector3& playerVel);

    // Disparo de Táticas Policiais
    void SpawnPursuitWave();
    void DeployRoadblock(const Vector3& targetRoadPos, bool withSpikeStrip);
    void CallRhinoCharge(const Vector3& playerPos);

    // Getters
    HeatLevel GetCurrentHeat() const { return m_currentHeat; }
    int GetActiveCopsCount() const;
    f32 GetBustedMeter() const { return m_bustedProgress; }
    f32 GetEvadeMeter() const { return m_evadeProgress; }
    bool IsPlayerBusted() const { return m_bustedProgress >= 100.0f; }
    bool IsPlayerEvaded() const { return m_evadeProgress >= 100.0f; }
    const std::vector<PoliceUnit>& GetUnits() const { return m_activeUnits; }

private:
    HeatLevel m_currentHeat = HEAT_1_PATROL;
    std::vector<PoliceUnit> m_activeUnits;
    f32 m_bustedProgress = 0.0f; // 0% a 100%
    f32 m_evadeProgress = 0.0f;  // Cooldown de fuga (0% a 100%)
    f32 m_pursuitTimer = 0.0f;
    int m_copsWrecked = 0;
};
