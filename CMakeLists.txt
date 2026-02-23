cmake_minimum_required(VERSION 3.18.0 FATAL_ERROR)
# CMP0000: Call the cmake_minimum_required() command at the beginning of the top-level
# CMakeLists.txt file even before calling the project() command.
# The cmake_minimum_required(VERSION) command implicitly invokes the cmake_policy(VERSION)
# command to specify that the current project code is written for the given range of CMake
# versions.
project(oneg4fm)

# PcmanFm-Qt Version
set(PCMANFM_QT_VERSION_MAJOR 2)
set(PCMANFM_QT_VERSION_MINOR 3)
set(PCMANFM_QT_VERSION_PATCH 0)

set(PCMANFM_QT_VERSION ${PCMANFM_QT_VERSION_MAJOR}.${PCMANFM_QT_VERSION_MINOR}.${PCMANFM_QT_VERSION_PATCH})

option(BUILD_TESTING "Build test targets" OFF)
include(CTest)
if(BUILD_TESTING)
    enable_testing()
endif()

# Minimum versions
set(LIBFMQT_MINIMUM_VERSION "2.3.0")
set(QT_MINIMUM_VERSION "6.6.0")

find_package(Qt6DBus ${QT_MINIMUM_VERSION} REQUIRED)
find_package(Qt6LinguistTools ${QT_MINIMUM_VERSION} REQUIRED)
find_package(Qt6Widgets ${QT_MINIMUM_VERSION} REQUIRED)
find_package(Qt6Concurrent ${QT_MINIMUM_VERSION} REQUIRED)

# pkg-config dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLAKE3 REQUIRED blake3)
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
pkg_check_modules(CAPSTONE REQUIRED capstone)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    @ONLY
)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

message(STATUS "Building ${PROJECT_NAME} with Qt ${Qt6Core_VERSION}")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Please use a separate build directory.")
endif()

option(UPDATE_TRANSLATIONS "Update source translation translations/*.ts files" OFF)
include(GNUInstallDirs)

set(CMAKE_AUTOMOC TRUE)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_link_options(-Wno-fuse-ld-path)
endif()

add_subdirectory(libfm)
add_subdirectory(libfm-qt)
add_subdirectory(oneg4fm)

# manpage for oneg4fm (optional in development trees)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/oneg4fm.1.in")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/oneg4fm.1.in"
        "${CMAKE_CURRENT_BINARY_DIR}/oneg4fm.1"
        @ONLY
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/oneg4fm.1"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
    )
else()
    message(STATUS "Skipping manpage generation: oneg4fm.1.in not found")
endif()

# add Doxygen support to generate API docs
# References:
# http://majewsky.wordpress.com/2010/08/14/tip-of-the-day-cmake-and-doxygen/
# http://www.bluequartz.net/projects/EIM_Segmentation/SoftwareDocumentation/html/usewithcmakeproject.html
option(BUILD_DOCUMENTATION "Use Doxygen to create the HTML based API documentation" OFF)
if(BUILD_DOCUMENTATION)
    find_package(Doxygen REQUIRED)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)
    add_custom_target(doc ALL
        ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()

install(
    FILES
        icons/oneg4fm.svg
    DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps"
)

add_subdirectory(config)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
