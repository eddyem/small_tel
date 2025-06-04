cmake_minimum_required(VERSION 3.30)

set(PROJ domedaemon)
project(${PROJ})

set(MINOR_VERSION "0")
set(MID_VERSION "1")
set(MAJOR_VERSION "0")
set(VERSION "${MAJOR_VERSION}.${MID_VERSION}.${MINOR_VERSION}")

enable_language(C)
message("VER: ${VERSION}")

# options
option(DEBUG "Compile in debug mode" OFF)

# default flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -W -Wextra -std=gnu99")
if(DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Og -g3 -ggdb -fno-builtin-strlen -Werror")
    add_definitions(-DEBUG)
    set(CMAKE_BUILD_TYPE DEBUG)
    set(CMAKE_VERBOSE_MAKEFILE "ON")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=native -fdata-sections -ffunction-sections -fno-builtin-strlen ")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_COLOR_MAKEFILE ON)

# here is one of two variants: all .c in directory or .c files in list
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)

###### pkgconfig ######
find_package(PkgConfig REQUIRED)
pkg_check_modules(MODULES REQUIRED usefull_macros>=0.3.2)

# change wrong behaviour with install prefix
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND CMAKE_INSTALL_PREFIX MATCHES "/usr/local")
else()
    message("Change default install path to /usr/local")
    set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
message("Install dir prefix: ${CMAKE_INSTALL_PREFIX}")

# executable file
add_executable(${PROJ} ${SOURCES})
# -I
target_include_directories(${PROJ} PUBLIC ${MODULES_INCLUDE_DIRS})
# -L
target_link_directories(${PROJ} PUBLIC ${MODULES_LIBRARY_DIRS})
# -l
target_link_libraries(${PROJ} ${MODULES_LIBRARIES})
# -D
add_definitions(
		-DPACKAGE_VERSION=\"${VERSION}\" -DMINOR_VERSION=\"${MINOR_VERSION}\" 
		-DMID_VERSION=\"${MID_VERSION}\" -DMAJOR_VERSION=\"${MAJOR_VESION}\"
		)

# Installation of the program
INSTALL(TARGETS ${PROJ} DESTINATION "bin")
