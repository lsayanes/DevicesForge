# ============================================================================
# VirtualDevices - Plugin publicable (VST3)
# ============================================================================
# Carga una captura (.dfcapture o WAV) y la reproduce por convolucion.
# El editor es VSTGUI con resource/editor.uidesc; las imagenes las arma
# resource/make_ui_assets.py desde img/VU_Meter.png.
# ============================================================================

set(EMULATOR_SOURCES
    EmulatorController.cpp
    EmulatorFactory.cpp
    EmulatorProcessor.cpp
    EmulatorViews.cpp
)

set(EMULATOR_HEADERS
    EmulatorController.h
    EmulatorIDs.h
    EmulatorProcessor.h
    EmulatorState.h
    EmulatorViews.h
    VuFace.h
    version.h
)

set(EMULATOR_RESOURCES
    resource/editor.uidesc
    resource/panel.png
    resource/panel_2.0x.png
    resource/vu_face.png
    resource/vu_face_2.0x.png
    resource/vu_cap.png
    resource/vu_cap_2.0x.png
    resource/knob.png
    resource/knob_2.0x.png
)

# El target de CMake queda DevicesForgeEmulator; el bundle es VirtualDevices.vst3.
smtg_add_vst3plugin(DevicesForgeEmulator
    PACKAGE_NAME "VirtualDevices"
    SOURCES_LIST ${EMULATOR_SOURCES} ${EMULATOR_HEADERS}
)

smtg_target_add_plugin_resources(DevicesForgeEmulator
    RESOURCES
        ${EMULATOR_RESOURCES}
)

target_include_directories(DevicesForgeEmulator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DevicesForgeEmulator
    PRIVATE
        sdk
        vstgui_support
        dfcore
        dfvst
)

# ============================================================================
# Platform Settings
# ============================================================================
if(SMTG_MAC)
    smtg_target_set_bundle(DevicesForgeEmulator
        BUNDLE_IDENTIFIER com.devicesforge.virtualdevices
        COMPANY_NAME "DevicesForge"
    )
    smtg_target_set_debug_executable(DevicesForgeEmulator
        "/Applications/VST3PluginTestHost.app"
        "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
elseif(SMTG_WIN)
    if(MSVC)
        smtg_target_set_debug_executable(DevicesForgeEmulator
            "$(ProgramW6432)/Steinberg/VST3PluginTestHost/VST3PluginTestHost.exe"
            "--pluginfolder \"$(OutDir)/\""
        )
    endif()
endif()

# ============================================================================
# Compiler Options
# ============================================================================
target_compile_definitions(DevicesForgeEmulator
    PRIVATE
        $<$<CONFIG:Debug>:DEBUG=1>
        $<$<CONFIG:Release>:NDEBUG=1>
)

if(MSVC)
    target_compile_options(DevicesForgeEmulator PRIVATE /W4)
else()
    target_compile_options(DevicesForgeEmulator PRIVATE -Wall -Wextra -Wpedantic)
endif()
