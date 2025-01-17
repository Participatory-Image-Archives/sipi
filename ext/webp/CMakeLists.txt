# https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.1.0.tar.gz
cmake_minimum_required(VERSION 3.14.5)
project(libwebp_builder C CXX)
include(ExternalProject)

#
# libwebp
#
ExternalProject_Add(project_webp
        DEPENDS xz
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.1.0.tar.gz
        SOURCE_DIR ${COMMON_SRCS}/libwebp-1.1.0
        CMAKE_ARGS -DCMAKE_PREFIX_PATH=${COMMON_LOCAL}
        -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
        )
ExternalProject_Get_Property(project_webp install_dir)

if(MAKE_SHARED_SIPI)
    add_library(webp SHARED IMPORTED GLOBAL)
    set_property(TARGET webp PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libwebp${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(webp STATIC IMPORTED GLOBAL)
    set_property(TARGET webp PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(webp project_webp)
