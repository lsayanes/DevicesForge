# ============================================================================
# DevicesForge - Test Host
# ============================================================================

set(HOST_SOURCES
    main.cpp
    ${VST3SDK_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${VST3SDK_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.h
    ${VST3SDK_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)

if(APPLE)
    list(APPEND HOST_SOURCES
        ${VST3SDK_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
    )
elseif(WIN32)
    list(APPEND HOST_SOURCES
        ${VST3SDK_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
    )
endif()

add_executable(DevicesForgeHost ${HOST_SOURCES})

if(APPLE)
    set_source_files_properties(
        "${VST3SDK_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm" PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
endif()

target_include_directories(DevicesForgeHost
    PRIVATE
        ${VST3SDK_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(DevicesForgeHost
    PRIVATE
        sdk_hosting
)

# Platform settings
if(APPLE)
    set_target_properties(DevicesForgeHost PROPERTIES
        MACOSX_BUNDLE FALSE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    find_library(COCOA_FRAMEWORK Cocoa)
    target_link_libraries(DevicesForgeHost PRIVATE ${COCOA_FRAMEWORK})
elseif(WIN32)
    set_target_properties(DevicesForgeHost PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

target_compile_features(DevicesForgeHost PRIVATE cxx_std_17)

# Compiler options
if(MSVC)
    target_compile_options(DevicesForgeHost PRIVATE /W4)
else()
    target_compile_options(DevicesForgeHost PRIVATE -Wall -Wextra)
endif()
