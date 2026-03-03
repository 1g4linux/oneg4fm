cmake_minimum_required(VERSION 3.18)

find_package(Qt6 REQUIRED COMPONENTS Test)
find_package(PkgConfig REQUIRED)
include(CMakeParseArguments)
pkg_check_modules(TEST_GIO REQUIRED gio-2.0 gobject-2.0 glib-2.0)

set(PCMANFM_TEST_LIBS
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${TEST_GIO_LIBRARIES}
)

set(PCMANFM_TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/oneg4fm
    ${TEST_GIO_INCLUDE_DIRS}
)

function(oneg4fm_add_test target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})

    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${PCMANFM_TEST_LIBS} ${ARG_LIBS})
    target_include_directories(${target} PRIVATE ${PCMANFM_TEST_INCLUDES} ${ARG_INCLUDES})

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

oneg4fm_add_test(oneg4fm-basic-tests
    SOURCES
        test_basic.cpp
)

oneg4fm_add_test(oneg4fm-application-startup-plan-tests
    SOURCES
        application_startup_plan_test.cpp
        ../oneg4fm/application_startup_plan.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-commands-tests
    SOURCES
        mainwindow_commands_test.cpp
        ../oneg4fm/mainwindow_commands.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-bookmark-commands-tests
    SOURCES
        mainwindow_bookmark_commands_test.cpp
        ../oneg4fm/mainwindow_bookmark_commands.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-fileops-commands-tests
    SOURCES
        mainwindow_fileops_commands_test.cpp
        ../oneg4fm/mainwindow_fileops_commands.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-navigation-commands-tests
    SOURCES
        mainwindow_navigation_commands_test.cpp
        ../oneg4fm/mainwindow_navigation_commands.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-selection-commands-tests
    SOURCES
        mainwindow_selection_commands_test.cpp
        ../oneg4fm/mainwindow_selection_commands.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-tab-commands-tests
    SOURCES
        mainwindow_tab_commands_test.cpp
        ../oneg4fm/mainwindow_tab_commands.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-view-commands-tests
    SOURCES
        mainwindow_view_commands_test.cpp
        ../oneg4fm/mainwindow_view_commands.cpp
)

oneg4fm_add_test(oneg4fm-mainwindow-view-controller-tests
    SOURCES
        mainwindow_view_controller_test.cpp
        ../oneg4fm/mainwindow_view_controller.cpp
)

oneg4fm_add_test(oneg4fm-core-utils-tests
    SOURCES
        test_core_utils.cpp
)

oneg4fm_add_test(oneg4fm-fs-tests
    SOURCES
        fs_ops_test.cpp
        ../src/core/fs_ops.cpp
        ../src/core/linux_fs_safety.cpp
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

oneg4fm_add_test(oneg4fm-linux-fs-safety-tests
    SOURCES
        linux_fs_safety_test.cpp
        ../src/core/linux_fs_safety.cpp
)

oneg4fm_add_test(oneg4fm-ops-tests
    SOURCES
        qt_fileops_test.cpp
        ../src/backends/qt/qt_fileops.cpp
        ../src/core/ifileops.cpp
        ../src/core/file_ops_contract.cpp
        ../src/core/gio_ops_executor.cpp
        ../src/core/fs_ops.cpp
        ../src/core/linux_fs_safety.cpp
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

oneg4fm_add_test(oneg4fm-libfmqt-fileops-bridge-tests
    SOURCES
        libfmqt_fileops_bridge_test.cpp
    LIBS
        fm-qt6
    INCLUDES
        ../libfm-qt/src
        ${CMAKE_BINARY_DIR}/libfm-qt/src
)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/core/file_ops_contract.cpp" AND DEFINED BLAKE3_LIBRARIES)
    target_compile_definitions(oneg4fm-libfmqt-fileops-bridge-tests PRIVATE LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT=1)
else()
    target_compile_definitions(oneg4fm-libfmqt-fileops-bridge-tests PRIVATE LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT=0)
endif()

oneg4fm_add_test(oneg4fm-cross-layer-parity-tests
    SOURCES
        cross_layer_parity_test.cpp
        ../src/backends/qt/qt_fileops.cpp
        ../src/core/ifileops.cpp
        ../src/core/file_ops_contract.cpp
        ../src/core/gio_ops_executor.cpp
        ../src/core/fs_ops.cpp
        ../src/core/linux_fs_safety.cpp
    LIBS
        fm-qt6
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ../libfm-qt/src
        ${CMAKE_BINARY_DIR}/libfm-qt/src
        ${BLAKE3_INCLUDE_DIRS}
)

oneg4fm_add_test(oneg4fm-file-ops-contract-tests
    SOURCES
        file_ops_contract_test.cpp
        ../src/core/file_ops_contract.cpp
        ../src/core/gio_ops_executor.cpp
        ../src/core/fs_ops.cpp
        ../src/core/linux_fs_safety.cpp
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

oneg4fm_add_test(oneg4fm-archive-tests
    SOURCES
        archive_extract_test.cpp
        ../src/core/archive_extract.cpp
        ../src/core/archive_writer.cpp
        ../src/core/fs_ops.cpp
        ../src/core/linux_fs_safety.cpp
    LIBS
        ${LIBARCHIVE_LIBRARIES}
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${LIBARCHIVE_INCLUDE_DIRS}
        ${BLAKE3_INCLUDE_DIRS}
)

oneg4fm_add_test(oneg4fm-windowed-reader-tests
    SOURCES
        windowed_file_reader_test.cpp
        ../src/core/windowed_file_reader.cpp
)

oneg4fm_add_test(oneg4fm-file-ops-inventory-tests
    SOURCES
        file_ops_inventory_test.cpp
)
target_compile_definitions(oneg4fm-file-ops-inventory-tests PRIVATE "PCMANFM_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
target_compile_definitions(oneg4fm-file-ops-inventory-tests PRIVATE "ONEG4FM_BINARY_DIR=\"${CMAKE_BINARY_DIR}\"")

oneg4fm_add_test(oneg4fm-browsehistory-tests
    SOURCES
        test_browsehistory.cpp
    LIBS
        fm-qt6
)

oneg4fm_add_test(oneg4fm-thumbnailer-tests
    SOURCES
        test_thumbnailer.cpp
    LIBS
        fm-qt6
)

oneg4fm_add_test(oneg4fm-disasm-tests
    SOURCES
        disasm_engine_test.cpp
        ../src/ui/disasm_engine.cpp
    LIBS
        ${CAPSTONE_LIBRARIES}
    INCLUDES
        ${CAPSTONE_INCLUDE_DIRS}
)

set(PCMANFM_SETTINGS_SOURCES
    ../oneg4fm/settings.cpp
    ../oneg4fm/settings.h
    ../src/ui/fsqt.cpp
    ../src/core/fs_ops.cpp
    ../src/core/linux_fs_safety.cpp
)

set(PCMANFM_SETTINGS_LIBS
    fm-qt6
    ${BLAKE3_LIBRARIES}
)

set(PCMANFM_SETTINGS_INCLUDES
    ${BLAKE3_INCLUDE_DIRS}
)

oneg4fm_add_test(oneg4fm-utils-tests
    SOURCES
        test_utils.cpp
        ${PCMANFM_SETTINGS_SOURCES}
    LIBS
        ${PCMANFM_SETTINGS_LIBS}
    INCLUDES
        ${PCMANFM_SETTINGS_INCLUDES}
)

oneg4fm_add_test(oneg4fm-settings-tests
    SOURCES
        test_settings.cpp
        ${PCMANFM_SETTINGS_SOURCES}
    LIBS
        ${PCMANFM_SETTINGS_LIBS}
    INCLUDES
        ${PCMANFM_SETTINGS_INCLUDES}
)

oneg4fm_add_test(oneg4fm-settings-functionality-tests
    SOURCES
        test_settings_functionality.cpp
        ${PCMANFM_SETTINGS_SOURCES}
    LIBS
        ${PCMANFM_SETTINGS_LIBS}
    INCLUDES
        ${PCMANFM_SETTINGS_INCLUDES}
)
target_compile_definitions(oneg4fm-settings-functionality-tests PRIVATE "PCMANFM_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

oneg4fm_add_test(oneg4fm-settings-schema-tests
    SOURCES
        settings_schema_test.cpp
        ${PCMANFM_SETTINGS_SOURCES}
    LIBS
        ${PCMANFM_SETTINGS_LIBS}
    INCLUDES
        ${PCMANFM_SETTINGS_INCLUDES}
)
target_compile_definitions(oneg4fm-settings-schema-tests PRIVATE "PCMANFM_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

set(PCMANFM_XDGDIR_SOURCES
    ../oneg4fm/xdgdir.cpp
    ../src/ui/fsqt.cpp
    ../src/core/fs_ops.cpp
    ../src/core/linux_fs_safety.cpp
)

oneg4fm_add_test(oneg4fm-xdgdir-tests
    SOURCES
        test_xdgdir.cpp
        ${PCMANFM_XDGDIR_SOURCES}
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

oneg4fm_add_test(oneg4fm-xdgdir-functionality-tests
    SOURCES
        test_xdgdir_functionality.cpp
        ${PCMANFM_XDGDIR_SOURCES}
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MAGICKWAND MagickWand)
endif()

if(MAGICKWAND_FOUND)
    oneg4fm_add_test(oneg4fm-imagemagick-tests
        SOURCES
            test_imagemagick.cpp
            ../oneg4fm/imagemagick_support.cpp
        LIBS
            ${MAGICKWAND_LIBRARIES}
        INCLUDES
            ${MAGICKWAND_INCLUDE_DIRS}
    )
    target_compile_definitions(oneg4fm-imagemagick-tests PRIVATE HAVE_MAGICKWAND)
    target_compile_options(oneg4fm-imagemagick-tests PRIVATE ${MAGICKWAND_CFLAGS_OTHER})
endif()
