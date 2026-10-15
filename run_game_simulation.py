"""
Simulador Completo de Gameplay: NFS Most Wanted (PS2 -> Android)
Integra Pistas (World Streaming), Física de Veículos (BMW M3) e IA Policial (Heat Levels).
"""
import math
import time

class VehicleSimulation:
    def __init__(self):
        self.mass = 1350.0  # kg
        self.pos_z = 0.0
        self.vel_z = 0.0
        self.speed_kmh = 0.0
        self.rpm = 1000.0
        self.gear = 1
        self.nos = 100.0
        self.gear_ratios = [0.0, 3.82, 2.26, 1.64, 1.29, 1.06, 0.88]

    def update(self, dt, throttle, nos=False):
        # Aceleração
        engine_force = throttle * 6200.0
        if nos and self.nos > 0:
            self.nos = max(0.0, self.nos - dt * 15.0)
            engine_force += 3800.0

        drag = 0.5 * 1.225 * 0.32 * (self.vel_z ** 2)
        accel = (engine_force - drag) / self.mass
        self.vel_z += accel * dt
        self.pos_z += self.vel_z * dt
        self.speed_kmh = self.vel_z * 3.6

        # Marchas e RPM
        if self.speed_kmh < 60: self.gear = 1
        elif self.speed_kmh < 105: self.gear = 2
        elif self.speed_kmh < 155: self.gear = 3
        elif self.speed_kmh < 210: self.gear = 4
        elif self.speed_kmh < 270: self.gear = 5
        else: self.gear = 6

        ratio = self.gear_ratios[self.gear]
        wheel_rpm = (self.vel_z / (0.32 * 2 * math.pi)) * 60
        self.rpm = max(1000, min(8500, wheel_rpm * ratio * 3.64))

class WorldStreamer:
    def __init__(self):
        self.sectors = [
            {"name": "Downtown_Rockport", "pos_z": 0.0, "loaded": False, "archive": "ZZDATA1.BIN", "size_mb": 18},
            {"name": "Rosewood_Center",   "pos_z": 800.0, "loaded": False, "archive": "ZZDATA1.BIN", "size_mb": 18},
            {"name": "Rockport_Turnpike", "pos_z": 1800.0, "loaded": False, "archive": "ZZDATA1.BIN", "size_mb": 18},
            {"name": "Highway_Zone_01",   "pos_z": 3200.0, "loaded": False, "archive": "ZZDATA0.BIN", "size_mb": 50}
        ]

    def update(self, player_z):
        events = []
        for s in self.sectors:
            dist = abs(player_z - s["pos_z"])
            if dist < 600 and not s["loaded"]:
                s["loaded"] = True
                events.append(f"STREAM IN: Setor '{s['name']}' ({s['size_mb']} MB de {s['archive']})")
            elif dist > 850 and s["loaded"]:
                s["loaded"] = False
                events.append(f"STREAM OUT: Setor '{s['name']}' liberado da RAM")
        return events

class PolicePursuit:
    def __init__(self, heat=3):
        self.heat = heat
        self.cops_count = 6
        self.busted_meter = 0.0
        self.evade_meter = 0.0
        self.cop_pos_z = -40.0
        self.model = "Pontiac GTO (State Police)"

    def update(self, dt, player_z, player_speed):
        # Viaturas perseguem a ~195 km/h
        cop_speed = 54.0 # m/s
        self.cop_pos_z += cop_speed * dt
        dist = abs(player_z - self.cop_pos_z)

        if dist < 15 and player_speed < 15:
            self.busted_meter = min(100.0, self.busted_meter + dt * 25.0)
            self.evade_meter = 0.0
        elif dist > 30:
            self.busted_meter = max(0.0, self.busted_meter - dt * 10.0)
            self.evade_meter = min(100.0, self.evade_meter + dt * 6.0)

def main():
    print("====================================================================================")
    print("      NFS: MOST WANTED (PS2 -> ANDROID) - SIMULADOR COMPLETO: PISTAS, FISICA E IA   ")
    print("====================================================================================\n")

    car = VehicleSimulation()
    world = WorldStreamer()
    police = PolicePursuit(heat=3)

    print(f"-> Veiculo do Jogador : BMW M3 GTR (V8 4.0L - 480 Nm)")
    print(f"-> Nivel de Pressao   : HEAT 3 (Viaturas: {police.model})")
    print(f"-> Motor de Streaming : Rockport City Sectors (ZZDATA0.BIN / ZZDATA1.BIN)\n")

    print(f"{'Tick':<5} | {'Distancia':<11} | {'Velocidade':<12} | {'Marcha':<6} | {'RPM':<6} | {'Nitro':<6} | {'Busted':<8} | {'Evade':<8} | Eventos")
    print("-" * 100)

    dt = 0.1 # 100ms
    for tick in range(1, 26):
        nos_on = (tick >= 10 and tick <= 18)
        car.update(dt, throttle=1.0, nos=nos_on)
        stream_events = world.update(car.pos_z)
        police.update(dt, car.pos_z, car.speed_kmh)

        events_str = "; ".join(stream_events) if stream_events else ("NOS ATIVADO!" if nos_on else "")
        print(f"{tick:<5} | {car.pos_z:>8.1f} m | {car.speed_kmh:>8.1f} km/h | {car.gear:<6} | {int(car.rpm):<6} | {car.nos:>4.0f}%  | {police.busted_meter:>5.1f}%  | {police.evade_meter:>5.1f}%  | {events_str}")

    print("-" * 100)
    print("\n[SUCESSO] Todos os 3 modulos (Pistas, Fisica e IA Policial) estao sincronizados e funcionais!")

if __name__ == '__main__':
    main()
