#include "vehicle_physics.h"
#include <cmath>
#include <algorithm>
#include <iostream>

VehicleSimulation::VehicleSimulation() {
    m_position = { 0.0f, 0.5f, 0.0f };
    m_velocity = { 0.0f, 0.0f, 0.0f };
    m_angularVelocity = { 0.0f, 0.0f, 0.0f };

    // Geometria padrão de suspensão de 4 rodas (BMW M3 GTR)
    // 0=Diant.Esq, 1=Diant.Dir, 2=Traseira.Esq, 3=Traseira.Dir
    m_wheels[0].localPosition = { -0.8f, -0.2f,  1.4f };
    m_wheels[1].localPosition = {  0.8f, -0.2f,  1.4f };
    m_wheels[2].localPosition = { -0.8f, -0.2f, -1.4f };
    m_wheels[3].localPosition = {  0.8f, -0.2f, -1.4f };

    for (int i = 0; i < 4; ++i) {
        m_wheels[i].suspensionRestLength = 0.35f;
        m_wheels[i].suspensionCurrentLen = 0.35f;
        m_wheels[i].springRate = 35000.0f; // 35 kN/m
        m_wheels[i].damperRate = 4500.0f;  // 4.5 kNs/m
        m_wheels[i].tireGrip = 1.35f;      // Pneu esportivo de alta aderência
        m_wheels[i].isOnGround = true;
    }
}

void VehicleSimulation::Configure(const VehicleSpecs& specs) {
    m_specs = specs;
}

void VehicleSimulation::Update(f32 dt, f32 throttle, f32 brake, f32 steer, bool handbrake, bool nos) {
    if (dt <= 0.0f) return;

    // 1. Atualizar esterçamento nas rodas dianteiras
    m_wheels[0].steerAngle = steer * 0.55f; // ~31 graus max
    m_wheels[1].steerAngle = steer * 0.55f;

    // 2. Transmissão e Motor
    UpdatePowertrain(dt, throttle, nos);

    // 3. Suspensão
    UpdateSuspension(dt);

    // 4. Forças de Atrito dos Pneus
    UpdateTireForces(dt, steer, brake, handbrake);

    // 5. Integração Newtoniana
    IntegrateRigidBody(dt);
}

void VehicleSimulation::UpdatePowertrain(f32 dt, f32 throttle, bool nos) {
    f32 targetSpeed = m_velocity.z * 3.6f; // km/h
    m_speedKmh = std::abs(targetSpeed);

    // Seleção automática de marchas
    if (m_speedKmh < 60.0f) m_currentGear = 1;
    else if (m_speedKmh < 105.0f) m_currentGear = 2;
    else if (m_speedKmh < 155.0f) m_currentGear = 3;
    else if (m_speedKmh < 210.0f) m_currentGear = 4;
    else if (m_speedKmh < 270.0f) m_currentGear = 5;
    else m_currentGear = 6;

    // Cálculo da Rotação (RPM)
    f32 gearRatio = (m_specs.gearRatios.size() > static_cast<size_t>(m_currentGear)) 
                    ? m_specs.gearRatios[m_currentGear] : 1.0f;
    f32 wheelRPM = (m_speedKmh / 3.6f) / (0.32f * 2.0f * 3.14159f) * 60.0f;
    m_engineRPM = wheelRPM * gearRatio * m_specs.finalDriveRatio;
    m_engineRPM = std::clamp(m_engineRPM, m_specs.idleRPM, m_specs.maxEngineRPM);

    // Injeção de Nitro (NOS)
    if (nos && m_nosRemaining > 0.0f) {
        m_nosRemaining -= dt * 15.0f; // consome 15% por segundo
        m_velocity.z += dt * 12.0f;   // aceleração extra brutal
    }
}

void VehicleSimulation::UpdateSuspension(f32 dt) {
    // Raycasting com o asfalto (Y = 0)
    for (int i = 0; i < 4; ++i) {
        f32 wheelWorldY = m_position.y + m_wheels[i].localPosition.y;
        f32 compression = m_wheels[i].suspensionRestLength - wheelWorldY;
        if (compression > 0.0f) {
            m_wheels[i].suspensionCurrentLen = m_wheels[i].suspensionRestLength - compression;
            m_wheels[i].isOnGround = true;
            // Força da mola F = -k * x
            f32 springForce = m_wheels[i].springRate * compression;
            m_velocity.y += (springForce / m_specs.mass) * dt;
        } else {
            m_wheels[i].isOnGround = false;
        }
    }
}

void VehicleSimulation::UpdateTireForces(f32 dt, f32 steer, f32 brake, bool handbrake) {
    // Resistência do Ar (Arrasto Aerodinâmico)
    f32 drag = 0.5f * 1.225f * m_specs.aeroDragCoeff * (m_velocity.z * m_velocity.z);
    if (m_velocity.z > 0.0f) m_velocity.z -= (drag / m_specs.mass) * dt;

    // Frenagem e Freio de Mão
    if (brake > 0.0f) {
        m_velocity.z -= brake * 18.0f * dt;
    }
    if (handbrake) {
        // Trava as rodas traseiras para induzir derrapagem (drift)
        m_wheels[2].tireGrip = 0.35f;
        m_wheels[3].tireGrip = 0.35f;
    } else {
        m_wheels[2].tireGrip = 1.35f;
        m_wheels[3].tireGrip = 1.35f;
    }

    // Aplica rotação ao chassi com base no esterço das rodas
    if (std::abs(m_velocity.z) > 1.0f) {
        m_angularVelocity.y = steer * (m_velocity.z / 25.0f);
    } else {
        m_angularVelocity.y = 0.0f;
    }
}

void VehicleSimulation::IntegrateRigidBody(f32 dt) {
    // Gravidade
    m_velocity.y -= 9.81f * dt;

    // Limite de contato com o solo
    if (m_position.y < 0.2f) {
        m_position.y = 0.2f;
        if (m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    }

    // Integração de posição
    m_position.x += m_velocity.x * dt;
    m_position.y += m_velocity.y * dt;
    m_position.z += m_velocity.z * dt;
}
