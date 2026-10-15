#pragma once
#include "nfs_types.h"
#include <vector>

// Modelo de Física de Veículos do NFS Most Wanted
// Baseado na engenharia reversa do bloco 0x002337a0 - 0x002501b0 do SLUS_213.51

struct WheelPhysics {
    Vector3 localPosition;    // Posição da roda em relação ao chassi
    f32 suspensionRestLength; // Comprimento da mola em repouso
    f32 suspensionCurrentLen; // Comprimento atual comprimido
    f32 springRate;           // Rigidez da mola (K)
    f32 damperRate;           // Amortecimento (C)
    f32 tireGrip;             // Coeficiente de atrito lateral/longitudinal
    f32 steerAngle;           // Ângulo de esterçamento (rodas dianteiras)
    f32 rotationSpeed;        // Velocidade angular de rotação da roda
    f32 brakeTorque;          // Torque de frenagem aplicado
    bool isOnGround;          // Contato com a pista
    Vector3 contactPoint;     // Ponto de contato no solo
    Vector3 contactNormal;    // Normal da pista no ponto de contato
};

struct VehicleSpecs {
    f32 mass;                 // Massa em kg (Ex: BMW M3 GTR = 1350 kg)
    Vector3 centerOfMass;     // Centro de gravidade
    f32 maxEngineRPM;         // Rotação máxima (ex: 8500 RPM)
    f32 idleRPM;              // Rotação em ponto morto (ex: 1000 RPM)
    f32 peakTorque;           // Torque máximo em Nm (ex: 480 Nm)
    std::vector<f32> gearRatios; // Relações de marcha (1ª a 6ª + Ré)
    f32 finalDriveRatio;      // Relação do diferencial final
    f32 aeroDragCoeff;        // Coeficiente aerodinâmico de arrasto
    f32 aeroDownforceCoeff;   // Força descendente em alta velocidade
    f32 nosCapacity;          // Capacidade de Nitro
    f32 nosBoostMultiplier;   // Multiplicador de torque com Nitro
};

class VehicleSimulation {
public:
    VehicleSimulation();
    ~VehicleSimulation() = default;

    void Configure(const VehicleSpecs& specs);
    void Update(f32 deltaTime, f32 throttle, f32 brake, f32 steer, bool handbrake, bool nos);

    // Getters de Estado
    Vector3 GetPosition() const { return m_position; }
    Vector3 GetVelocity() const { return m_velocity; }
    f32 GetSpeedKmh() const { return m_speedKmh; }
    f32 GetRPM() const { return m_engineRPM; }
    int GetCurrentGear() const { return m_currentGear; }
    f32 GetNosRemaining() const { return m_nosRemaining; }

private:
    VehicleSpecs m_specs;
    WheelPhysics m_wheels[4]; // 0=Diant.Esq, 1=Diant.Dir, 2=Traseira.Esq, 3=Traseira.Dir

    Vector3 m_position;
    Vector3 m_velocity;
    Vector3 m_angularVelocity;
    Matrix4x4 m_orientation;

    f32 m_speedKmh = 0.0f;
    f32 m_engineRPM = 1000.0f;
    int m_currentGear = 1;
    f32 m_nosRemaining = 100.0f;

    void UpdateSuspension(f32 dt);
    void UpdatePowertrain(f32 dt, f32 throttle, bool nos);
    void UpdateTireForces(f32 dt, f32 steer, f32 brake, bool handbrake);
    void IntegrateRigidBody(f32 dt);
};
