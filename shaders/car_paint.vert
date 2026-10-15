#version 300 es
// Shaders para Android (OpenGL ES 3.0 / Vulkan SPIR-V)
// Renderizador de Veículos para o Port de NFS Most Wanted

precision highp float;

// Atributos de Vértice (Extraídos diretamente dos Chunks 0x00134011 da EAGL4)
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

// Uniforms de Transformação da Câmera e Mundo
uniform mat4 u_ModelMatrix;
uniform mat4 u_ViewMatrix;
uniform mat4 u_ProjectionMatrix;
uniform mat3 u_NormalMatrix;
uniform vec3 u_CameraPosition;

// Saídas para o Fragment Shader
out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_ViewDir;

void main() {
    vec4 worldPos = u_ModelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal = normalize(u_NormalMatrix * a_Normal);
    v_TexCoord = a_TexCoord;
    v_ViewDir = normalize(u_CameraPosition - worldPos.xyz);

    gl_Position = u_ProjectionMatrix * u_ViewMatrix * worldPos;
}
