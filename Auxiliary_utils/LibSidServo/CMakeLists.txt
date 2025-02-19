cmake_minimum_required(VERSION 3.30)
set(PROJ sidservo)
set(MINOR_VERSION "1")
set(MID_VERSION "0")
set(MAJOR_VERSION "0")
set(VERSION "${MAJOR_VERSION}.${MID_VERSION}.${MINOR_VERSION}")

project(${PROJ} VERSION ${VERSION} LANGUAGES C)

# default flags
set(CMAKE_C_FLAGS "${CFLAGS} -O2")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -Wextra -Wall -Werror -W")
set(CMAKE_COLOR_MAKEFILE ON)

option(DEBUG "Compile in debug mode" OFF)
option(EXAMPLES "Compile also some examples" ON)

# cmake -DDEBUG=on -> debugging
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(DEBUG)
        set(CMAKE_BUILD_TYPE "Debug")
    else()
        set(CMAKE_BUILD_TYPE "Release")
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_definitions(-DEBUG)
	set(CMAKE_VERBOSE_MAKEFILE true)
	if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		message("install to ${CMAKE_CURRENT_SOURCE_DIR}/install ")
		set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/install)
	endif()
	set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS_DEBUG})
else()
	set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS_RELEASE})
endif()

message("Build type: ${CMAKE_BUILD_TYPE}, cflags: ${CMAKE_C_FLAGS}")

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)

###### pkgconfig ######
# pkg-config modules (for pkg-check-modules)
#set(MODULES cfitsio fftw3)
# find packages:
#find_package(PkgConfig REQUIRED)
#pkg_check_modules(${PROJ} REQUIRED ${MODULES})

###### additional flags ######
#list(APPEND ${PROJ}_LIBRARIES "-lfftw3_threads")

# library
add_library(${PROJ} SHARED ${SOURCES})
# library header files
set(LIBHEADER "sidservo.h")
# -I
include_directories(${${PROJ}_INCLUDE_DIRS})
# -L
link_directories(${${PROJ}_LIBRARY_DIRS})
# -D
add_definitions(
        -DPACKAGE_VERSION=\"${VERSION}\" -DMINOR_VERSION=\"${MINOR_VERSION}\"
        -DMID_VERSION=\"${MID_VERSION}\" -DMAJOR_VERSION=\"${MAJOR_VESION}\"
)

# -l
target_link_libraries(${PROJ} ${${PROJ}_LIBRARIES})

set(PCFILE "${CMAKE_BINARY_DIR}/${PROJ}.pc")
configure_file("${PROJ}.pc.in" ${PCFILE} @ONLY)

set_target_properties(${PROJ} PROPERTIES VERSION ${VERSION})
set_target_properties(${PROJ} PROPERTIES PUBLIC_HEADER ${LIBHEADER})

# Installation of the program
include(GNUInstallDirs)
#install(TARGETS ${PROJ} DESTINATION "bin")
install(TARGETS ${PROJ} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${PCFILE} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

# EXAMPLES
if(EXAMPLES)
    add_subdirectory(examples)
endif()
