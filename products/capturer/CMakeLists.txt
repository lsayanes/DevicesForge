# Capturador offline. La CLI no linkea el VST. La ventana usa Qt6 si esta instalado.

add_executable(dfcapture main.cpp)

target_link_libraries(dfcapture PRIVATE dfmeasure)

set_target_properties(dfcapture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_features(dfcapture PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(dfcapture PRIVATE /W4)
else()
    target_compile_options(dfcapture PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 no encontrado: no se compila Capturador.app")
    return()
endif()

message(STATUS "Qt6 ${Qt6_VERSION}: Capturador.app")

set(CMAKE_AUTOMOC ON)

qt_add_executable(Capturador
    gui/main.cpp
    gui/MainWindow.cpp
    gui/MainWindow.h
)

target_link_libraries(Capturador PRIVATE Qt6::Widgets dfmeasure)
target_compile_features(Capturador PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(Capturador PRIVATE /W4)
else()
    target_compile_options(Capturador PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
    set_target_properties(Capturador PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "Capturador"
        MACOSX_BUNDLE_BUNDLE_NAME "Capturador"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.devicesforge.capturador"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()
