#include "ea_chunks.h"
#include "renderer.h"
#include "vehicle_physics.h"
#include "police_ai.h"
#include "world_streamer.h"
#include <iostream>
#include <iomanip>
#include <fstream>

void RunFullGameSimulation() {
    std::cout << "==========================================================\n";
    std::cout << "  NFS: Most Wanted (PS2 -> Android) - OPCAO 2: JOGO COMPLETO\n";
    std::cout << "==========================================================\n\n";

    // 1. Inicializando Sistema de Streaming de Rockport (Pistas)
    WorldStreamer world;
    world.Init("ZDIR.BIN");

    // 2. Configurando Física Real da BMW M3 GTR
    VehicleSimulation car;
    VehicleSpecs m3Specs;
    m3Specs.mass = 1350.0f; // 1350 kg
    m3Specs.idleRPM = 1000.0f;
    m3Specs.maxEngineRPM = 8500.0f;
    m3Specs.peakTorque = 480.0f; // 480 Nm
    m3Specs.gearRatios = { -3.2f, 3.82f, 2.26f, 1.64f, 1.29f, 1.06f, 0.88f };
    m3Specs.finalDriveRatio = 3.64f;
    m3Specs.aeroDragCoeff = 0.32f;
    m3Specs.nosCapacity = 100.0f;
    car.Configure(m3Specs);

    // 3. Inicializando Sistema de Perseguição Policial (IA)
    PursuitSystem pursuit;
    pursuit.SetHeatLevel(HEAT_3_STATE_GTO); // Inicia em Heat 3 (Pontiac GTOs + Bloqueios)

    // 4. Inicializando Renderizador Android (GLES3/Vulkan)
    AndroidRenderer renderer;
    renderer.Init(1920, 1080);
    renderer.LoadShaders("shaders/car_paint.vert", "shaders/car_paint.frag");

    std::cout << "\n>>> INICIANDO SIMULACAO DO GAMEPLAY (20 TICKS DE FISICA E IA) <<<\n";
    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << " Tick |  Velocidade  | Marcha |   RPM   |   Nitro   | Policiais | Busted % | Evade %\n";
    std::cout << "------------------------------------------------------------------------------------\n";

    f32 dt = 0.05f; // 50ms por tick (20 FPS de amostragem)
    for (int tick = 1; tick <= 20; ++tick) {
        // Simulação de entrada do jogador: acelerador a 100% e Nitro ativado no tick 10
        f32 throttle = 1.0f;
        f32 steer = (tick > 5 && tick < 10) ? 0.3f : 0.0f; // Faz uma curva
        bool nos = (tick >= 10);

        // Atualiza a física do carro
        car.Update(dt, throttle, 0.0f, steer, false, nos);

        // Atualiza o streaming do mapa conforme o carro se desloca por Rockport
        world.Update(car.GetPosition());

        // Atualiza a IA policial perseguindo o carro
        pursuit.Update(dt, car.GetPosition(), car.GetVelocity());

        std::cout << std::setw(5) << tick << " | "
                  << std::fixed << std::setprecision(1) << std::setw(8) << car.GetSpeedKmh() << " km/h | "
                  << std::setw(6) << car.GetCurrentGear() << " | "
                  << std::setw(7) << static_cast<int>(car.GetRPM()) << " | "
                  << std::setw(7) << car.GetNosRemaining() << "% | "
                  << std::setw(9) << pursuit.GetActiveCopsCount() << " | "
                  << std::setw(7) << pursuit.GetBustedMeter() << "% | "
                  << std::setw(6) << pursuit.GetEvadeMeter() << "%\n";
    }

    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << "\n[OK] Simulacao completa de Pistas, Fisica e IA Policial executada com sucesso!\n";
}

int main() {
    RunFullGameSimulation();
    return 0;
}
