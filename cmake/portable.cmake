add_library(bo1_fastfile_format STATIC src/database/db_fastfile_format.cpp)
target_include_directories(bo1_fastfile_format PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(bo1_fastfile_format PUBLIC cxx_std_17)

add_library(bo1_bytecode STATIC src/clientscript/cscr_bytecode.cpp)
target_include_directories(bo1_bytecode PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(bo1_bytecode PUBLIC cxx_std_17)

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
# The CLI and the iOS app share the engine's bundled inflater and inspection API.
add_library(bo1_inspect_zlib STATIC
    src/zlib/adler32.c
    src/zlib/crc32.c
    src/zlib/inffast.c
    src/zlib/inflate.c
    src/zlib/inftrees.c
    src/zlib/zutil.c
)
target_include_directories(bo1_inspect_zlib PUBLIC "${PROJECT_SOURCE_DIR}/src/zlib")
add_library(bo1_fastfile_inspection STATIC
    src/database/fastfile_inspection.cpp src/database/fastfile_assets.cpp)
target_compile_features(bo1_fastfile_inspection PUBLIC cxx_std_17)
target_link_libraries(bo1_fastfile_inspection PUBLIC bo1_fastfile_format PRIVATE bo1_inspect_zlib)

if (BO1_BUILD_FASTFILE_INSPECTOR)
    add_executable(bo1_fastfile_inspect tools/fastfile_inspect.cpp)
    target_link_libraries(bo1_fastfile_inspect PRIVATE bo1_fastfile_inspection)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(bo1_bytecode_tests tests/bytecode_tests.cpp)
    target_link_libraries(bo1_bytecode_tests PRIVATE bo1_bytecode)
    add_test(NAME bytecode COMMAND bo1_bytecode_tests)

    add_executable(bo1_narrow32_tests tests/narrow32_tests.cpp)
    target_link_libraries(bo1_narrow32_tests PRIVATE bo1_narrow32)
    add_test(NAME narrow32 COMMAND bo1_narrow32_tests)

    add_executable(bo1_steam_offline_tests tests/steam_offline_tests.cpp)
    target_link_libraries(bo1_steam_offline_tests PRIVATE bo1_steam_offline)
    add_test(NAME steam_offline COMMAND bo1_steam_offline_tests)

    add_executable(bo1_fastfile_format_tests tests/fastfile_format_tests.cpp)
    target_link_libraries(bo1_fastfile_format_tests PRIVATE bo1_fastfile_format)
    add_test(NAME fastfile_format COMMAND bo1_fastfile_format_tests)

    add_executable(bo1_fastfile_inspection_tests tests/fastfile_inspection_tests.cpp)
    target_link_libraries(bo1_fastfile_inspection_tests PRIVATE bo1_fastfile_inspection)
    add_test(NAME fastfile_inspection COMMAND bo1_fastfile_inspection_tests)

    add_executable(bo1_fastfile_assets_tests tests/fastfile_assets_tests.cpp tests/native_asset_fixture.cpp)
    target_link_libraries(bo1_fastfile_assets_tests PRIVATE bo1_fastfile_inspection)
    add_test(NAME fastfile_assets COMMAND bo1_fastfile_assets_tests)

    add_executable(bo1_techset_assets_tests tests/techset_assets_tests.cpp tests/techset_fixture.cpp)
    target_link_libraries(bo1_techset_assets_tests PRIVATE bo1_fastfile_inspection)
    add_test(NAME techset_assets COMMAND bo1_techset_assets_tests)

    add_executable(bo1_temp_stream_tests tests/temp_stream_tests.cpp)
    target_include_directories(bo1_temp_stream_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(bo1_temp_stream_tests PRIVATE cxx_std_17)
    add_test(NAME temp_stream COMMAND bo1_temp_stream_tests)

    add_executable(bo1_xmodel_header_tests tests/xmodel_header_tests.cpp)
    target_include_directories(bo1_xmodel_header_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(bo1_xmodel_header_tests PRIVATE cxx_std_17)
    add_test(NAME xmodel_header COMMAND bo1_xmodel_header_tests)

    add_executable(bo1_xsurface_header_tests tests/xsurface_header_tests.cpp)
    target_include_directories(bo1_xsurface_header_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(bo1_xsurface_header_tests PRIVATE cxx_std_17)
    add_test(NAME xsurface_header COMMAND bo1_xsurface_header_tests)

    add_executable(bo1_bc2_texture_tests tests/bc2_texture_tests.cpp)
    target_include_directories(bo1_bc2_texture_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(bo1_bc2_texture_tests PRIVATE cxx_std_17)
    add_test(NAME bc2_texture COMMAND bo1_bc2_texture_tests)

    add_executable(bo1_iwi_image_tests tests/iwi_image_tests.cpp)
    target_include_directories(bo1_iwi_image_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(bo1_iwi_image_tests PRIVATE cxx_std_17)
    add_test(NAME iwi_image COMMAND bo1_iwi_image_tests)

    add_executable(bo1_model_mesh_tests tests/model_mesh_tests.cpp)
    target_include_directories(bo1_model_mesh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(bo1_model_mesh_tests PRIVATE cxx_std_17)
    add_test(NAME model_mesh COMMAND bo1_model_mesh_tests)

    if (BO1_BUILD_FASTFILE_INSPECTOR)
        find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
        add_test(NAME fastfile_inspect
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/fastfile_inspect_tests.py"
                "$<TARGET_FILE:bo1_fastfile_inspect>")
    endif()
endif()
