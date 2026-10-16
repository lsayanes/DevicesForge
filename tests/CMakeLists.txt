# ============================================================================
# Tests Configuration
# ============================================================================

# Find Google Test
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# ============================================================================
# Test Executable
# ============================================================================
add_executable(DevicesForgeTests
    test_DSP.cpp
)

target_link_libraries(DevicesForgeTests
    PRIVATE
        dfcore
        dfmeasure
        GTest::gtest_main
)

# ============================================================================
# Register Tests
# ============================================================================
include(GoogleTest)
gtest_discover_tests(DevicesForgeTests)
