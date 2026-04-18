include(ExternalProject)
set(DEP_INSTALL_DIR ${PROJECT_BINARY_DIR}/install)
set(DEP_INCLUDE_DIR ${DEP_INSTALL_DIR}/include)
set(DEP_LIB_DIR ${DEP_INSTALL_DIR}/lib)
set(DEP_NRD_INCLUDE_DIRS "")
set(DEP_NRD_LIB_DIRS "")

set(NRD_LOCAL_SOURCE_DIR "${PROJECT_BINARY_DIR}/dep_nrd-prefix/src/dep_nrd")
set(NRD_LOCAL_HEADER_DIR "${NRD_LOCAL_SOURCE_DIR}/Include")
set(NRD_LOCAL_INTEGRATION_DIR "${NRD_LOCAL_SOURCE_DIR}/Integration")
set(NRD_LOCAL_SHADER_INCLUDE_DIR "${NRD_LOCAL_SOURCE_DIR}/Shaders/Include")
set(NRD_LOCAL_LIB_DIR "${NRD_LOCAL_SOURCE_DIR}/_Bin/Debug")
set(NRD_LOCAL_HEADER "${NRD_LOCAL_HEADER_DIR}/NRD.h")

# 1. spdlog
ExternalProject_Add(
    dep_spdlog
    GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
    GIT_TAG "v1.x"
    GIT_SHALLOW 1
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
        # MultiThreadedDLL -> MultiThreadedDebugDLL로 수정
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL 
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_BUILD_TESTS=OFF
    # --config Release -> --config Debug로 수정
    BUILD_COMMAND   ${CMAKE_COMMAND} --build <BINARY_DIR> --config Debug
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Debug --target install
)
set(DEP_LIST ${DEP_LIST} dep_spdlog)

# 2. stb
ExternalProject_Add(
    dep_stb
    GIT_REPOSITORY "https://github.com/nothings/stb"
    GIT_TAG "master"
    GIT_SHALLOW 1
    UPDATE_DISCONNECTED 1  # [추가]
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${DEP_INSTALL_DIR}/include/stb
            COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_BINARY_DIR}/dep_stb-prefix/src/dep_stb/stb_image.h
            ${DEP_INSTALL_DIR}/include/stb/stb_image.h
            COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_BINARY_DIR}/dep_stb-prefix/src/dep_stb/stb_image_write.h
            ${DEP_INSTALL_DIR}/include/stb/stb_image_write.h
)
set(DEP_LIST ${DEP_LIST} dep_stb)

# 3. GLM
ExternalProject_Add(
    dep_glm
    GIT_REPOSITORY "https://github.com/g-truc/glm"
    GIT_TAG "1.0.1"
    GIT_SHALLOW 1
    UPDATE_DISCONNECTED 1  # [추가]
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${DEP_INSTALL_DIR}/include/glm
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_BINARY_DIR}/dep_glm-prefix/src/dep_glm/glm
            ${DEP_INSTALL_DIR}/include/glm
)
set(DEP_LIST ${DEP_LIST} dep_glm)

# 4. Assimp
ExternalProject_Add(
    dep_assimp
    GIT_REPOSITORY "https://github.com/assimp/assimp.git"
    GIT_TAG "v5.3.1"
    GIT_SHALLOW 1
    UPDATE_DISCONNECTED 1  # [추가]
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
        -DASSIMP_BUILD_SAMPLES=OFF
        -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF
        -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF
        -DASSIMP_BUILD_ZLIB=ON
        -DASSIMP_BUILD_OBJ_IMPORTER=ON
        -DASSIMP_BUILD_FBX_IMPORTER=ON
        -DASSIMP_INJECT_DEBUG_POSTFIX=OFF
        -DBUILD_SHARED_LIBS=OFF
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        # [FIX] static(lib) + Debug 조합에서 존재하지 않는 링커 PDB를 install 하려다
        #       cmake_install.cmake:148 에서 실패하는 assimp 5.3.1 버그 회피
        -DASSIMP_INSTALL_PDB=OFF
    # VS 멀티-config: 외부 빌드 구성 무시, Release 고정
    BUILD_COMMAND   ${CMAKE_COMMAND} --build <BINARY_DIR> --config Debug
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Debug --target install
)
set(DEP_LIBS ${DEP_LIBS} assimp-vc145-mt zlibstaticd)
set(DEP_LIST ${DEP_LIST} dep_assimp)

# 5. tinygltf (헤더 전용)
ExternalProject_Add(
    dep_tinygltf
    GIT_REPOSITORY "https://github.com/syoyo/tinygltf.git"
    GIT_TAG        "v2.8.21"
    GIT_SHALLOW    1
    UPDATE_DISCONNECTED 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    TEST_COMMAND      ""
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E copy
        ${PROJECT_BINARY_DIR}/dep_tinygltf-prefix/src/dep_tinygltf/tiny_gltf.h
        ${DEP_INSTALL_DIR}/include/tiny_gltf.h
        COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_BINARY_DIR}/dep_tinygltf-prefix/src/dep_tinygltf/json.hpp
        ${DEP_INSTALL_DIR}/include/json.hpp
)
set(DEP_LIST ${DEP_LIST} dep_tinygltf)

# 6. NRD (NVIDIA Real-time Denoisers)
# 오프라인 워크플로를 깨지 않기 위해, 로컬에 NRD 소스/설치가 없으면 자동 비활성화한다.
if(PT_ENABLE_NRD AND NOT EXISTS "${NRD_LOCAL_SOURCE_DIR}/CMakeLists.txt" AND NOT EXISTS "${NRD_LOCAL_HEADER}")
    message(STATUS "PT_ENABLE_NRD=ON but no local NRD source/install was found. Disabling NRD for this configure step.")
    set(PT_ENABLE_NRD OFF)
endif()

if(PT_ENABLE_NRD)
    ExternalProject_Add(
        dep_nrd
        GIT_REPOSITORY "https://github.com/NVIDIAGameWorks/RayTracingDenoiser.git"
        GIT_TAG "v4.14.3"
        GIT_SHALLOW 1
        UPDATE_DISCONNECTED 1
        # Patch: NVIDIA-RTX/ShaderMake (main) dropped --useAPI and changed FXC variable name.
        # cmake/patch_nrd.cmake removes --useAPI and fixes ${FXC_PATH} → ${SHADERMAKE_FXC_PATH}.
        PATCH_COMMAND ${CMAKE_COMMAND}
            "-DNRD_SRC=<SOURCE_DIR>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_nrd.cmake"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
            -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL
            -DNRD_STATIC_LIBRARY=ON
            -DNRD_EMBEDS_DXBC_SHADERS=ON
            -DNRD_EMBEDS_DXIL_SHADERS=OFF
            -DNRD_EMBEDS_SPIRV_SHADERS=OFF
        BUILD_COMMAND   ${CMAKE_COMMAND} --build <BINARY_DIR> --config Debug
        INSTALL_COMMAND ""
    )
    set(DEP_LIBS ${DEP_LIBS} NRD)
    set(DEP_LIST ${DEP_LIST} dep_nrd)
    set(DEP_NRD_INCLUDE_DIRS ${NRD_LOCAL_HEADER_DIR} ${NRD_LOCAL_INTEGRATION_DIR} ${NRD_LOCAL_SHADER_INCLUDE_DIR})
    set(DEP_NRD_LIB_DIRS ${NRD_LOCAL_LIB_DIR})
endif()

add_dependencies(${PROJECT_NAME} ${DEP_LIST})
