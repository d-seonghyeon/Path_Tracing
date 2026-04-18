# cmake/patch_nrd.cmake
# Patches NRD v4.14.3 CMakeLists.txt to work with NVIDIA-RTX/ShaderMake (main branch):
#   1. Remove --useAPI (flag was removed from the new ShaderMake CLI)
#   2. Replace ${FXC_PATH} with ${SHADERMAKE_FXC_PATH} (NRD bug: wrong variable name)
#
# Invoked by dep_nrd PATCH_COMMAND in Dependency.cmake.
# NRD_SRC is passed via -DNRD_SRC=<SOURCE_DIR>.

if(NOT NRD_SRC)
    message(FATAL_ERROR "patch_nrd.cmake: NRD_SRC not specified.")
endif()

set(FILE "${NRD_SRC}/CMakeLists.txt")
if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "patch_nrd.cmake: ${FILE} not found.")
endif()

file(READ "${FILE}" content)

# Fix 1: remove --useAPI (unknown to current ShaderMake CLI)
string(REPLACE "--useAPI " "" content "${content}")

# Fix 2: FXC_PATH is never set in NRD CMake; ShaderMake exposes it as SHADERMAKE_FXC_PATH
string(REPLACE [[${FXC_PATH}]] [[${SHADERMAKE_FXC_PATH}]] content "${content}")

file(WRITE "${FILE}" "${content}")
message(STATUS "patch_nrd.cmake: patched ${FILE}")
