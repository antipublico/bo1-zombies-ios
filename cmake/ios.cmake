if (NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "BO1_BUILD_IOS_APP requires -DCMAKE_SYSTEM_NAME=iOS and an Apple SDK")
endif()

enable_language(OBJCXX)

add_executable(bo1_ios MACOSX_BUNDLE ios/main.mm tests/native_asset_fixture.cpp tests/techset_fixture.cpp)
target_compile_features(bo1_ios PRIVATE cxx_std_17)
target_compile_options(bo1_ios PRIVATE -fobjc-arc)
target_link_libraries(bo1_ios PRIVATE
    bo1_fastfile_inspection bo1_bytecode bo1_narrow32 bo1_steam_offline
    "-framework UIKit" "-framework Foundation" "-framework UniformTypeIdentifiers"
    "-framework CoreGraphics" "-framework SceneKit"
)
set_target_properties(bo1_ios PROPERTIES
    OUTPUT_NAME BO1Zombies
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/ios/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.antipublico.bo1zombies.dev
    MACOSX_BUNDLE_BUNDLE_NAME "BO1 iOS"
    MACOSX_BUNDLE_BUNDLE_VERSION 1
    MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1.0
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.antipublico.bo1zombies.dev
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ENABLE_BITCODE NO
)
