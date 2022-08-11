project (rdb.ko)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -g")

include_directories ("${PROJECT_SOURCE_DIR}/src")

link_directories ("${PROJECT_SOURCE_DIR}/")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -export-dynamic -avoid-version -g")
 
if (${USE_128_BIT_TYPES})
add_custom_command(
OUTPUT rdb.ko
DEPENDS rdb.c
COMMAND cp ${CMAKE_SOURCE_DIR}/kernel/Kbuild.64 ${CMAKE_SOURCE_DIR}/kernel/Kbuild
COMMAND ${CMAKE_SOURCE_DIR}/kernel/make_modules.sh
)

add_custom_target(modules ALL
DEPENDS rdb.ko)
else  (${USE_128_BIT_TYPES})
add_custom_command(
OUTPUT rdb.ko
DEPENDS rdb.c
COMMAND cp ${CMAKE_SOURCE_DIR}/kernel/Kbuild.32 ${CMAKE_SOURCE_DIR}/kernel/Kbuild
COMMAND ${CMAKE_SOURCE_DIR}/kernel/make_modules.sh
)

add_custom_target(modules ALL
DEPENDS rdb.ko)
endif (${USE_128_BIT_TYPES})

########### install files ###############
#install(TARGETS rdb_beta DESTINATION /var/alertlogic/lib)
