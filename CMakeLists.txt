cmake_minimum_required(VERSION 3.15)
project(nfs_mw_android_port CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Código-fonte comum
set(COMMON_SOURCES
    src/vfs.cpp
    src/chunk_loader.cpp
    src/renderer.cpp
    src/vehicle_physics.cpp
    src/police_ai.cpp
    src/world_streamer.cpp
)

# Compilação Android NDK
if(ANDROID)
    message(STATUS "Configurando para compilação nativa Android NDK (${ANDROID_ABI})")
    
    # Inclui o módulo oficial android_native_app_glue
    include_directories(${ANDROID_NDK}/sources/android/native_app_glue)
    
    add_library(nfs_mw_native SHARED 
        ${COMMON_SOURCES}
        src/android_main.cpp
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
    
    target_link_libraries(nfs_mw_native 
        android 
        log 
        EGL 
        GLESv3
    )
else()
    # Compilação Desktop para testes
    add_executable(nfs_port_test 
        ${COMMON_SOURCES}
        src/main.cpp
    )
endif()
