cmake_minimum_required(VERSION 3.25)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum supported macOS" FORCE)
endif()
project(Apertone VERSION 1.6.0 LANGUAGES C CXX OBJCXX)

if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    message(FATAL_ERROR "Apertone currently supports macOS Apple Silicon only.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum supported macOS")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(EXISTS "${CMAKE_SOURCE_DIR}/.tools/qt/6.10.3/macos")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/.tools/qt/6.10.3/macos")
endif()
include(cmake/Dependencies.cmake)
include(cmake/Sparkle.cmake)
include(cmake/CrashReporter.cmake)

add_library(luma_core STATIC
    core/ai/Culling.cpp core/catalog/IntelligentLibrary.cpp
    core/io/StagingFile.cpp
    core/library/ImportOptions.cpp core/catalog/ImportPlans.cpp core/library/SmartCollection.cpp core/catalog/SmartCollections.cpp
    core/render/CreativeColor.cpp core/render/Presence.cpp core/render/AutoAdjust.cpp core/render/Enhance.cpp core/quick/QuickLooks.cpp core/quick/MatchLook.cpp
    core/edits/CreativeSettings.cpp core/catalog/CreativeStorage.cpp
    core/edits/EditSnapshot.cpp core/edits/EditHistory.cpp core/render/ImageHistogram.cpp core/color/WhiteBalance.cpp core/render/WhiteBalanceSampler.cpp core/catalog/EditVersions.cpp
    core/merge/HdrMerge.cpp core/merge/PanoramaMerge.cpp
    core/catalog/SemanticCatalog.cpp
    core/catalog/CatalogBackup.cpp core/catalog/DerivedCatalog.cpp core/catalog/MetadataCatalog.cpp core/catalog/RelinkCatalog.cpp core/metadata/DescriptiveMetadata.cpp
    core/edits/Retouch.cpp core/edits/BitmapMask.cpp core/render/RetouchPass.cpp core/catalog/RetouchStorage.cpp
    core/catalog/BitmapMaskMigration.cpp
    core/edits/LocalMasks.cpp core/render/MaskCoverage.cpp core/render/MaskReference.cpp core/render/LocalAdjustments.cpp core/render/LocalBlur.cpp core/catalog/LocalMaskStorage.cpp core/catalog/MaskComponentMigration.cpp
    "${CMAKE_BINARY_DIR}/generated/LensDatabase.h"
    core/math/Homography.cpp core/edits/TransformSettings.cpp core/render/TransformMapping.cpp core/render/PerspectiveCorrection.cpp
    core/lens/LensProfiles.cpp core/lens/LensCorrection.cpp core/edits/LensSettings.cpp
    core/catalog/TransformMigration.cpp core/catalog/LensMigration.cpp core/catalog/CropMigration.cpp core/catalog/NoiseReductionMigration.cpp core/catalog/SharpeningMigration.cpp core/catalog/BatchEdits.cpp core/library/LibraryTypes.cpp core/catalog/LibraryCatalog.cpp core/catalog/Catalog.cpp core/raw/RawDecoder.cpp core/image/Bayer.cpp
    core/presets/SettingsSubset.cpp core/render/NoiseReduction.cpp core/render/Sharpening.cpp core/render/CropGeometry.cpp core/render/CpuRenderer.cpp core/render/DevelopPipeline.cpp core/render/AdvancedColor.cpp core/edits/CropSettings.cpp core/edits/EditDocument.cpp core/export/JpegICC.cpp
    core/export/ExportOptions.cpp core/export/LinearOutput.cpp core/export/DngWriter.cpp
    core/color/RGBProfiles.cpp core/color/DisplayTransform.cpp core/jobs/JobManager.cpp)
target_include_directories(luma_core PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_include_directories(luma_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(luma_core PUBLIC luma_libraw luma_sqlite luma_lcms)
target_compile_options(luma_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(luma_macos STATIC platform/macos/RasterDecoder.mm platform/macos/AdvancedImageExporter.mm platform/macos/ExportMetadata.mm platform/macos/Watermark.mm platform/macos/ExportColor.mm platform/macos/DisplayProfile.mm platform/macos/FileHasher.cpp platform/macos/ImageExporter.mm)
target_sources(luma_macos PRIVATE platform/macos/MemoryPressure.mm)
target_sources(luma_macos PRIVATE platform/macos/SystemDiagnostics.mm)
set_source_files_properties(platform/macos/SystemDiagnostics.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_source_files_properties(platform/macos/MemoryPressure.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_sources(luma_macos PRIVATE platform/macos/MetalDevelop.mm)
target_sources(luma_macos PRIVATE platform/macos/VisionSegmentation.mm)
target_sources(luma_macos PRIVATE platform/macos/PhotoCulling.mm)
set_source_files_properties(platform/macos/PhotoCulling.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_sources(luma_macos PRIVATE platform/macos/CoreMLDenoise.mm platform/macos/CoreMLSky.mm)
target_sources(luma_macos PRIVATE platform/macos/SemanticEmbedding.mm)
target_sources(luma_macos PRIVATE platform/macos/ImageAlignment.mm platform/macos/PhotoMetadataIO.mm platform/macos/ApplicationAlert.mm)
set_source_files_properties(platform/macos/ApplicationAlert.mm platform/macos/PhotoMetadataIO.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_source_files_properties(platform/macos/ImageAlignment.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_source_files_properties(platform/macos/SemanticEmbedding.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(luma_macos PRIVATE "-framework NaturalLanguage")
set_source_files_properties(platform/macos/CoreMLDenoise.mm platform/macos/CoreMLSky.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(luma_macos PRIVATE "-framework CoreML")
set_source_files_properties(platform/macos/VisionSegmentation.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(luma_macos PRIVATE "-framework Vision" "-framework CoreVideo")
set_source_files_properties(platform/macos/MetalDevelop.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(luma_macos PRIVATE luma_core "-framework Metal")
target_include_directories(luma_macos PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(luma_macos PRIVATE luma_lcms "-framework AppKit" "-framework ColorSync" "-framework CoreImage" "-framework ImageIO" "-framework CoreText" "-framework UniformTypeIdentifiers")
target_compile_options(luma_macos PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(luma_services STATIC apps/shared/DerivedImage.cpp apps/shared/MergeService.cpp apps/shared/BackupService.cpp apps/shared/MetadataService.cpp apps/shared/RelinkService.cpp apps/shared/PhotoDecoder.cpp apps/shared/SessionService.cpp apps/shared/LibraryService.cpp)
target_include_directories(luma_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(luma_services PUBLIC luma_core luma_macos)
target_compile_options(luma_services PRIVATE -Wall -Wextra -Wpedantic -Werror)

qt_standard_project_setup(REQUIRES 6.10)
add_library(luma_localization STATIC apps/desktop/LocalizationController.cpp apps/desktop/LocalizationController.h)
target_include_directories(luma_localization PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(luma_localization PUBLIC Qt6::Core Qt6::Qml)
target_compile_options(luma_localization PRIVATE -Wall -Wextra -Wpedantic -Werror)
qt_add_resources(luma_localization luma_languages PREFIX "/luma/i18n" BASE "resources/i18n"
    FILES resources/i18n/en.json resources/i18n/ru.json resources/i18n/zh_CN.json resources/i18n/hy.json
          resources/i18n/de.json resources/i18n/es.json resources/i18n/it.json resources/i18n/fr.json)

qt_add_executable(Apertone MACOSX_BUNDLE
    apps/desktop/AppearanceController.cpp apps/desktop/AppearanceController.h
    apps/desktop/DiagnosticSession.cpp apps/desktop/DiagnosticSession.h platform/macos/CrashCapture.mm
    apps/desktop/CatalogStartup.cpp apps/desktop/CatalogStartup.h
    apps/desktop/DiagnosticsController.cpp apps/desktop/DiagnosticsController.h
    apps/desktop/ExportController.cpp apps/desktop/ExportController.h apps/shared/ExportSettings.cpp
    apps/desktop/BatchController.cpp apps/desktop/BatchController.h
    apps/desktop/PresetController.cpp apps/desktop/PresetController.h apps/shared/PresetStore.cpp apps/shared/XmpPresetImport.cpp apps/shared/BuiltinPresets.cpp apps/desktop/EditorPresets.cpp apps/desktop/EditorQuickLooks.cpp apps/desktop/EditorVersions.cpp apps/desktop/EditorHistory.cpp apps/desktop/EditorHistogram.cpp apps/desktop/EditorWhiteBalance.cpp apps/desktop/EditorCreative.cpp apps/desktop/EditorCompare.cpp
    apps/desktop/UpdateController.cpp apps/desktop/UpdateController.h platform/macos/Updater.mm
    apps/desktop/main.cpp apps/desktop/EditorController.cpp apps/desktop/EditorAI.cpp apps/desktop/EditorDenoise.cpp apps/desktop/EditorMerge.cpp apps/desktop/EditorRetouch.cpp apps/desktop/EditorTransform.cpp apps/desktop/EditorLens.cpp apps/desktop/EditorCrop.cpp apps/desktop/EditorLocal.cpp apps/desktop/EditorMaskComponents.cpp apps/desktop/EditorDetail.cpp apps/desktop/EditorAdvanced.cpp apps/desktop/PreviewProvider.cpp apps/desktop/ThumbnailProvider.cpp apps/desktop/LibraryModel.cpp apps/desktop/LibrarySemantic.cpp apps/desktop/LibraryIntelligence.cpp apps/desktop/LibrarySmart.cpp apps/desktop/LibraryImport.cpp apps/desktop/LibraryMaintenance.cpp apps/desktop/LibraryAcceptance.cpp apps/desktop/LocalMaskAcceptance.cpp)
set_source_files_properties(apps/desktop/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(Apertone URI Apertone.Desktop VERSION 1.0
    QML_FILES apps/desktop/qml/Theme.qml apps/desktop/qml/ApertoneButton.qml apps/desktop/qml/ApertoneComboBox.qml apps/desktop/qml/ApertoneTextField.qml apps/desktop/qml/ApertoneSpinBox.qml apps/desktop/qml/PhotoViewport.qml apps/desktop/qml/HistogramPanel.qml apps/desktop/qml/CreativePanel.qml apps/desktop/qml/ColorWheel.qml apps/desktop/qml/HistoryDialog.qml apps/desktop/qml/VersionsDialog.qml apps/desktop/qml/CatalogStartup.qml apps/desktop/qml/DiagnosticsDialog.qml apps/desktop/qml/LocalCheckBox.qml apps/desktop/qml/LocalPanel.qml apps/desktop/qml/RetouchPanel.qml apps/desktop/qml/RetouchOverlay.qml apps/desktop/qml/LocalSlider.qml apps/desktop/qml/LocalOverlay.qml apps/desktop/qml/PreferencesPanel.qml apps/desktop/qml/PhotoMetadataDialog.qml apps/desktop/qml/CatalogMaintenanceDialog.qml apps/desktop/qml/ExportPanel.qml apps/desktop/qml/TransformPanel.qml apps/desktop/qml/TransformOverlay.qml apps/desktop/qml/LensPanel.qml apps/desktop/qml/CropPanel.qml apps/desktop/qml/CropOverlay.qml apps/desktop/qml/DetailPanel.qml apps/desktop/qml/BatchSettings.qml apps/desktop/qml/PresetPanel.qml apps/desktop/qml/Main.qml apps/desktop/qml/UpdateBell.qml apps/desktop/qml/MetadataRow.qml apps/desktop/qml/DevelopSlider.qml apps/desktop/qml/CurvesPanel.qml apps/desktop/qml/HSLPanel.qml apps/desktop/qml/LibraryPane.qml apps/desktop/qml/ImportDialog.qml apps/desktop/qml/CompareDialog.qml apps/desktop/qml/ComparePane.qml apps/desktop/qml/SmartCollectionDialog.qml apps/desktop/qml/SmartRuleEditor.qml apps/desktop/qml/SemanticSearchDialog.qml apps/desktop/qml/PhotoCard.qml)
target_link_libraries(Apertone PRIVATE luma_localization luma_services Qt6::Quick Qt6::QuickControls2 luma_sparkle)
target_link_libraries(Apertone PRIVATE luma_crashreporter "-framework Foundation" "-framework CoreServices")
target_link_options(Apertone PRIVATE -ObjC)
set_source_files_properties(platform/macos/CrashCapture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_source_files_properties(platform/macos/Updater.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_property(TARGET Apertone APPEND PROPERTY BUILD_RPATH "${sparkle_SOURCE_DIR}")
add_custom_command(TARGET Apertone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:Apertone>/Contents/Frameworks"
    COMMAND /usr/bin/ditto "${sparkle_SOURCE_DIR}/Sparkle.framework"
        "$<TARGET_BUNDLE_DIR:Apertone>/Contents/Frameworks/Sparkle.framework"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:Apertone>/Contents/Resources/ThirdParty"
    COMMAND ${CMAKE_COMMAND} -E copy "${sparkle_SOURCE_DIR}/LICENSE"
        "$<TARGET_BUNDLE_DIR:Apertone>/Contents/Resources/ThirdParty/Sparkle-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${plcrashreporter_SOURCE_DIR}/LICENSE"
        "$<TARGET_BUNDLE_DIR:Apertone>/Contents/Resources/ThirdParty/PLCrashReporter-LICENSE.txt")
target_compile_definitions(Apertone PRIVATE APERTONE_VERSION="${PROJECT_VERSION}")
set_source_files_properties(resources/branding/Apertone.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(Apertone PRIVATE resources/branding/Apertone.icns)
target_compile_options(Apertone PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(Apertone PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.luma.desktop"
    MACOSX_BUNDLE_BUNDLE_NAME Apertone
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/macos/Info.plist.in")

set(LUMA_BUNDLED_MODELS_DIR "${CMAKE_BINARY_DIR}/models" CACHE PATH "Compiled offline model artifacts")
if(EXISTS "${LUMA_BUNDLED_MODELS_DIR}/manifest.json")
    if(Python3_VERSION VERSION_LESS "3.11")
        message(FATAL_ERROR "Bundled model manifests require Python 3.11 or later; use the Python 3.12 build environment.")
    endif()
    # Run even when Apertone itself did not relink, so a changed model recipe cannot
    # leave stale resources in a previously generated app bundle.
    add_custom_target(luma_bundled_models ALL
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/install-bundled-models.py"
            "${LUMA_BUNDLED_MODELS_DIR}" "$<TARGET_BUNDLE_DIR:Apertone>"
        DEPENDS Apertone
        VERBATIM)
elseif(PROJECT_VERSION VERSION_GREATER_EQUAL "1.0.0")
    message(FATAL_ERROR "Build the bundled models with scripts/build-bundled-models.sh before configuring Apertone 1.0.")
endif()

add_executable(luma_cli apps/cli/main.cpp apps/cli/LibraryCommands.cpp)
set_target_properties(luma_cli PROPERTIES OUTPUT_NAME apertone
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(luma_cli PRIVATE luma_services)
target_compile_options(luma_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
option(LUMA_ENABLE_SCALE_CONTRACT_TESTS "Enable deferred application scale contracts for pre-1.0 qualification" OFF)
if(BUILD_TESTING)
    add_executable(luma_culling_tests tests/unit/CullingTests.cpp)
    target_link_libraries(luma_culling_tests PRIVATE luma_macos)
    target_compile_options(luma_culling_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME intelligent-library-culling COMMAND luma_culling_tests)
    set_tests_properties(intelligent-library-culling PROPERTIES TIMEOUT 30 LABELS "unit;library;ai")
    add_executable(luma_quick_look_tests tests/unit/QuickLookTests.cpp)
    target_link_libraries(luma_quick_look_tests PRIVATE luma_core)
    target_compile_options(luma_quick_look_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME quick-look-stack-and-blur COMMAND luma_quick_look_tests)
    add_executable(luma_vision_selection_tests tests/unit/VisionSelectionTests.cpp)
    target_link_libraries(luma_vision_selection_tests PRIVATE luma_macos)
    target_compile_definitions(luma_vision_selection_tests PRIVATE
        APERTONE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(luma_vision_selection_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME vision-people-selections COMMAND luma_vision_selection_tests)
    set_tests_properties(vision-people-selections PROPERTIES TIMEOUT 30 LABELS "unit;ai;masks")
    # Explicit model qualification uses this bridge; ordinary CTest does not
    # require a downloaded model or silently claim its quality has been tested.
    add_executable(luma_denoise_runtime_probe tests/models/DenoiseRuntime.cpp)
    target_link_libraries(luma_denoise_runtime_probe PRIVATE luma_macos Qt6::Core)
    target_compile_options(luma_denoise_runtime_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(luma_sky_runtime_probe tests/models/SkyRuntime.cpp)
    target_link_libraries(luma_sky_runtime_probe PRIVATE luma_macos Qt6::Core)
    target_compile_options(luma_sky_runtime_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(luma_diagnostic_session_tests tests/unit/DiagnosticSessionTests.cpp
        apps/desktop/DiagnosticSession.cpp)
    target_include_directories(luma_diagnostic_session_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(luma_diagnostic_session_tests PRIVATE Qt6::Core)
    target_compile_options(luma_diagnostic_session_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME diagnostic-session-shutdown COMMAND luma_diagnostic_session_tests)
    set_tests_properties(diagnostic-session-shutdown PROPERTIES TIMEOUT 20 LABELS "unit;diagnostics")
    add_test(NAME model-xcode-preflight COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/unit/test_model_toolchain.py")
    set_tests_properties(model-xcode-preflight PROPERTIES TIMEOUT 15 LABELS "unit;models")
    add_executable(luma_creative_tests tests/unit/CreativeWorkflowTests.cpp)
    target_link_libraries(luma_creative_tests PRIVATE luma_services)
    target_compile_options(luma_creative_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME creative-settings-render-auto COMMAND luma_creative_tests)
    set_tests_properties(creative-settings-render-auto PROPERTIES TIMEOUT 30 LABELS "unit;creative")
    add_executable(luma_smart_collection_tests tests/unit/SmartCollectionTests.cpp)
    target_link_libraries(luma_smart_collection_tests PRIVATE luma_services)
    target_compile_options(luma_smart_collection_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME smart-collection-query-backup COMMAND luma_smart_collection_tests)
    set_tests_properties(smart-collection-query-backup PROPERTIES TIMEOUT 30 LABELS "unit;catalog")
    add_executable(luma_advanced_import_tests tests/unit/AdvancedImportTests.cpp)
    target_link_libraries(luma_advanced_import_tests PRIVATE luma_services)
    target_compile_options(luma_advanced_import_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME advanced-import-copy-move-backup COMMAND luma_advanced_import_tests
        "${CMAKE_SOURCE_DIR}/tests/assets/images/quadrants.webp")
    set_tests_properties(advanced-import-copy-move-backup PROPERTIES TIMEOUT 30 LABELS "unit;import")
    add_executable(luma_staging_tests tests/unit/StagingFileTests.cpp)
    target_link_libraries(luma_staging_tests PRIVATE luma_core)
    target_compile_options(luma_staging_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME owned-staging-crash-cleanup COMMAND luma_staging_tests)
    set_tests_properties(owned-staging-crash-cleanup PROPERTIES TIMEOUT 20 LABELS "unit;recovery")
    add_executable(luma_photo_metadata_tests tests/unit/PhotoMetadataTests.cpp)
    target_link_libraries(luma_photo_metadata_tests PRIVATE luma_services Qt6::Core)
    target_compile_options(luma_photo_metadata_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME native-xmp-metadata-lists COMMAND luma_photo_metadata_tests
        "${CMAKE_SOURCE_DIR}/tests/assets/images/quadrants.webp")
    set_tests_properties(native-xmp-metadata-lists PROPERTIES TIMEOUT 30 LABELS "unit;metadata")
    add_executable(luma_ux_metric_tests tests/ux/DistributionTests.cpp)
    target_include_directories(luma_ux_metric_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(luma_ux_metric_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ux-metric-accounting COMMAND luma_ux_metric_tests)
    add_test(NAME ux-migration-data-invariants COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_migration_fixtures.py")
    set_tests_properties(ux-migration-data-invariants PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-restored-data-invariants COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_catalog_invariants.py")
    set_tests_properties(ux-restored-data-invariants PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-runner-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_runner.py")
    set_tests_properties(ux-runner-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-quality-workload-policy COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_quality_policy.py")
    set_tests_properties(ux-quality-workload-policy PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-metadata-display-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_metadata_display_fixtures.py")
    add_test(NAME ux-merge-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_merge_fixtures.py")
    add_test(NAME ux-catalog-scale-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_catalog_scale_fixtures.py")
    set_tests_properties(ux-catalog-scale-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "unit;ux;infrastructure")
    set_tests_properties(ux-merge-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "unit;ux;infrastructure")
    set_tests_properties(ux-metadata-display-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "unit;ux;infrastructure")
    add_test(NAME ux-thumbnail-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_thumbnail_response_fixtures.py")
    set_tests_properties(ux-thumbnail-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-thumbnail-invalidation-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_thumbnail_invalidation_fixtures.py")
    set_tests_properties(ux-thumbnail-invalidation-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-thumbnail-away-back-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_thumbnail_away_back_fixtures.py")
    set_tests_properties(ux-thumbnail-away-back-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-watermark-snapshot-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_export_watermark_fixtures.py")
    set_tests_properties(ux-watermark-snapshot-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-crop-done-fixture-backend-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_crop_done_fixtures.py")
    set_tests_properties(ux-crop-done-fixture-backend-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-export-current-filename-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_export_current_filename_fixtures.py")
    set_tests_properties(ux-export-current-filename-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-mask-transparent-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_mask_transparent_fixture.py")
    set_tests_properties(ux-mask-transparent-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-native-bundle-provenance COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_native_bundle_provenance.py")
    set_tests_properties(ux-native-bundle-provenance PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    set_tests_properties(ux-metric-accounting PROPERTIES LABELS "ux;infrastructure")
    add_executable(luma_ux_history_oracle_tests tests/ux/HistoryChaosModelTests.cpp tests/ux/HistoryChaosPlan.cpp)
    target_include_directories(luma_ux_history_oracle_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(luma_ux_history_oracle_tests PRIVATE Qt6::Core luma_core)
    target_compile_options(luma_ux_history_oracle_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ux-history-chaos-plan COMMAND luma_ux_history_oracle_tests)
    add_executable(luma_inference_stack_tests tests/unit/InferenceStackTests.cpp)
    target_include_directories(luma_inference_stack_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(luma_inference_stack_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME inference-stack-contract COMMAND luma_inference_stack_tests)
    add_test(NAME performance-baseline-schema COMMAND "${CMAKE_SOURCE_DIR}/.tools/venv/bin/python" "${CMAKE_SOURCE_DIR}/tests/ux/performance_baseline.py")
    add_test(NAME ux-history-chaos-routing COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_history_chaos_cli.py")
    set_tests_properties(ux-history-chaos-plan ux-history-chaos-routing PROPERTIES TIMEOUT 120 LABELS "ux;infrastructure")
    find_package(Qt6 6.10.3 REQUIRED COMPONENTS Test)
    qt_add_executable(luma_ux_tests MACOSX_BUNDLE tests/ux/main.cpp tests/ux/UxContext.cpp tests/ux/UxPixels.cpp tests/ux/UxDriver.cpp tests/ux/UxMetrics.cpp tests/ux/SqliteFault.cpp tests/ux/RecoveryScenarios.cpp tests/ux/BasicScenarios.cpp tests/ux/LibraryScenarios.cpp tests/ux/MaskScenarios.cpp tests/ux/LifecycleScenarios.cpp tests/ux/ImportScenarios.cpp tests/ux/ExportScenarios.cpp tests/ux/RestartScenarios.cpp tests/ux/BatchScenarios.cpp tests/ux/GeometryScenarios.cpp tests/ux/CropScenarios.cpp tests/ux/AccessibilityScenarios.cpp tests/ux/LibraryScaleScenarios.cpp tests/ux/SelectionScenarios.cpp tests/ux/KeyboardScenarios.cpp tests/ux/NavigationScenarios.cpp tests/ux/FilterExtraScenarios.cpp tests/ux/DetailScenarios.cpp tests/ux/LensScenarios.cpp tests/ux/AdvancedScenarios.cpp tests/ux/PresetScenarios.cpp tests/ux/PresetExtraScenarios.cpp tests/ux/PresetRemainingScenarios.cpp tests/ux/HistoryStressScenarios.cpp tests/ux/ImportExtraScenarios.cpp tests/ux/CurrentPerformanceScenarios.cpp tests/ux/WhiteBalanceScenarios.cpp tests/ux/PreviewBackendScenarios.cpp tests/ux/ZoomScenarios.cpp tests/ux/GestureRemainingScenarios.cpp tests/ux/VisualRemainingScenarios.cpp tests/ux/LibraryRemainingScenarios.cpp tests/ux/BatchUpdateRemainingScenarios.cpp tests/ux/AppExtraScenarios.cpp tests/ux/ExportExtraScenarios.cpp tests/ux/PreviewExtraScenarios.cpp tests/ux/RecoveryExtraScenarios.cpp tests/ux/AsyncScenarios.cpp tests/ux/TransformScenarios.cpp tests/ux/TransformExtraScenarios.cpp tests/ux/LocalizationScenarios.cpp tests/ux/UpdaterScenarios.cpp
        apps/desktop/ExportController.cpp apps/shared/ExportSettings.cpp
        apps/desktop/BatchController.cpp apps/desktop/PresetController.cpp apps/shared/PresetStore.cpp apps/shared/XmpPresetImport.cpp apps/shared/BuiltinPresets.cpp
        apps/desktop/EditorController.cpp apps/desktop/EditorAI.cpp apps/desktop/EditorDenoise.cpp apps/desktop/EditorMerge.cpp apps/desktop/EditorRetouch.cpp apps/desktop/EditorTransform.cpp apps/desktop/EditorLens.cpp apps/desktop/EditorCrop.cpp apps/desktop/EditorLocal.cpp apps/desktop/EditorMaskComponents.cpp apps/desktop/EditorDetail.cpp apps/desktop/EditorAdvanced.cpp apps/desktop/EditorPresets.cpp apps/desktop/EditorQuickLooks.cpp apps/desktop/EditorVersions.cpp apps/desktop/EditorHistory.cpp apps/desktop/EditorHistogram.cpp apps/desktop/EditorWhiteBalance.cpp apps/desktop/EditorCreative.cpp apps/desktop/EditorCompare.cpp
        apps/desktop/PreviewProvider.cpp apps/desktop/ThumbnailProvider.cpp apps/desktop/LibraryModel.cpp apps/desktop/LibrarySemantic.cpp apps/desktop/LibraryIntelligence.cpp apps/desktop/LibrarySmart.cpp apps/desktop/LibraryImport.cpp apps/desktop/LibraryMaintenance.cpp apps/desktop/UpdateController.cpp)
    target_link_libraries(luma_ux_tests PRIVATE luma_localization luma_services Qt6::Test Qt6::Quick Qt6::QuickControls2)
    target_sources(luma_ux_tests PRIVATE apps/desktop/AppearanceController.cpp apps/desktop/AppearanceController.h apps/desktop/DiagnosticsController.cpp
        apps/desktop/DiagnosticSession.cpp platform/macos/CrashCapture.mm)
    target_link_libraries(luma_ux_tests PRIVATE luma_crashreporter "-framework CoreServices")
    target_link_options(luma_ux_tests PRIVATE -ObjC)
    set_target_properties(luma_ux_tests PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "app.luma.ux-tests"
        MACOSX_BUNDLE_BUNDLE_NAME "ApertoneUXTests"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/tests/ux/Info.plist.in")
    if(TARGET luma_bundled_models)
        add_custom_target(luma_ux_bundled_models ALL
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/install-bundled-models.py"
                "${LUMA_BUNDLED_MODELS_DIR}" "$<TARGET_BUNDLE_DIR:luma_ux_tests>"
            DEPENDS luma_ux_tests
            VERBATIM)
    endif()
    # Keep the established runner path; the runner launches its canonical bundle target.
    # Configure also repairs a deleted launcher when the executable needs no relink.
    set(UX_BUILD_ROOT "${CMAKE_BINARY_DIR}")
    set(UX_PROJECT_ROOT "${CMAKE_SOURCE_DIR}")
    set(UX_BUNDLE_EXECUTABLE "${CMAKE_BINARY_DIR}/luma_ux_tests.app/Contents/MacOS/luma_ux_tests")
    include(cmake/LinkUxTestExecutable.cmake)
    add_custom_command(TARGET luma_ux_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" "-DUX_BUILD_ROOT=${CMAKE_BINARY_DIR}"
            "-DUX_PROJECT_ROOT=${CMAKE_SOURCE_DIR}"
            "-DUX_BUNDLE_EXECUTABLE=$<TARGET_FILE:luma_ux_tests>"
            -P "${CMAKE_SOURCE_DIR}/cmake/LinkUxTestExecutable.cmake"
        VERBATIM)
    target_sources(luma_ux_tests PRIVATE tests/ux/ExportSettingsScenarios.cpp tests/ux/ExportPrivacyFixture.mm)
    set_source_files_properties(tests/ux/ExportPrivacyFixture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(luma_ux_tests PRIVATE "-framework Foundation" "-framework ImageIO" "-framework CoreGraphics")
    target_sources(luma_ux_tests PRIVATE tests/ux/ExportStressScenarios.cpp)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_MERGE_PROBE)
    target_sources(luma_ux_tests PRIVATE tests/ux/MergeScenarios.cpp tests/ux/DailyWorkflowScenarios.cpp tests/ux/MigrationDataScenarios.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/CatalogMaintenanceScenarios.cpp tests/ux/MetadataScenarios.cpp tests/ux/RelinkScenarios.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/ExportResizeScenarios.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/OwnedVolume.mm tests/ux/RemovableSourceScenarios.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/RemovableDestinationScenarios.cpp tests/ux/CopyDestinationScenarios.cpp tests/ux/KeyboardActivationCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/MaskHandleCropLossCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/LocalizedDialogCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/AutoTransformProbe.cpp tests/ux/AutoTransformRaceCandidate.cpp)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_AUTO_TRANSFORM_PROBE)
    # Direct UX objects replace the static library's matching service object;
    # only the test executable compiles the real filesystem checkpoints.
    target_sources(luma_ux_tests PRIVATE tests/ux/PersistenceProbe.cpp apps/shared/MetadataService.cpp
        core/catalog/Catalog.cpp core/catalog/CatalogBackup.cpp apps/shared/DerivedImage.cpp)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_PERSISTENCE_PROBE)
    target_sources(luma_ux_tests PRIVATE tests/ux/RetouchResultProbe.cpp tests/ux/RetouchScenarios.cpp)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_RETOUCH_RESULT_PROBE)
    target_sources(luma_ux_tests PRIVATE tests/ux/PreviewResultProbe.cpp)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_PREVIEW_RESULT_PROBE)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_MEMORY_PRESSURE_PROBE)
    target_sources(luma_ux_tests PRIVATE tests/ux/LoadProbe.cpp)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_LOAD_PROBE)
    set_source_files_properties(tests/ux/OwnedVolume.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(luma_ux_tests PRIVATE tests/ux/PanelDetailStressScenarios.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/HistoryChaosPlan.cpp tests/ux/HistoryChaosScenarios.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/ThumbnailRemainingScenarios.cpp tests/ux/RetinaScenarios.cpp tests/ux/NativeRetinaGeometry.mm)
    set_source_files_properties(tests/ux/NativeRetinaGeometry.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(luma_ux_tests PRIVATE "-framework AppKit")
    # Compile the identical renderer body under a separate UX-only symbol. The
    # shipping luma_core object/API remains unchanged and has no row observer.
    add_library(luma_ux_cpu_row_renderer OBJECT core/render/CpuRenderer.cpp)
    target_link_libraries(luma_ux_cpu_row_renderer PRIVATE luma_core)
    target_compile_definitions(luma_ux_cpu_row_renderer PRIVATE LUMA_UX_CPU_RENDER_ROWS=1)
    target_compile_options(luma_ux_cpu_row_renderer PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(luma_ux_cpu_row_equivalence_tests tests/ux/CpuRowEquivalenceTests.cpp
        $<TARGET_OBJECTS:luma_ux_cpu_row_renderer>)
    target_link_libraries(luma_ux_cpu_row_equivalence_tests PRIVATE luma_core)
    target_compile_definitions(luma_ux_cpu_row_equivalence_tests PRIVATE LUMA_UX_CPU_RENDER_ROWS=1)
    target_compile_options(luma_ux_cpu_row_equivalence_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ux-cpu-partial-row-equivalence COMMAND luma_ux_cpu_row_equivalence_tests)
    set_tests_properties(ux-cpu-partial-row-equivalence PROPERTIES TIMEOUT 30 LABELS "unit;ux;infrastructure")
    add_test(NAME ux-thumbnail-partial-render-fixture-accounting COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/ux/test_race007_fixtures.py")
    set_tests_properties(ux-thumbnail-partial-render-fixture-accounting PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    add_test(NAME ux-cpu-probe-shipping-isolation COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/ux/check_renderer_symbol_isolation.py"
        --shipping "$<TARGET_FILE:Apertone>" --ux "$<TARGET_FILE:luma_ux_tests>")
    set_tests_properties(ux-cpu-probe-shipping-isolation PROPERTIES TIMEOUT 30 LABELS "ux;infrastructure")
    target_sources(luma_ux_tests PRIVATE tests/ux/ThumbnailResponseTrace.cpp
        tests/ux/ThumbnailCpuRenderProbe.cpp $<TARGET_OBJECTS:luma_ux_cpu_row_renderer>)
    target_sources(luma_ux_tests PRIVATE tests/ux/CardFlagLocalizationCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/ExportWatermarkSnapshotMissingCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/Lib004ActualResponseCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/Race002ActualResponseCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/Race007PartialRenderCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/MinimizeExportCandidate.cpp tests/ux/NativeWindowLifecycle.mm)
    set_source_files_properties(tests/ux/NativeWindowLifecycle.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(luma_ux_tests PRIVATE tests/ux/Lib005ActualResponseCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/CropDoneResidueCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/ExportCurrentFilenameLegalCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/LibraryScrollCandidate.cpp)
    target_sources(luma_ux_tests PRIVATE tests/ux/AppDeactivationCandidate.cpp tests/ux/NativeApplicationHandoff.mm)
    set_source_files_properties(tests/ux/NativeApplicationHandoff.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_HISTORY_READBACK=1)
    target_compile_definitions(luma_ux_tests PRIVATE LUMA_UX_THUMBNAIL_TRACE=1)
    target_compile_definitions(luma_ux_tests PRIVATE APERTONE_VERSION="${PROJECT_VERSION}")
    target_compile_options(luma_ux_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    qt_add_executable(luma_localization_tests tests/unit/LocalizationTests.cpp apps/desktop/AppearanceController.cpp apps/desktop/AppearanceController.h)
    target_include_directories(luma_localization_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(luma_localization_tests PRIVATE luma_localization Qt6::Quick Qt6::QuickControls2)
    target_compile_options(luma_localization_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME language-runtime-preferences COMMAND luma_localization_tests "${CMAKE_SOURCE_DIR}/apps/desktop/qml")
    set_tests_properties(language-runtime-preferences PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
    qt_add_executable(luma_update_tests tests/unit/UpdateTests.cpp apps/desktop/UpdateController.cpp apps/desktop/UpdateController.h)
    target_include_directories(luma_update_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(luma_update_tests PRIVATE Qt6::Core)
    target_compile_options(luma_update_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME update-restart-guard COMMAND luma_update_tests)
    qt_add_executable(luma_batch_tests tests/unit/ExportQueueTests.cpp apps/desktop/ExportController.cpp apps/desktop/ExportController.h apps/shared/ExportSettings.cpp tests/unit/BatchTests.cpp tests/unit/ThumbnailTests.cpp apps/desktop/ThumbnailProvider.cpp apps/desktop/BatchController.cpp
        apps/desktop/BatchController.h apps/desktop/LibraryModel.cpp apps/desktop/LibrarySemantic.cpp apps/desktop/LibraryIntelligence.cpp apps/desktop/LibrarySmart.cpp apps/desktop/LibraryImport.cpp apps/desktop/LibraryMaintenance.cpp
        apps/desktop/UpdateController.cpp apps/desktop/UpdateController.h
        apps/shared/PresetStore.cpp apps/shared/XmpPresetImport.cpp apps/shared/BuiltinPresets.cpp apps/desktop/PresetController.cpp apps/desktop/PresetController.h
        apps/desktop/EditorController.cpp apps/desktop/EditorAI.cpp apps/desktop/EditorDenoise.cpp apps/desktop/EditorMerge.cpp apps/desktop/EditorRetouch.cpp apps/desktop/EditorTransform.cpp apps/desktop/EditorLens.cpp apps/desktop/EditorCrop.cpp apps/desktop/EditorLocal.cpp apps/desktop/EditorMaskComponents.cpp apps/desktop/EditorDetail.cpp apps/desktop/EditorAdvanced.cpp apps/desktop/EditorPresets.cpp apps/desktop/EditorQuickLooks.cpp apps/desktop/EditorVersions.cpp apps/desktop/EditorHistory.cpp apps/desktop/EditorHistogram.cpp apps/desktop/EditorWhiteBalance.cpp apps/desktop/EditorCreative.cpp apps/desktop/EditorCompare.cpp
        apps/desktop/PreviewProvider.cpp)
    target_link_libraries(luma_batch_tests PRIVATE luma_services Qt6::Quick Qt6::QuickControls2)
    target_compile_options(luma_batch_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    qt_add_executable(luma_preset_tests tests/unit/PresetTests.cpp tests/unit/LocalEditorTests.cpp tests/unit/LocalCompleteEditorTests.cpp tests/unit/XmpPresetTests.cpp tests/unit/FilterEditorTests.cpp tests/unit/CropEditorTests.cpp tests/unit/LensEditorTests.cpp tests/unit/TransformEditorTests.cpp apps/shared/PresetStore.cpp apps/shared/XmpPresetImport.cpp apps/shared/BuiltinPresets.cpp
        apps/desktop/PresetController.cpp apps/desktop/PresetController.h
        apps/desktop/EditorController.cpp apps/desktop/EditorAI.cpp apps/desktop/EditorDenoise.cpp apps/desktop/EditorMerge.cpp apps/desktop/EditorRetouch.cpp apps/desktop/EditorTransform.cpp apps/desktop/EditorLens.cpp apps/desktop/EditorCrop.cpp apps/desktop/EditorLocal.cpp apps/desktop/EditorMaskComponents.cpp apps/desktop/EditorDetail.cpp apps/desktop/EditorAdvanced.cpp apps/desktop/EditorPresets.cpp apps/desktop/EditorQuickLooks.cpp apps/desktop/EditorVersions.cpp apps/desktop/EditorHistory.cpp apps/desktop/EditorHistogram.cpp apps/desktop/EditorWhiteBalance.cpp apps/desktop/EditorCreative.cpp apps/desktop/EditorCompare.cpp
        apps/desktop/PreviewProvider.cpp)
    target_link_libraries(luma_preset_tests PRIVATE luma_localization luma_services Qt6::Quick Qt6::QuickControls2)
    target_compile_options(luma_preset_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(luma_raster_tests tests/unit/RasterTests.mm)
    target_link_libraries(luma_raster_tests PRIVATE luma_services)
    target_compile_options(luma_raster_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME raster-native-color-alpha COMMAND luma_raster_tests "${CMAKE_SOURCE_DIR}/tests/assets/images")
    add_executable(luma_export_tests tests/unit/ExportTests.cpp tests/unit/ExportNativeTests.mm)
    target_link_libraries(luma_export_tests PRIVATE luma_services)
    target_compile_options(luma_export_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME advanced-export-options-color-resize COMMAND luma_export_tests)
    add_executable(luma_filter_tests tests/unit/FilterTests.cpp apps/shared/PresetStore.cpp apps/shared/XmpPresetImport.cpp apps/shared/BuiltinPresets.cpp)
    target_link_libraries(luma_filter_tests PRIVATE luma_services Qt6::Gui)
    target_compile_options(luma_filter_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME builtin-filter-photographic-quality COMMAND luma_filter_tests "${CMAKE_SOURCE_DIR}/tests/assets/filters" "${CMAKE_BINARY_DIR}/qa/filters")
    set_tests_properties(builtin-filter-photographic-quality PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(luma_local_mask_tests tests/unit/LocalMaskTests.cpp tests/unit/LocalCompleteTests.cpp)
    target_link_libraries(luma_local_mask_tests PRIVATE luma_services)
    target_compile_options(luma_local_mask_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME local-mask-render-storage COMMAND luma_local_mask_tests)
    add_executable(luma_transform_tests tests/unit/TransformTests.cpp)
    target_link_libraries(luma_transform_tests PRIVATE luma_services)
    target_compile_options(luma_transform_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME perspective-transform-unit COMMAND luma_transform_tests)
    add_executable(luma_lens_tests tests/unit/LensTests.cpp)
    target_link_libraries(luma_lens_tests PRIVATE luma_services)
    target_compile_options(luma_lens_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME lens-calibration-unit COMMAND luma_lens_tests)
    add_executable(luma_crop_tests tests/unit/CropTests.cpp)
    target_link_libraries(luma_crop_tests PRIVATE luma_services)
    target_compile_options(luma_crop_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME crop-geometry-unit COMMAND luma_crop_tests)
    add_executable(luma_noise_tests tests/unit/NoiseReductionTests.cpp)
    target_link_libraries(luma_noise_tests PRIVATE luma_services)
    target_compile_options(luma_noise_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME noise-reduction-unit COMMAND luma_noise_tests)
    add_executable(luma_sharpening_tests tests/unit/SharpeningTests.cpp)
    target_link_libraries(luma_sharpening_tests PRIVATE luma_services)
    target_compile_options(luma_sharpening_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME sharpening-unit COMMAND luma_sharpening_tests)
    add_executable(luma_tests tests/unit/CoreTests.cpp)
    target_link_libraries(luma_tests PRIVATE luma_services)
    target_compile_options(luma_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME core-unit COMMAND luma_tests)
    add_executable(luma_advanced_tests tests/unit/AdvancedTests.cpp)
    target_link_libraries(luma_advanced_tests PRIVATE luma_services)
    target_compile_options(luma_advanced_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME advanced-color-unit COMMAND luma_advanced_tests)
    add_executable(luma_preview_cache_tests tests/unit/PreviewCacheTests.cpp)
    target_include_directories(luma_preview_cache_tests PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_options(luma_preview_cache_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME preview-cache-contract COMMAND luma_preview_cache_tests)
    add_executable(luma_session_cache_tests tests/unit/SessionCacheTests.cpp)
    target_link_libraries(luma_session_cache_tests PRIVATE luma_services Qt6::Core Qt6::Gui)
    target_compile_options(luma_session_cache_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME session-cache-contract COMMAND luma_session_cache_tests)
    set_tests_properties(session-cache-contract PROPERTIES TIMEOUT 60 LABELS "unit;preview")
    add_executable(luma_parallel_render_tests tests/unit/ParallelRenderTests.cpp)
    target_link_libraries(luma_parallel_render_tests PRIVATE luma_core)
    target_compile_options(luma_parallel_render_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME parallel-render-equivalence COMMAND luma_parallel_render_tests)
    set_tests_properties(parallel-render-equivalence PROPERTIES TIMEOUT 90 LABELS "unit;render")

    add_executable(luma_metal_develop_tests tests/unit/MetalDevelopTests.cpp)
    target_link_libraries(luma_metal_develop_tests PRIVATE luma_services)
    target_compile_options(luma_metal_develop_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME metal-develop-equivalence COMMAND luma_metal_develop_tests)
    set_tests_properties(metal-develop-equivalence PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "unit;image-quality;metal")
    add_executable(luma_copy_publication_tests tests/unit/CopyPublicationTests.cpp tests/ux/SqliteFault.cpp)
    target_link_libraries(luma_copy_publication_tests PRIVATE luma_services Qt6::Core Qt6::Gui)
    target_compile_options(luma_copy_publication_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME copy-publication-single-link COMMAND luma_copy_publication_tests)
    set_tests_properties(copy-publication-single-link PROPERTIES TIMEOUT 30 LABELS "unit;import")
    add_executable(luma_library_tests tests/unit/LibraryTests.cpp apps/desktop/LibraryModel.cpp apps/desktop/LibrarySemantic.cpp apps/desktop/LibraryIntelligence.cpp apps/desktop/LibrarySmart.cpp apps/desktop/LibraryImport.cpp apps/desktop/LibraryMaintenance.cpp)
    target_sources(luma_library_tests PRIVATE apps/shared/PresetStore.cpp apps/shared/XmpPresetImport.cpp apps/shared/BuiltinPresets.cpp)
    target_link_libraries(luma_library_tests PRIVATE luma_services Qt6::Core)
    target_compile_options(luma_library_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME qml-shell COMMAND Apertone --smoke-test --catalog "${CMAKE_BINARY_DIR}/smoke.sqlite")
    set_tests_properties(qml-shell PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME common-image-workflow COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/integration/images.py" "$<TARGET_FILE:luma_cli>" "$<TARGET_FILE:Apertone>" "$<TARGET_FILE:luma_raster_tests>")
    set_tests_properties(common-image-workflow PROPERTIES TIMEOUT 180)
    add_test(NAME language-native-ui COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/integration/languages.py" "$<TARGET_FILE:Apertone>")
    set_tests_properties(language-native-ui PROPERTIES TIMEOUT 180)
    add_test(NAME language-catalog-coverage COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/check-translations.py")
    add_test(NAME apertone-rebrand-compatibility COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/rebrand.py")
    set_tests_properties(apertone-rebrand-compatibility PROPERTIES LABELS "unit;release")
    add_test(NAME export-settings-immutable-queue COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/integration/export_queue.py" "$<TARGET_FILE:luma_batch_tests>" "$<TARGET_FILE:luma_cli>" "$<TARGET_FILE:Apertone>")
    set_tests_properties(export-settings-immutable-queue PROPERTIES TIMEOUT 180)
    add_test(NAME batch-settings-atomic-history COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/batch.py" "$<TARGET_FILE:luma_batch_tests>")
    set_tests_properties(batch-settings-atomic-history PROPERTIES TIMEOUT 180)
    add_test(NAME edited-thumbnail-snapshot-cache COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/integration/thumbnails.py" "$<TARGET_FILE:luma_batch_tests>")
    set_tests_properties(edited-thumbnail-snapshot-cache PROPERTIES TIMEOUT 180)
    add_test(NAME presets-storage-preview-history COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/presets.py" "$<TARGET_FILE:luma_preset_tests>")
    set_tests_properties(presets-storage-preview-history PROPERTIES TIMEOUT 180)
    add_test(NAME signed-update-feed COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/appcast.py")
    set_tests_properties(signed-update-feed PROPERTIES TIMEOUT 180)
    add_test(NAME release-publisher COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/release_publisher.py")
    add_test(NAME release-archive-identity COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/release_identity.py")
    add_test(NAME release-dmg-recovery COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/release_dmg.py")
    add_test(NAME release-build-verification COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/release_build.py")
    add_test(NAME owner-accepted-ux-verification COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/owner_accepted_ux.py")
    add_test(NAME library-import-restart COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/library.py" "$<TARGET_FILE:luma_cli>" "$<TARGET_FILE:luma_library_tests>")
    set_tests_properties(library-import-restart PROPERTIES TIMEOUT 180)
    if(LUMA_ENABLE_SCALE_CONTRACT_TESTS)
        add_test(NAME library-model-scale-contract COMMAND luma_library_tests --scale-contract)
        set_tests_properties(library-model-scale-contract PROPERTIES TIMEOUT 180 LABELS "load;scale")
    endif()
    add_test(NAME synthetic-raw-restart COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/restart.py" "$<TARGET_FILE:luma_cli>" synthetic)
    add_test(NAME common-bayer-camera-matrix COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/camera_raw_matrix.py" "$<TARGET_FILE:luma_cli>")
    set_tests_properties(common-bayer-camera-matrix PROPERTIES TIMEOUT 180)
    add_test(NAME develop-export-restart COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/develop.py" "$<TARGET_FILE:luma_cli>")
    add_test(NAME complete-mask-native-restart COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/complete_masks.py" "$<TARGET_FILE:Apertone>")
    set_tests_properties(complete-mask-native-restart PROPERTIES TIMEOUT 240)
    add_test(NAME local-mask-native-restart COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/local_masks.py" "$<TARGET_FILE:Apertone>")
    set_tests_properties(local-mask-native-restart PROPERTIES TIMEOUT 240)
    add_test(NAME transform-export-migration COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/transform.py" "$<TARGET_FILE:luma_cli>")
    set_tests_properties(transform-export-migration PROPERTIES TIMEOUT 300)
    add_test(NAME lens-export-migration COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/lens.py" "$<TARGET_FILE:luma_cli>")
    set_tests_properties(lens-export-migration PROPERTIES TIMEOUT 300)
    add_test(NAME crop-export-migration COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/crop.py" "$<TARGET_FILE:luma_cli>")
    set_tests_properties(crop-export-migration PROPERTIES TIMEOUT 300)
    add_test(NAME noise-export-migration COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/noise_reduction.py" "$<TARGET_FILE:luma_cli>")
    set_tests_properties(noise-export-migration PROPERTIES TIMEOUT 300)
    add_test(NAME sharpening-export-migration COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/sharpening.py" "$<TARGET_FILE:luma_cli>")
    set_tests_properties(sharpening-export-migration PROPERTIES TIMEOUT 300)
    add_test(NAME advanced-export-restart COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/advanced.py" "$<TARGET_FILE:luma_cli>")
    set_tests_properties(advanced-export-restart PROPERTIES TIMEOUT 240)
    set_tests_properties(develop-export-restart PROPERTIES TIMEOUT 180)
    add_test(NAME nikon-d7500-restart COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/integration/restart.py" "$<TARGET_FILE:luma_cli>" private)
    set_tests_properties(nikon-d7500-restart PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    set_tests_properties(synthetic-raw-restart PROPERTIES TIMEOUT 90)
endif()
