# ============================================================================
# DevicesForge - Plugin Sources
# ============================================================================

# DSP Sources (can be built as static lib for testing)
set(DSP_SOURCES
    dsp/FFTProcessor.cpp
    dsp/IRManager.cpp
    dsp/DynamicConvolver.cpp
    dsp/SignalGenerator.cpp
    dsp/RingCaptureBuffer.cpp
    dsp/IRPostProcessor.cpp
    dsp/SweepDeconvolver.cpp
    dsp/IRExporter.cpp
)

set(DSP_HEADERS
    dsp/FFTProcessor.h
    dsp/IRManager.h
    dsp/DynamicConvolver.h
    dsp/SignalGenerator.h
    dsp/RingCaptureBuffer.h
    dsp/IRPostProcessor.h
    dsp/SweepDeconvolver.h
    dsp/IRExporter.h
    plugin/DevicesForge.h
)

# Plugin Sources (VST3 specific - native)
set(PLUGIN_SOURCES
    plugin/PluginProcessor.cpp
    plugin/PluginFactory.cpp
)

set(PLUGIN_HEADERS
    plugin/PluginProcessor.h
    plugin/PluginCIDs.h
)

# ============================================================================
# ONNX Runtime (optional)
# ============================================================================
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    list(APPEND DSP_SOURCES ai/ONNXInference.cpp)
    list(APPEND DSP_HEADERS ai/ONNXInference.h)
    add_definitions(-DHAS_ONNX_RUNTIME)
endif()

# ============================================================================
# DSP Static Library (for testing)
# ============================================================================
add_library(DevicesForgeDSP STATIC
    ${DSP_SOURCES}
    ${DSP_HEADERS}
)

target_include_directories(DevicesForgeDSP
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/dsp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/ai
)

target_link_libraries(DevicesForgeDSP
    PUBLIC
        PFFFT::PFFFT
)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    target_include_directories(DevicesForgeDSP PUBLIC ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(DevicesForgeDSP PUBLIC ${ONNXRUNTIME_LIB})
endif()

# ============================================================================
# VST3 Plugin
# ============================================================================
smtg_add_vst3plugin(DevicesForge
    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}
    ${DSP_SOURCES}
    ${DSP_HEADERS}
)

target_include_directories(DevicesForge
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/dsp
        ${CMAKE_CURRENT_SOURCE_DIR}/ai
)

target_link_libraries(DevicesForge
    PRIVATE
        sdk
        PFFFT::PFFFT
)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    target_include_directories(DevicesForge PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(DevicesForge PRIVATE ${ONNXRUNTIME_LIB})
endif()

# ============================================================================
# Platform Settings
# ============================================================================
if(SMTG_MAC)
    smtg_target_set_bundle(DevicesForge
        BUNDLE_IDENTIFIER com.devicesforge.channel
        COMPANY_NAME "DevicesForge"
    )
    smtg_target_set_debug_executable(DevicesForge
        "/Applications/VST3PluginTestHost.app"
        "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
elseif(SMTG_WIN)
    if(MSVC)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT DevicesForge)
        smtg_target_set_debug_executable(DevicesForge
            "$(ProgramW6432)/Steinberg/VST3PluginTestHost/VST3PluginTestHost.exe"
            "--pluginfolder \"$(OutDir)/\""
        )
    endif()
endif()

# ============================================================================
# Compiler Options
# ============================================================================
target_compile_definitions(DevicesForge
    PRIVATE
        $<$<CONFIG:Debug>:DEBUG=1>
        $<$<CONFIG:Release>:NDEBUG=1>
)

if(MSVC)
    target_compile_options(DevicesForge PRIVATE /W4)
else()
    target_compile_options(DevicesForge PRIVATE -Wall -Wextra -Wpedantic)
endif()
