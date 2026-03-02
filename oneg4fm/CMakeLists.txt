find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(MAGICKWAND MagickWand)
endif()

set(oneg4fm_SRCS
    application.cpp
    application_lifecycle.cpp
    application_startup_plan.cpp
    application_window_orchestration.cpp
    oneg4fm.cpp
    mainwindow.cpp
    mainwindow_commands.cpp
    mainwindow_navigation_commands.cpp
    mainwindow_viewframe.cpp
    mainwindow_tabs.cpp
    mainwindow_fileops.cpp
    mainwindow_navigation.cpp
    mainwindow_view.cpp
    mainwindow_pathbar.cpp
    mainwindow_menus.cpp
    mainwindow_bookmarks.cpp
    mainwindow_dragdrop.cpp
    mainwindow_events.cpp
    tabpage.cpp
    tabbar.cpp
    statusbar.cpp
    view.cpp
    launcher.cpp
    imagemagick_support.cpp
    imagemagick_support.h
    image_viewer_window.cpp
    image_viewer_window.h
    imagemagick_qt.cpp
    imagemagick_qt.h
    preferencesdialog.cpp
    xdgdir.cpp
    connectserverdialog.cpp
    settings.cpp
    bulkrename.cpp
    createlauncherdialog.cpp
    hiddenshortcutsdialog.cpp
    # New backend files
    ../src/core/ifileops.cpp
    ../src/core/backend_registry.cpp
    ../src/backends/qt/qt_fileops.cpp
    ../src/backends/qt/qt_fileinfo.cpp
    ../src/backends/qt/qt_foldermodel.cpp
    ../src/core/file_ops_contract.cpp
    ../src/core/gio_ops_executor.cpp
    ../src/core/fs_ops.cpp
    ../src/core/linux_fs_safety.cpp
    ../src/core/archive_writer.cpp
    ../src/core/archive_extract.cpp
    ../src/core/windowed_file_reader.cpp
    ../src/ui/filepropertiesdialog.cpp
    ../src/ui/archivejob.cpp
    ../src/ui/archiveextractjob.cpp
    ../src/ui/hexdocument.cpp
    ../src/ui/hexeditorview.cpp
    ../src/ui/hexeditorwindow.cpp
    ../src/ui/binarydocument.cpp
    ../src/ui/color_manager.cpp
    ../src/ui/color_delegate.cpp
    ../src/ui/disasm_engine.cpp
    ../src/ui/disasmmodel.cpp
    ../src/ui/disassemblywindow.cpp
    ../src/ui/fsqt.cpp
    ../src/core/ifoldermodel.h
)

set(ONEG4FM_DBUS_APP_SERVICE "org.oneg4fm.oneg4fm")
set(ONEG4FM_DBUS_APP_INTERFACE "org.oneg4fm.Application")

qt6_add_dbus_adaptor(oneg4fm_DBUS_SRCS
    org.oneg4fm.Application.xml
    application.h
    Oneg4FM::Application
    applicationadaptor
    ApplicationAdaptor
)

qt6_add_dbus_adaptor(oneg4fm_DBUS_SRCS
    org.freedesktop.FileManager1.xml
    application.h
    Oneg4FM::Application
    applicationadaptorfreedesktopfilemanager
    ApplicationAdaptorFreeDesktopFileManager
)

# qt6_add_dbus_adaptor() already generated the moc files. It also marked the
# files with SKIP_AUTOMOC but we still need to mark them with SKIP_AUTOGEN.
# TODO: Check if this behaviour is a CMake bug.
set_source_files_properties(${oneg4fm_DBUS_SRCS} PROPERTIES SKIP_AUTOGEN ON)

set(oneg4fm_UIS
    main-win.ui
    about.ui
    shortcuts.ui
    preferences.ui
    connect.ui
    bulk-rename.ui
)

configure_file(
    oneg4fm.desktop.in
    "${CMAKE_CURRENT_BINARY_DIR}/oneg4fm.desktop"
    @ONLY
)
set(PCMANFM_DESKTOP_FILE "${CMAKE_CURRENT_BINARY_DIR}/oneg4fm.desktop")

configure_file(
    org.oneg4fm.oneg4fm.service.in
    "${CMAKE_CURRENT_BINARY_DIR}/org.oneg4fm.oneg4fm.service"
    @ONLY
)
set(ONEG4FM_DBUS_SERVICE_FILE "${CMAKE_CURRENT_BINARY_DIR}/org.oneg4fm.oneg4fm.service")

add_executable(oneg4fm
    ${oneg4fm_SRCS}
    ${oneg4fm_DBUS_SRCS}
    ${oneg4fm_UIS}
)

target_compile_definitions(oneg4fm
    PRIVATE
        PCMANFM_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/oneg4fm"
        PCMANFM_QT_VERSION="${PCMANFM_QT_VERSION}"
        LIBFM_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/libfm"
        ONEG4FM_DBUS_APP_SERVICE="${ONEG4FM_DBUS_APP_SERVICE}"
        ONEG4FM_DBUS_APP_INTERFACE="${ONEG4FM_DBUS_APP_INTERFACE}"
)

target_include_directories(oneg4fm
    PRIVATE
        "${Qt6Gui_PRIVATE_INCLUDE_DIRS}"
        "${CMAKE_CURRENT_BINARY_DIR}/oneg4fm_autogen/include"
        ../src
        ${BLAKE3_INCLUDE_DIRS}
        ${LIBARCHIVE_INCLUDE_DIRS}
        ${CAPSTONE_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(oneg4fm
    Qt6::Widgets
    Qt6::DBus
    Qt6::Concurrent
    fm-qt6
    ${BLAKE3_LIBRARIES}
    ${LIBARCHIVE_LIBRARIES}
    ${CAPSTONE_LIBRARIES}
)

if(MAGICKWAND_FOUND)
    target_include_directories(oneg4fm PRIVATE ${MAGICKWAND_INCLUDE_DIRS})
    target_link_libraries(oneg4fm ${MAGICKWAND_LIBRARIES})
    target_compile_options(oneg4fm PRIVATE ${MAGICKWAND_CFLAGS_OTHER})
    target_compile_definitions(oneg4fm PRIVATE HAVE_MAGICKWAND)
endif()

install(TARGETS oneg4fm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# install a desktop entry file for oneg4fm and desktop preferences
install(FILES "${PCMANFM_DESKTOP_FILE}" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
install(FILES "${ONEG4FM_DBUS_SERVICE_FILE}" DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/services")

# prevent the generated files from being deleted during make clean
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM true)
