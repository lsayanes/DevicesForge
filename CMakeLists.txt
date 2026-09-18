cmake_minimum_required(VERSION 3.19)
project(DevicesForge VERSION 1.0.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Paths
# ============================================================================
set(VST3SDK_SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/vst3sdk" CACHE PATH "Path to VST3 SDK")
set(ONNXRUNTIME_ROOTDIR "${CMAKE_SOURCE_DIR}/external/onnxruntime" CACHE PATH "Path to ONNX Runtime")

# ============================================================================
# VST3 SDK
# ============================================================================
if(EXISTS "${VST3SDK_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory(${VST3SDK_SOURCE_DIR} ${CMAKE_BINARY_DIR}/vst3sdk)
    smtg_enable_vst3_sdk()
else()
    message(FATAL_ERROR "VST3 SDK not found at ${VST3SDK_SOURCE_DIR}\n"
                        "Run scripts/install-deps.sh first")
endif()

# ============================================================================
# pffft (FFT Library) - FetchContent
# ============================================================================
include(FetchContent)
FetchContent_Declare(pffft
    GIT_REPOSITORY https://github.com/marton78/pffft.git
    GIT_TAG master
)
FetchContent_MakeAvailable(pffft)

# ============================================================================
# ONNX Runtime
# ============================================================================
if(EXISTS "${ONNXRUNTIME_ROOTDIR}/include")
    message(STATUS "ONNX Runtime found at ${ONNXRUNTIME_ROOTDIR}")
    set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOTDIR}/include")
    
    # Find library
    if(APPLE)
        find_library(ONNXRUNTIME_LIB onnxruntime
                     PATHS "${ONNXRUNTIME_ROOTDIR}/lib"
                     NO_DEFAULT_PATH)
    elseif(WIN32)
        find_library(ONNXRUNTIME_LIB onnxruntime
                     PATHS "${ONNXRUNTIME_ROOTDIR}/lib"
                     NO_DEFAULT_PATH)
    endif()
    
    if(ONNXRUNTIME_LIB)
        message(STATUS "ONNX Runtime library: ${ONNXRUNTIME_LIB}")
    else()
        message(WARNING "ONNX Runtime library not found - AI features disabled")
    endif()
else()
    message(WARNING "ONNX Runtime not found at ${ONNXRUNTIME_ROOTDIR}\n"
                    "AI features will be disabled")
    set(ONNXRUNTIME_INCLUDE_DIR "")
    set(ONNXRUNTIME_LIB "")
endif()

# ============================================================================
# Plugin Target
# ============================================================================
add_subdirectory(src)

# ============================================================================
# Test Host
# ============================================================================
option(BUILD_TEST_HOST "Build CLI test host" ON)
if(BUILD_TEST_HOST)
    add_subdirectory(host)
endif()

# ============================================================================
# Tests
# ============================================================================
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
