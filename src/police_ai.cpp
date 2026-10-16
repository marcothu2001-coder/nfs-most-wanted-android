#include "police_ai.h"
#include <iostream>
#include <cmath>

PursuitSystem::PursuitSystem() {
    SetHeatLevel(HEAT_1_PATROL);
}

void PursuitSystem::SetHeatLevel(HeatLevel level) {
    m_currentHeat = level;
    std::cout << "[PoliceAI] Nivel de Pressao alterado para HEAT " << m_currentHeat << "!\n";
    SpawnPursuitWave();
}

void PursuitSystem::SpawnPursuitWave() {
    m_activeUnits.clear();
    int count = 2;
    std::string model = "COPMIDSIZE";

    switch (m_currentHeat) {
        case HEAT_1_PATROL:
            count = 2;
            model = "COPMIDSIZE"; // Crown Vic
            break;
        case HEAT_2_UNDERCOVER:
            count = 4;
            model = "COPMIDSIZE"; // Descaracterizada
            break;
        case HEAT_3_STATE_GTO:
            count = 6;
            model = "COPMIDSIZE"; // GTO
            break;
        case HEAT_4_POLICE_VETTE:
            count = 8;
            model = "COPSPORT";   // Corvette
            break;
        case HEAT_5_CROSS_RHINO:
            count = 10;
            model = "COPSUV";     // Heavy Rhino + Cross
            break;
    }

    for (int i = 0; i < count; ++i) {
        PoliceUnit u;
        u.unitId = 100 + i;
        u.vehicleModel = model;
        u.state = COP_ENGAGE;
        u.health = 100.0f;
        u.aggressionFactor = 0.5f + (static_cast<f32>(m_currentHeat) * 0.15f);
        u.position = { (i % 2 == 0 ? 10.0f : -10.0f), 0.2f, -30.0f - (i * 15.0f) };
        u.velocity = { 0.0f, 0.0f, 40.0f };
        m_activeUnits.push_back(u);
    }

    std::cout << "[PoliceAI] Despachadas " << m_activeUnits.size() 
              << " viaturas modelo '" << model << "' para interceptacao.\n";
}

void PursuitSystem::Update(f32 dt, const Vector3& playerPos, const Vector3& playerVel) {
    if (dt <= 0.0f) return;
    m_pursuitTimer += dt;

    int closeCops = 0;
    for (size_t i = 0; i < m_activeUnits.size(); ++i) {
        auto& unit = m_activeUnits[i];
        if (unit.state == COP_WRECKED) continue;

        // Vetor de aproximação até o jogador
        f32 targetX = playerPos.x + ((i % 2 == 0) ? -2.2f : 2.2f);
        f32 targetZ = playerPos.z - 6.0f - (static_cast<f32>(i) * 5.0f) + std::sin(m_pursuitTimer * 1.5f + i) * 4.0f;

        f32 dx = targetX - unit.position.x;
        f32 dz = targetZ - unit.position.z;
        f32 dist = std::sqrt(dx * dx + dz * dz);

        f32 distToPlayer = std::sqrt(
            (playerPos.x - unit.position.x) * (playerPos.x - unit.position.x) +
            (playerPos.z - unit.position.z) * (playerPos.z - unit.position.z)
        );

        if (distToPlayer < 18.0f) {
            closeCops++;
        }

        if (dist > 0.001f) {
            f32 catchup = std::min(dist * 3.5f, 75.0f * unit.aggressionFactor);
            unit.position.x += (dx / dist) * catchup * 0.5f * dt;
            unit.position.z += (dz / dist) * catchup * dt;
        }
    }

    // Lógica do Medidor de Busted (Preso) vs Evaded (Fuga)
    f32 playerSpeed = std::sqrt(playerVel.x * playerVel.x + playerVel.z * playerVel.z);
    
    if (closeCops > 0 && playerSpeed < 4.0f) {
        // Jogador quase parado com polícia perto: barra de BUSTED enche!
        m_bustedProgress += dt * (15.0f * closeCops);
        m_evadeProgress = 0.0f;
    } else if (closeCops == 0) {
        // Longe da polícia: barra de EVADE (Fuga / Cooldown) enche!
        m_bustedProgress = std::max(0.0f, m_bustedProgress - (dt * 10.0f));
        m_evadeProgress += dt * 8.0f;
    }

    m_bustedProgress = std::clamp(m_bustedProgress, 0.0f, 100.0f);
    m_evadeProgress = std::clamp(m_evadeProgress, 0.0f, 100.0f);
}

void PursuitSystem::DeployRoadblock(const Vector3& targetRoadPos, bool withSpikeStrip) {
    std::cout << "[PoliceAI] BLOQUEIO POLICIAL implantado na posicao (" 
              << targetRoadPos.x << ", " << targetRoadPos.z << ")" 
              << (withSpikeStrip ? " COM TAPETE DE PREGOS!" : "!") << "\n";
}

void PursuitSystem::CallRhinoCharge(const Vector3& playerPos) {
    std::cout << "[PoliceAI] CODIGO VERMELHO: RHINO SUV enviado em alta velocidade na contramao!\n";
}

int PursuitSystem::GetActiveCopsCount() const {
    int count = 0;
    for (const auto& u : m_activeUnits) {
        if (u.state != COP_WRECKED) count++;
    }
    return count;
}
