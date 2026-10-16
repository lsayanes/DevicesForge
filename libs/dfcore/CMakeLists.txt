add_library(dfcore STATIC
    DFConstants.h
    FFTProcessor.cpp
    FFTProcessor.h
    IRManager.cpp
    IRManager.h
    DynamicConvolver.cpp
    DynamicConvolver.h
    IRExporter.cpp
    IRExporter.h
)

target_include_directories(dfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dfcore PUBLIC PFFFT::PFFFT)
df_library_defaults(dfcore)
