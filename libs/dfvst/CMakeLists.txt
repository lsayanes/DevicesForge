if(APPLE)
    set(DFVST_PLATFORM_SOURCES FileDialog.mm)
    set_source_files_properties(FileDialog.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    set(DFVST_PLATFORM_SOURCES FileDialog.cpp)
endif()

add_library(dfvst STATIC
    FileDialog.h
    ${DFVST_PLATFORM_SOURCES}
)

target_include_directories(dfvst PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dfvst PUBLIC dfcore)
df_library_defaults(dfvst)

if(WIN32)
    target_link_libraries(dfvst PUBLIC comdlg32)
endif()

if(APPLE)
    find_library(APPKIT_FRAMEWORK AppKit)
    if(APPKIT_FRAMEWORK)
        target_link_libraries(dfvst PUBLIC ${APPKIT_FRAMEWORK})
    endif()
endif()
