project(examples)

# common includes & library
include_directories(../)
link_libraries(astrotools usefull_macros erfa)

# exe list
add_executable(coordstest coordstest.c)
add_executable(transform transform.c)
