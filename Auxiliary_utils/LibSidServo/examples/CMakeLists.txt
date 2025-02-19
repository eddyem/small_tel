project(examples)

# common includes & library
include_directories(../)
link_libraries(sidservo usefull_macros -lm)

# exe list
add_executable(goto goto.c dump.c)
add_executable(dump dumpmoving.c dump.c)
add_executable(dump_s dumpmoving_scmd.c dump.c)
add_executable(dumpswing dumpswing.c dump.c)
add_executable(traectory_s scmd_traectory.c dump.c traectories.c)
