set(libfm_core_SRCS
    # gio gvfs implementations
    core/vfs/fm-file.c
    core/vfs/fm-file.h
    core/vfs/fm-xml-file.c
    core/vfs/fm-xml-file.h
    core/vfs/vfs-search.c
    # other legacy C code
    core/legacy/fm-config.c
    core/legacy/fm-app-info.c
    # core data structures
    core/gobjectptr.h
    core/filepath.cpp
    core/fileops_bridge_policy.cpp
    core/iconinfo.cpp
    core/mimetype.cpp
    core/fileinfo.cpp
    core/folder.cpp
    core/folderconfig.cpp
    core/filemonitor.cpp
    # i/o jobs
    core/job.cpp
    core/filetransferjob.cpp
    core/deletejob.cpp
    core/dirlistjob.cpp
    core/filechangeattrjob.cpp
    core/fileinfojob.cpp
    core/filelinkjob.cpp
    core/fileoperationjob.cpp
    core/filesysteminfojob.cpp
    core/job.cpp
    core/totalsizejob.cpp
    core/trashjob.cpp
    core/untrashjob.cpp
    core/thumbnailjob.cpp
    # extra desktop services
    core/bookmarks.cpp
    core/basicfilelauncher.cpp
    core/volumemanager.cpp
    core/userinfocache.cpp
    core/thumbnailer.cpp
    core/terminal.cpp
    core/archiver.cpp
    core/templates.cpp
    # custom actions
    customactions/fileaction.cpp
    customactions/fileactionprofile.cpp
    customactions/fileactioncondition.cpp
)

set(LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT 0)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/file_ops_contract.cpp" AND DEFINED BLAKE3_LIBRARIES)
    set(LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT 1)
    list(APPEND libfm_core_SRCS
        ../../src/core/file_ops_contract.cpp
        ../../src/core/fs_ops.cpp
        ../../src/core/linux_fs_safety.cpp
    )
endif()

set(libfm_SRCS
    ${libfm_core_SRCS}
    libfmqt.cpp
    bookmarkaction.cpp
    sidepane.cpp
    filelauncher.cpp
    foldermodel.cpp
    foldermodelitem.cpp
    cachedfoldermodel.cpp
    proxyfoldermodel.cpp
    folderview.cpp
    folderitemdelegate.cpp
    createnewmenu.cpp
    filemenu.cpp
    foldermenu.cpp
    filepropsdialog.cpp
    applaunchcontext.cpp
    placesview.cpp
    placesmodel.cpp
    placesmodelitem.cpp
    dirtreeview.cpp
    dirtreemodel.cpp
    dirtreemodelitem.cpp
    dnddest.cpp
    mountoperation.cpp
    mountoperationpassworddialog.cpp
    mountoperationquestiondialog.cpp
    fileoperation.cpp
    fileoperationdialog.cpp
    renamedialog.cpp
    pathedit.cpp
    pathbar.cpp
    colorbutton.cpp
    fontbutton.cpp
    browsehistory.cpp
    utilities.cpp
    dndactionmenu.cpp
    editbookmarksdialog.cpp
    execfiledialog.cpp
    appchoosercombobox.cpp
    appchooserdialog.cpp
    filesearchdialog.cpp
    filedialog.cpp
    fm-search.c # might be moved to libfm later
    xdndworkaround.cpp
    filedialoghelper.cpp
)

set(libfm_UIS
    file-props.ui
    file-operation-dialog.ui
    rename-dialog.ui
    mount-operation-password.ui
    edit-bookmarks.ui
    exec-file.ui
    app-chooser-dialog.ui
    filesearch.ui
    filedialog.ui
)

set(LIBFM_QT_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/libfm-qt6")
set(LIBFM_QT_INTREE_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/include")

add_library(${LIBFM_QT_LIBRARY_NAME} SHARED
    ${libfm_SRCS}
    ${libfm_UIS}
)

install(EXPORT
    "${LIBFM_QT_LIBRARY_NAME}-targets"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/${LIBFM_QT_LIBRARY_NAME}"
    COMPONENT Devel
)

target_link_libraries(${LIBFM_QT_LIBRARY_NAME}
    PRIVATE
        Qt6::GuiPrivate
    PUBLIC
        Qt6::Widgets
        ${GLIB_LIBRARIES}
        ${GLIB_GIO_LIBRARIES}
        ${GLIB_GOBJECT_LIBRARIES}
        ${GLIB_GTHREAD_LIBRARIES}
        ${XCB_LIBRARIES}
        ${EXIF_LIBRARIES}
)
if(LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT)
    target_link_libraries(${LIBFM_QT_LIBRARY_NAME} PRIVATE ${BLAKE3_LIBRARIES})
endif()

# set libtool soname
set_target_properties(${LIBFM_QT_LIBRARY_NAME} PROPERTIES
    VERSION ${LIBFM_QT_ABI_VERSION}
    SOVERSION ${LIBFM_QT_SOVERSION}
)

target_include_directories(${LIBFM_QT_LIBRARY_NAME}
    PRIVATE "${Qt6Gui_PRIVATE_INCLUDE_DIRS}"
        ${BLAKE3_INCLUDE_DIRS}
        core/legacy
        "${CMAKE_CURRENT_BINARY_DIR}"
    PUBLIC
        "${GLIB_INCLUDE_DIRS}"
        "${GLIB_GIO_UNIX_INCLUDE_DIRS}"
        "${XCB_INCLUDE_DIRS}"
        "${EXIF_INCLUDE_DIRS}"
    INTERFACE
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        "$<BUILD_INTERFACE:${LIBFM_QT_INTREE_INCLUDE_DIR}>"
)

get_property(_fmqt_interface_includes TARGET ${LIBFM_QT_LIBRARY_NAME} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
list(REMOVE_ITEM _fmqt_interface_includes "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/")
set_property(TARGET ${LIBFM_QT_LIBRARY_NAME} PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_fmqt_interface_includes}")
message(STATUS "fm-qt6 interface includes: ${_fmqt_interface_includes}")

target_compile_definitions(${LIBFM_QT_LIBRARY_NAME}
    PRIVATE "LIBFM_QT_DATA_DIR=\"${LIBFM_QT_DATA_DIR}\""
            "GETTEXT_PACKAGE=\"\""
            "LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT=${LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT}"
    PUBLIC "QT_NO_KEYWORDS"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${LIBFM_QT_LIBRARY_NAME}_export.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/libfm-qt6"
    COMPONENT Devel
)

# install include header files (FIXME: can we make this cleaner? should dir name be versioned?)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/libfm-qt6"
    COMPONENT Devel
    FILES_MATCHING PATTERN "*.h"
)

generate_export_header(${LIBFM_QT_LIBRARY_NAME}
    EXPORT_MACRO_NAME LIBFM_QT_API
)

# InTree build
file(COPY ${CMAKE_CURRENT_BINARY_DIR}/${LIBFM_QT_LIBRARY_NAME}_export.h
    DESTINATION "${LIBFM_QT_INTREE_INCLUDE_DIR}/libfm-qt6"
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION "${LIBFM_QT_INTREE_INCLUDE_DIR}/libfm-qt6"
    FILES_MATCHING PATTERN "*.h"
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/fm-qt-config.cmake.in"
    "${CMAKE_BINARY_DIR}/${LIBFM_QT_LIBRARY_NAME}-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/${LIBFM_QT_LIBRARY_NAME}"
)

install(FILES
    "${CMAKE_BINARY_DIR}/${LIBFM_QT_LIBRARY_NAME}-config.cmake"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/${LIBFM_QT_LIBRARY_NAME}"
    COMPONENT Devel
)

# FIXME: add libtool version to the lib (soname) later.
# FIXME: only export public symbols

install(TARGETS ${LIBFM_QT_LIBRARY_NAME}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    EXPORT "${LIBFM_QT_LIBRARY_NAME}-targets"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    PUBLIC_HEADER
    COMPONENT Runtime
)

export(TARGETS ${LIBFM_QT_LIBRARY_NAME}
    FILE "${CMAKE_BINARY_DIR}/${LIBFM_QT_LIBRARY_NAME}-targets.cmake"
    EXPORT_LINK_INTERFACE_LIBRARIES
)

# install a pkgconfig file for libfm-qt
set(REQUIRED_QT "Qt6Widgets >= ${QT_MINIMUM_VERSION}")
configure_file(libfm-qt.pc.in lib${LIBFM_QT_LIBRARY_NAME}.pc @ONLY)
# FreeBSD loves to install files to different locations
# https://www.freebsd.org/doc/handbook/dirstructure.html
if(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/lib${LIBFM_QT_LIBRARY_NAME}.pc"
        DESTINATION libdata/pkgconfig
        COMPONENT Devel
    )
else()
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/lib${LIBFM_QT_LIBRARY_NAME}.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        COMPONENT Devel
    )
endif()

# prevent the generated files from being deleted during make cleaner
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM true)


if(BUILD_TESTING)
    set(TEST_LIBRARIES
        Qt6::Core
        Qt6::Widgets
        ${FM_LIBRARIES}
        ${LIBFM_QT_LIBRARY_NAME}
    )
    # some simple test cases
    add_executable("test-folder"
        tests/test-folder.cpp
    )
    target_link_libraries("test-folder" ${TEST_LIBRARIES})

    add_executable("test-folderview"
        tests/test-folderview.cpp
    )
    target_link_libraries("test-folderview" ${TEST_LIBRARIES})

    add_executable("test-filedialog"
        tests/test-filedialog.cpp
    )
    target_link_libraries("test-filedialog" ${TEST_LIBRARIES})

    add_executable("test-volumemanager"
        tests/test-volumemanager.cpp
    )
    target_link_libraries("test-volumemanager" ${TEST_LIBRARIES})

    add_executable("test-placesview"
        tests/test-placesview.cpp
    )
    target_link_libraries("test-placesview" ${TEST_LIBRARIES})

    set(_fmqt_test_targets
        test-folder
        test-folderview
        test-filedialog
        test-volumemanager
        test-placesview
    )

    foreach(test_target IN LISTS _fmqt_test_targets)
        target_include_directories(${test_target} PRIVATE
            "${LIBFM_QT_INTREE_INCLUDE_DIR}/libfm-qt6"
            "${CMAKE_CURRENT_BINARY_DIR}"
        )

        add_test(NAME ${test_target} COMMAND ${test_target})
        set_tests_properties(${test_target} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FM_QT_DEMO_TEST_QUIT_MS=300"
            TIMEOUT 20
        )
    endforeach()
endif()
