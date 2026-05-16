cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0091 NEW)  # CMAKE_MSVC_RUNTIME_LIBRARY 활성화

set(PROJECT_NAME Path_Tracing)
project(${PROJECT_NAME})

set(CMAKE_CXX_STANDARD 17)

# [중요] 모든 라이브러리를 Debug용(MDd)으로 통일
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL")

# 1. 경로 설정
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shader")
set(MODEL_SRC_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/model")
set(OUTPUT_BASE_DIR "${CMAKE_BINARY_DIR}")
set(OUTPUT_SHADER_DIR "${OUTPUT_BASE_DIR}/shader")
set(OUTPUT_MODEL_DIR  "${OUTPUT_BASE_DIR}/model")
set(HDRI_SRC_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/hdri")
set(OUTPUT_HDRI_DIR "${OUTPUT_BASE_DIR}/hdri")

# 2. 파일 스캔
file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
    "${SHADER_SRC_DIR}/*.hlsl"
    "${SHADER_SRC_DIR}/*.hlsli"
)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# 3. 셰이더 개별 복사 명령 생성
set(ALL_DEST_SHADERS "")
foreach(SHADER_FILE ${SHADER_SOURCES})
    get_filename_component(FILE_NAME ${SHADER_FILE} NAME)
    set(DEST_FILE "${OUTPUT_SHADER_DIR}/${FILE_NAME}")

    add_custom_command(
        OUTPUT "${DEST_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SHADER_FILE}" "${DEST_FILE}"
        DEPENDS "${SHADER_FILE}"
        COMMENT "Updating shader: ${FILE_NAME}"
        VERBATIM
    )
    list(APPEND ALL_DEST_SHADERS "${DEST_FILE}")
endforeach()

# 4. 자산 업데이트 타겟
add_custom_target(UpdateAssets
    DEPENDS ${ALL_DEST_SHADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_MODEL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_HDRI_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${MODEL_SRC_DIR}"  "${OUTPUT_MODEL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${HDRI_SRC_DIR}"   "${OUTPUT_HDRI_DIR}"
    COMMENT "Syncing shaders, models, and HDRIs..."
)

# 5. 실행 파일 생성
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/buffer.h           src/buffer.cpp
    src/common.h
    src/context.h          src/context.cpp
    src/compute_program.h  src/compute_program.cpp
    src/shader.h           src/shader.cpp
    src/image.h            src/image.cpp
    src/texture.h          src/texture.cpp
    src/mesh.h             src/mesh.cpp
    src/model.h            src/model.cpp
    src/gltf_loader.h      src/gltf_loader.cpp
    src/bvh.h              src/bvh.cpp
    src/scene_desc.h       src/scene_desc.cpp
    src/EnvMap.h           src/EnvMap.cpp
    src/EnergyLUT.h    src/EnergyLUT.cpp
    ${SHADER_SOURCES}
)

add_dependencies(${PROJECT_NAME} UpdateAssets)
set_source_files_properties(${SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

# 6. 라이브러리 및 설정 (Dependency.cmake 포함)
include(Dependency.cmake)
target_include_directories(${PROJECT_NAME} PUBLIC ${DEP_INCLUDE_DIR})
target_link_directories(${PROJECT_NAME}    PUBLIC ${DEP_LIB_DIR})

target_compile_definitions(${PROJECT_NAME} PUBLIC
    WINDOW_NAME=\"Path_Tracing\"
    WINDOW_WIDTH=960
    WINDOW_HEIGHT=540
    _ITERATOR_DEBUG_LEVEL=2  # Assimp Debug 빌드와 일치
)

set(DX_LIBS d3d11.lib dxgi.lib d3dcompiler.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE ${DEP_LIBS} ${DX_LIBS})
add_dependencies(${PROJECT_NAME} ${DEP_LIST})