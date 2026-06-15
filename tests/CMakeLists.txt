# Catch2: prefer a system install, otherwise fetch it.
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found on the system; fetching it with FetchContent.")
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
endif()

set(TEST_SOURCES
        test_backupmanager.cpp
        test_bg3deployer.cpp
        test_cryptography.cpp
        test_deployer.cpp
        test_fomodinstaller.cpp
        test_installer.cpp
        test_moddedapplication.cpp
        test_reversedeployer.cpp
        test_tagconditionnode.cpp
        test_tool.cpp
        test_utils.cpp
        test_utils.h
        matcher.cpp
        matcher.h
        tests.cpp
)

# The LOOT and OpenMW deployer tests include headers and link symbols that only
# exist in `core` when LIMO_WITH_LOOT=ON (see the top-level CMakeLists.txt).
# Only compile them when that support is built, so the test target links cleanly
# with LOOT off.
if(LIMO_WITH_LOOT)
    list(APPEND TEST_SOURCES
            test_lootdeployer.cpp
            test_openmwdeployer.cpp
    )
endif()

configure_file(test_utils.h.in test_utils.h)

add_executable(tests ${TEST_SOURCES})
target_include_directories(tests
    PRIVATE core
)
target_link_libraries(tests
    PRIVATE Catch2::Catch2WithMain
    PRIVATE core
)

include(Catch)
catch_discover_tests(tests)
