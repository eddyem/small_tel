cmake_minimum_required(VERSION 3.20)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(PLUGINS REQUIRED usefull_macros)

include_directories(${PLUGINS_INCLUDE_DIRS} ..)
link_directories(${PLUGINS_LIBRARY_DIRS} ..)
link_libraries(${$PLUGINS_LIBRARIES} ${PROJLIB} -fPIC)

set(LIBS "")

if(DUMMY)
    add_library(wsdummy SHARED dummy.c)
    list(APPEND LIBS wsdummy)
endif()

if(FDEXAMPLE)
    add_library(fdex SHARED fdexample.c)
    list(APPEND LIBS fdex)
endif()

if(HYDREON)
    add_library(hydreon SHARED hydreon.c)
    list(APPEND LIBS hydreon)
endif()

if(BTAMETEO)
    add_library(btameteo SHARED btameteo.c bta_shdata.c) 
    target_link_libraries(btameteo -lcrypt)
    list(APPEND LIBS btameteo)
endif()

if(REINHARDT)
    add_library(reinhardt SHARED reinhardt.c) 
    list(APPEND LIBS reinhardt)
endif()

if(WXA100)
    add_library(wxa100 SHARED wxa100.c) 
    list(APPEND LIBS wxa100)
endif()

install(TARGETS ${LIBS} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
