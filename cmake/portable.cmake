add_library(bo1_fastfile_format STATIC src/database/db_fastfile_format.cpp)
target_include_directories(bo1_fastfile_format PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(bo1_fastfile_format PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
add_library(bo1_narrow32 STATIC src/platform/narrow32_portable.cpp)
target_include_directories(bo1_narrow32 PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(bo1_narrow32 PUBLIC cxx_std_17)
target_link_libraries(bo1_narrow32 PUBLIC Threads::Threads)

add_library(bo1_steam_offline STATIC src/platform/steam_offline.cpp)
target_include_directories(bo1_steam_offline PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(bo1_steam_offline PUBLIC cxx_std_17)
target_compile_definitions(bo1_steam_offline PUBLIC STEAM_API_NODLL)

option(BO1_BUILD_FASTFILE_INSPECTOR "Build the host fastfile inspector" ON)
if (BO1_BUILD_FASTFILE_INSPECTOR)
    # Reuse the same inflater that the Windows engine currently builds.
    add_library(bo1_inspect_zlib STATIC
        src/zlib/adler32.c
        src/zlib/crc32.c
        src/zlib/inffast.c
        src/zlib/inflate.c
        src/zlib/inftrees.c
        src/zlib/zutil.c
    )
    target_include_directories(bo1_inspect_zlib PUBLIC "${PROJECT_SOURCE_DIR}/src/zlib")
    add_executable(bo1_fastfile_inspect tools/fastfile_inspect.cpp)
    target_link_libraries(bo1_fastfile_inspect PRIVATE bo1_fastfile_format bo1_inspect_zlib)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(bo1_narrow32_tests tests/narrow32_tests.cpp)
    target_link_libraries(bo1_narrow32_tests PRIVATE bo1_narrow32)
    add_test(NAME narrow32 COMMAND bo1_narrow32_tests)

    add_executable(bo1_steam_offline_tests tests/steam_offline_tests.cpp)
    target_link_libraries(bo1_steam_offline_tests PRIVATE bo1_steam_offline)
    add_test(NAME steam_offline COMMAND bo1_steam_offline_tests)

    add_executable(bo1_fastfile_format_tests tests/fastfile_format_tests.cpp)
    target_link_libraries(bo1_fastfile_format_tests PRIVATE bo1_fastfile_format)
    add_test(NAME fastfile_format COMMAND bo1_fastfile_format_tests)

    if (BO1_BUILD_FASTFILE_INSPECTOR)
        find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
        add_test(NAME fastfile_inspect
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/fastfile_inspect_tests.py"
                "$<TARGET_FILE:bo1_fastfile_inspect>")
    endif()
endif()
