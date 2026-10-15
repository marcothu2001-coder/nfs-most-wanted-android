#pragma once
#include "nfs_types.h"
#include <vector>
#include <string>

// Hardware Abstraction Layer (HAL)
// Abstrai as peculiaridades do PS2 (EE, VU0, GS, IOP) para rodar nativamente no Android / Desktop.
class IPS2HardwareAbstraction {
public:
    virtual ~IPS2HardwareAbstraction() = default;

    // Inicialização do sistema
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;

    // Gerenciamento de Memória (Substitui FastMem e Heap de PS2)
    virtual void* Alloc(size_t bytes, size_t alignment = 16) = 0;
    virtual void Free(void* ptr) = 0;

    // Temporização (Substitui os registradores de ciclo COP0)
    virtual u64 GetCycleCount() = 0;
    virtual f64 GetDeltaTimeSeconds() = 0;

    // Subsistema de Renderização (Vulkan / OpenGL ES)
    virtual bool InitRenderer(int width, int height) = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void DrawIndexedMesh(const void* vertices, u32 vert_count, const u16* indices, u32 idx_count) = 0;

    // Subsistema de Arquivos (VFS / Streaming assíncrono)
    virtual bool MountVFS(const std::string& zdir_path, const std::string& zzdata_dir) = 0;
    virtual std::vector<u8> ReadFile(const std::string& virtual_path) = 0;
};
