cmake_minimum_required(VERSION 3.20)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(PLUGINS REQUIRED usefull_macros)

include_directories(${PLUGINS_INCLUDE_DIRS} ..)
link_directories(${PLUGINSLIBRARY_DIRS})
link_libraries(${$PLUGINS_LIBRARIES} -fPIC)

set(LIBS "")

if(DUMMY)
    add_library(wsdummy SHARED dummy.c)
    list(APPEND LIBS wsdummy)
endif(DUMMY)

install(TARGETS ${LIBS} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
