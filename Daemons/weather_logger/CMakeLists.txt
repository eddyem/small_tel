cmake_minimum_required(VERSION 4.0)
set(PROJ meteologger)
set(MINOR_VERSION "1")
set(MID_VERSION "0")
set(MAJOR_VERSION "0")
set(VERSION "${MAJOR_VERSION}.${MID_VERSION}.${MINOR_VERSION}")

project(${PROJ} VERSION ${VERSION} LANGUAGES C)
message("${PROJ} version: ${VERSION}")

option(DEBUG "Compile in debug mode" OFF)
# option()

# default flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -W -Wextra -fPIC")
set(CMAKE_COLOR_MAKEFILE ON)

# here is one of two variants: all .c in directory or .c files in list
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)
#list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/<file to remove>)
#set(SOURCES list_of_c_files)

# we can change file list
#if(NOT DEFINED something)
#	set(SOURCES ${SOURCES} one_more_list)
#	add_definitions(-DSOME_DEFS)
#endif()

# cmake -DDEBUG=1 -> debugging
if(DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Og -g3 -ggdb -Werror")
    add_definitions(-DEBUG)
    set(CMAKE_BUILD_TYPE DEBUG)
    set(CMAKE_VERBOSE_MAKEFILE "ON")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=native -fdata-sections -ffunction-sections -flto=auto")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -flto=auto")
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
###### pkgconfig ######
find_package(PkgConfig REQUIRED)
pkg_check_modules(${PROJ} REQUIRED usefull_macros>=0.3.5)

# external modules like OpenMP:
#include(FindOpenMP)
#if(OPENMP_FOUND)
#	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
#	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
#	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
#endif()

# append data from find_package:
#list(APPEND ${PROJ}_INCLUDE_DIRS ${<pkg1>_INCLUDE_DIR} ${<pkg2>_INCLUDE_DIR})
#list(APPEND ${PROJ}_LIBRARIES ${<pkg1>_LIBRARY} ${<pkg2>_LIBRARY})
#list(APPEND ${${PROJ}_LIBRARY_DIRS}  ${<pkg1>_LIBRARY_DIRS})

# static library
#set(LIBSRC fd.c weathlib.c)
#set(LIBHEADER weathlib.h)
#add_library(${PROJLIB} STATIC ${LIBSRC})
#set_target_properties(${PROJLIB} PROPERTIES VERSION ${VERSION})

# exe file
add_executable(${PROJ} ${SOURCES})
# another exe, depending on some other files
#add_executable(test_client client.c usefull_macros.c parceargs.c)
# -I
include_directories(${${PROJ}_INCLUDE_DIRS})
# -L
link_directories(${${PROJ}_LIBRARY_DIRS})
# -D
add_definitions(-D_XOPEN_SOURCE=1234 -D_DEFAULT_SOURCE -D_GNU_SOURCE
        -DPACKAGE_VERSION=\"${VERSION}\" -DMINOR_VERSION=\"${MINOR_VERSION}\" -DMID_VERSION=\"${MID_VERSION}\"
        -DMAJOR_VERSION=\"${MAJOR_VERSION}\")

###### pthreads ######
#find_package(Threads REQUIRED)
#if(THREADS_HAVE_PTHREAD_ARG)
#  set_property(TARGET ${PROJ} PROPERTY COMPILE_OPTIONS "-pthread")
#  set_property(TARGET ${PROJ} PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
#endif()
#if(CMAKE_THREAD_LIBS_INIT)
#  list(APPEND ${PROJ}_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}")
#endif()

# target libraries
target_link_libraries(${PROJ} ${${PROJ}_LIBRARIES} ${CMAKE_DL_LIBS})

# install
include(GNUInstallDirs)
install(TARGETS ${PROJ} DESTINATION "bin")
# Script to be executed at installation time (kind of post-intallation script) to
# change the right accesses on the installed files
#INSTALL(SCRIPT inst.cmake)

# subdirs
#add_subdirectory("plugins")
