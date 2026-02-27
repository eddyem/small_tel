cmake_minimum_required(VERSION 3.20)
set(PROJ weatherdaemon)
set(MAJOR_VERSION "0")
set(MID_VERSION "0")
set(MINOR_VERSION "1")

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)
#set(SOURCES main.c cmdlnopts.c sensors.c)

set(VERSION "${MAJOR_VERSION}.${MID_VERSION}.${MINOR_VERSION}")
project(${PROJ} VERSION ${VERSION} LANGUAGES C)
message("VER: ${VERSION}")

# list of options
option(DEBUG "Compile in debug mode" OFF)
option(DUMMY "Dummy device plugin" ON)
option(FDEXAMPLE "Example of file descriptor plugin" ON)


# default flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -W -Wextra -fno-builtin-strlen")

message("Install dir prefix: ${CMAKE_INSTALL_PREFIX}")

add_definitions(-D_XOPEN_SOURCE=1234 -D_DEFAULT_SOURCE -D_GNU_SOURCE
        -DPACKAGE_VERSION=\"${VERSION}\" -DMINOR_VERSION=\"${MINOR_VERSION}\" -DMID_VERSION=\"${MID_VERSION}\"
        -DMAJOR_VERSION=\"${MAJOR_VESION}\")

set(CMAKE_COLOR_MAKEFILE ON)

# cmake -DDEBUG=on -> debugging
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
find_package(PkgConfig REQUIRED)
pkg_check_modules(${PROJ} REQUIRED usefull_macros>=0.3.2)

#include(FindOpenMP)
#if(OPENMP_FOUND)
#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
#    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
#    add_definitions(-DOMP_FOUND)
#endif()

# exe & lib files
add_executable(${PROJ} ${SOURCES})
target_link_libraries(${PROJ} ${${PROJ}_LIBRARIES} -lm ${CMAKE_DL_LIBS})
target_include_directories(${PROJ} PUBLIC ${${PROJ}_INCLUDE_DIRS} .)
target_link_directories(${PROJ} PUBLIC ${${PROJ}_LIBRARY_DIRS} )
set_target_properties(${PROJLIB} PROPERTIES VERSION ${VERSION})

include(GNUInstallDirs)
# Installation of the program
install(TARGETS ${PROJ} DESTINATION "bin")

add_subdirectory("plugins")
