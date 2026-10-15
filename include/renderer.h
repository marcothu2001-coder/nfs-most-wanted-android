#pragma once
#include "nfs_types.h"
#include <vector>
#include <string>

// Estrutura de vértice alinhada para GPU móvel (ARM Mali / Qualcomm Adreno)
struct GPUMeshVertex {
    Vector3 position;
    Vector3 normal;
    f32 u, v;
};

// Objeto de renderização de parte do carro
struct GPUMeshPart {
    u32 vao;
    u32 vbo;
    u32 ebo;
    u32 indexCount;
    u32 partHash;
    std::string name;
};

class AndroidRenderer {
public:
    AndroidRenderer() = default;
    ~AndroidRenderer() = default;

    bool Init(int screenWidth, int screenHeight);
    void Shutdown();

    // Compilação de Shaders GLES 3.0 / Vulkan
    bool LoadShaders(const std::string& vertPath, const std::string& fragPath);

    // Upload de geometria extraída da EAGL4 para a GPU
    GPUMeshPart UploadMesh(const std::vector<GPUMeshVertex>& vertices, 
                           const std::vector<u16>& indices, 
                           u32 partHash, 
                           const std::string& partName);

    // Ciclo de Renderização de Frame
    void BeginFrame();
    void SetCamera(const Matrix4x4& view, const Matrix4x4& projection, const Vector3& camPos);
    void DrawPart(const GPUMeshPart& part, const Matrix4x4& modelMatrix, const Vector3& paintColor);
    void EndFrame();

private:
    u32 m_shaderProgram = 0;
    int m_width = 1920;
    int m_height = 1080;
    
    // Uniform Locations
    int m_locModel = -1;
    int m_locView = -1;
    int m_locProj = -1;
    int m_locNormal = -1;
    int m_locCamPos = -1;
    int m_locCarPaint = -1;
    int m_locMetallic = -1;
    int m_locRoughness = -1;
    int m_locSunDir = -1;
};
