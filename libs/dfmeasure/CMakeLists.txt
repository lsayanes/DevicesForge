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
)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    list(APPEND DFMEASURE_SOURCES ai/ONNXInference.cpp ai/ONNXInference.h)
endif()

add_library(dfmeasure STATIC ${DFMEASURE_SOURCES})

target_include_directories(dfmeasure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dfmeasure PUBLIC dfcore)
df_library_defaults(dfmeasure)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    target_compile_definitions(dfmeasure PUBLIC HAS_ONNX_RUNTIME)
    target_include_directories(dfmeasure PUBLIC ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(dfmeasure PUBLIC ${ONNXRUNTIME_LIB})
endif()
