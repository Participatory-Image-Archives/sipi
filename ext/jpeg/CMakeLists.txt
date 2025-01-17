cmake_minimum_required(VERSION 3.14.5)
project(jpeg_builder C CXX)
include(ExternalProject)

#
# get libjpeg 1.4.1
#
ExternalProject_Add(project_jpeg
    INSTALL_DIR ${COMMON_LOCAL}
    URL https://github.com/dasch-swiss/sipi-third-party/raw/main/third-party/jpegsrc.v9c.tar.gz
    URL_HASH SHA256=650250979303a649e21f87b5ccd02672af1ea6954b911342ea491f351ceb7122
    SOURCE_DIR ${COMMON_SRCS}/libjpeg-v9c
    CONFIGURE_COMMAND ${COMMON_SRCS}/libjpeg-v9c/configure --prefix=${COMMON_LOCAL} --enable-shared --enable-static --libdir=${CONFIGURE_LIBDIR}
    BUILD_COMMAND make
    BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_jpeg install_dir)

if(MAKE_SHARED_SIPI)
    add_library(jpeg SHARED IMPORTED GLOBAL)
    set_property(TARGET jpeg PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libjpeg${CMAKE_SHARED_LIBRARY_SUFFIX})
    set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=ON")
else()
    add_library(jpeg STATIC IMPORTED GLOBAL)
    set_property(TARGET jpeg PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libjpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
    set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=OFF")
endif()

add_dependencies(jpeg project_jpeg)
