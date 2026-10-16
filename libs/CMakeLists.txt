# ============================================================================
# DevicesForge - Librerias comunes
# ============================================================================
#   dfcore    : FFT, convolucion, IR/WAV I/O, constantes de audio (sin VST)
#   dfmeasure : senales, captura, deconvolucion, post-proceso (sin VST)
#   dfvst     : helpers de plugin (dialogos nativos de archivo)
# ============================================================================

function(df_library_defaults target)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(${target} PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(dfcore)
add_subdirectory(dfmeasure)
add_subdirectory(dfvst)
