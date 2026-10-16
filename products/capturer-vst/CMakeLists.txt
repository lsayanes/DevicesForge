# ============================================================================
# DevicesForge - Plugin de captura (VST3, uso interno)
# ============================================================================
# El DSP viene de libs/ (dfcore, dfmeasure) y los dialogos de libs/dfvst.
# El target conserva el nombre DevicesForge: el bundle sigue siendo
# DevicesForge.vst3 y los proyectos existentes lo encuentran igual.
# ============================================================================

set(PLUGIN_SOURCES
    PluginProcessor.cpp
    PluginFactory.cpp
    PluginEditor.cpp
)

set(PLUGIN_HEADERS
    DevicesForge.h
    PluginProcessor.h
    PluginCIDs.h
    PluginEditor.h
    version.h
)

smtg_add_vst3plugin(DevicesForge
    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}
)

target_include_directories(DevicesForge
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DevicesForge
    PRIVATE
        sdk
        vstgui_support
        dfcore
        dfmeasure
        dfvst
)

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
        set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT DevicesForge)
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
