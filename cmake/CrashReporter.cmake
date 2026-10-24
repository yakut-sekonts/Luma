include(FetchContent)
include(ExternalProject)

# Build only the macOS static library, never the upstream crash demo or tests.
FetchContent_Declare(plcrashreporter
    URL https://codeload.github.com/microsoft/plcrashreporter/tar.gz/0254f941c646b1ed17b243654723d0f071e990d0
    URL_HASH SHA256=7d5153ad5a9feba5988bf8d7a933aa1f1d8e85910665e25e32a3127e9ea0dc1e
    SOURCE_SUBDIR luma-source-only)
FetchContent_MakeAvailable(plcrashreporter)
set(LUMA_CRASH_BUILD "${CMAKE_BINARY_DIR}/crashreporter")
ExternalProject_Add(luma_crashreporter_build
    SOURCE_DIR "${plcrashreporter_SOURCE_DIR}"
    BINARY_DIR "${LUMA_CRASH_BUILD}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND /usr/bin/xcrun xcodebuild
        -project "${plcrashreporter_SOURCE_DIR}/CrashReporter.xcodeproj"
        -target "CrashReporter macOS" -configuration Release -sdk macosx
        "CONFIGURATION_BUILD_DIR=${LUMA_CRASH_BUILD}/products"
        "OBJROOT=${LUMA_CRASH_BUILD}/objects" "SYMROOT=${LUMA_CRASH_BUILD}/symbols"
        "ARCHS=arm64" "ONLY_ACTIVE_ARCH=YES" "MACOSX_DEPLOYMENT_TARGET=14.0"
        "CODE_SIGNING_ALLOWED=NO" "CODE_SIGNING_REQUIRED=NO"
    BUILD_BYPRODUCTS "${LUMA_CRASH_BUILD}/products/libCrashReporter.a"
    INSTALL_COMMAND "")
add_library(luma_crashreporter STATIC IMPORTED)
set_target_properties(luma_crashreporter PROPERTIES
    IMPORTED_LOCATION "${LUMA_CRASH_BUILD}/products/libCrashReporter.a"
    INTERFACE_INCLUDE_DIRECTORIES "${plcrashreporter_SOURCE_DIR}/Source")
add_dependencies(luma_crashreporter luma_crashreporter_build)
