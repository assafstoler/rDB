CMAKE_MINIMUM_REQUIRED(VERSION 2.8 FATAL_ERROR)

PROJECT(rdbfw)


#SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
#ADD_DEFINITIONS(-DPACKAGE_VERSION="${PACKAGE_VERSION}")
#ADD_DEFINITIONS(-DPACKAGE_STRING="${PACKAGE_STRING}")

#SET (MAIN_DIR ${CMAKE_SOURCE_DIR})
#get_filename_component(MAIN_DIR ${CMAKE_SOURCE_DIR} PATH)
#SET(TEST_DIR ${MAIN_DIR}/t)

#MESSAGE(---> ${MAIN_DIR})

#INCLUDE_DIRECTORIES(${MAIN_DIR})
#INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR}) # for generated files


ADD_DEFINITIONS(-DUSE_MSG_BUFFERS)

#This will count how many messages (events) each module got.
ADD_DEFINITIONS(-DMSG_ACCOUNTING)

ADD_DEFINITIONS(-DWAKEUP_ACCOUNTING)


ADD_EXECUTABLE(rdbfw rdbfw.c messaging.c utils.c rdbfw.h)
target_link_libraries (rdbfw pthread dl rdb_beta)
#target_compile_definitions(rdbfw-individual PRIVATE -DINDIVIDUAL_MESSAGING_POOL)

ADD_LIBRARY(event_skeleton SHARED event_driven_skeleton.c)
target_link_libraries (event_skeleton pthread dl rdb_beta)

ADD_LIBRARY(skeleton SHARED skeleton.c)
target_link_libraries (skeleton pthread dl rdb_beta)

ADD_LIBRARY(timers SHARED timers.c)
target_link_libraries (timers pthread dl rdb_beta)

