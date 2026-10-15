#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// Definições de tipos compatíveis com MIPS R5900 (PS2) e ARM64 (Android)
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using f32 = float;
using f64 = double;

// Registrador de 128-bit do Emotion Engine do PS2
struct alignas(16) uint128_t {
    u64 lo;
    u64 hi;
};

// Algoritmo de hash bStringHash da EA Black Box
inline u32 bStringHash(const char* str) {
    u32 h = 0xffffffff;
    if (!str) return h;
    while (*str) {
        char c = *str++;
        if (c >= 'a' && c <= 'z') c -= 32; // Uppercase
        h = (h * 33) + static_cast<u8>(c);
    }
    return h;
}

inline u32 bStringHash(const std::string& str) {
    return bStringHash(str.c_str());
}

// Estruturas matemáticas básicas
struct Vector3 {
    f32 x, y, z;
};

struct Vector4 {
    f32 x, y, z, w;
};

struct Matrix4x4 {
    f32 m[4][4];
};
