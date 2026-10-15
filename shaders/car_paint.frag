#version 300 es
// Fragment Shader GLSL ES 3.0 / Vulkan SPIR-V
// Simula o pipeline de materiais EAGL4 (Car Paint + Fresnel + Rockport Visual Filter)

precision highp float;

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_ViewDir;

// Texturas e Mapas de Reflexo
uniform sampler2D u_DiffuseTexture; // Vinil / decalques / detalhes
uniform samplerCube u_EnvMap;        // Reflexo do céu e cidade de Rockport

// Propriedades do Carro
uniform vec3 u_CarPaintColor;        // Ex: Prata BMW M3 (0.85, 0.88, 0.95)
uniform float u_Metallic;            // Fator metálico (0.0 a 1.0)
uniform float u_Roughness;           // Rugosidade da pintura
uniform vec3 u_SunDirection;         // Direção da luz solar de Rockport

out vec4 fragColor;

void main() {
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(v_ViewDir);
    vec3 L = normalize(u_SunDirection);
    vec3 H = normalize(L + V);

    // 1. Iluminação Difusa (Lambertiana)
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuseLight = vec3(1.0, 0.95, 0.8) * NdotL; // Luz quente do sol de Rockport
    vec3 ambientLight = vec3(0.25, 0.28, 0.35);       // Luz ambiente azulada

    // 2. Reflexo Fresnel (Schlick Approximation)
    float F0 = mix(0.04, 0.9, u_Metallic);
    float fresnel = F0 + (1.0 - F0) * pow(clamp(1.0 - dot(N, V), 0.0, 1.0), 5.0);

    // 3. Brilho Especular (Blinn-Phong)
    float NdotH = max(dot(N, H), 0.0);
    float specular = pow(NdotH, 64.0) * (1.0 - u_Roughness);

    // 4. Reflexão Ambiente (Environment Cube Map)
    vec3 R = reflect(-V, N);
    vec3 envColor = texture(u_EnvMap, R).rgb * fresnel;

    // 5. Cor Base (Combinação de Pintura Metálica com Textura de Detalhes)
    vec4 texColor = texture(u_DiffuseTexture, v_TexCoord);
    vec3 baseColor = mix(u_CarPaintColor, texColor.rgb, texColor.a);

    // 6. Composição Final
    vec3 finalColor = baseColor * (diffuseLight + ambientLight) 
                    + (vec3(specular) * vec3(1.0, 0.98, 0.9)) 
                    + envColor;

    // 7. Filtro Visual Característico do NFS Most Wanted (Sepia / Warm Contrast Grading)
    vec3 warmGrading = vec3(1.05, 1.02, 0.88);
    finalColor *= warmGrading;

    fragColor = vec4(finalColor, 1.0);
}
