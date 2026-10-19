set(DFMEASURE_SOURCES
    DFMeasureConstants.h
    SignalGenerator.cpp
    SignalGenerator.h
    RingCaptureBuffer.cpp
    RingCaptureBuffer.h
    IRPostProcessor.cpp
    IRPostProcessor.h
    SweepDeconvolver.cpp
    SweepDeconvolver.h
    OfflineCapturer.cpp
    OfflineCapturer.h
)

add_library(dfmeasure STATIC ${DFMEASURE_SOURCES})

target_include_directories(dfmeasure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dfmeasure PUBLIC dfcore)
df_library_defaults(dfmeasure)
