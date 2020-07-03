find_program(
    MEMORYCHECK_COMMAND
    valgrind )
set( MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full" )

message (${MEMORYCHECK_COMMAND})

project ( rdbfw_test )
include ( CTest )


add_executable ( fn_test fn_test.c ../utils.c )
target_link_libraries( fn_test m )
target_include_directories ( fn_test PRIVATE $<TARGET_PROPERTY:rdbfwlib,INCLUDE_DIRECTORIES> )
INSTALL(TARGETS fn_test
        DESTINATION /usr/local/bin
)

ADD_LIBRARY ( event_skeleton SHARED event_driven_skeleton.c )
target_link_libraries ( event_skeleton PRIVATE pthread dl rdb_beta )
target_include_directories ( event_skeleton PRIVATE $<TARGET_PROPERTY:rdbfwlib,INCLUDE_DIRECTORIES> )
INSTALL(TARGETS event_skeleton
    LIBRARY DESTINATION /usr/local/lib
    PUBLIC_HEADER DESTINATION /usr/local/include/rdbfw
    )


add_executable ( fw_test fw_test.c )
#target_include_directories(fw_test PRIVATE ${HEADERS})
target_link_libraries(fw_test m rdb_beta rt rdbfwlib)
INSTALL(TARGETS fw_test
    DESTINATION /usr/local/bin
    )


enable_testing()

add_test (rdbfw_test_timespec_math_1 fn_test -t 1 -f 5.0 -o 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_1
    PROPERTIES PASS_REGULAR_EXPRESSION "^2.500000000\n2500\n2500\n$")

add_test (rdbfw_test_timespec_math_2 fn_test -t 1 -f 5.500000000 -o 7.300000000)
set_tests_properties (rdbfw_test_timespec_math_2
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.800000000\n1800\n1800\n$")

add_test (rdbfw_test_timespec_math_3 fn_test -t 1 -f 5.500000000 -o 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_3
    PROPERTIES PASS_REGULAR_EXPRESSION "^2.000000000\n2000\n2000\n$")

add_test (rdbfw_test_timespec_math_4 fn_test -t 1 -f 5.500000000 -o 7.700000000)
set_tests_properties (rdbfw_test_timespec_math_4
    PROPERTIES PASS_REGULAR_EXPRESSION "^2.200000000\n2200\n2200\n$")

add_test (rdbfw_test_timespec_math_5 fn_test -t 1 -f 5.500000000 -o 5.300000000)
set_tests_properties (rdbfw_test_timespec_math_5
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_6 fn_test -t 1 -f 5.500000000 -o 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_6
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_7 fn_test -t 1 -f 5.500000000 -o 5.700000000)
set_tests_properties (rdbfw_test_timespec_math_7
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_8 fn_test -t 1 -f 0.500000000 -o 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_8
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_9 fn_test -t 1 -f 0.500000000 -o 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_9
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_10 fn_test -t 1 -f 0.500000000 -o 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_10
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_11 fn_test -t 1 -f 0.-500000000 -o 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_11
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_12 fn_test -t 1 -f 0.-500000000 -o 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_12
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_13 fn_test -t 1 -f 0.-500000000 -o 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_13
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_14 fn_test -t 1 -f 0.-500000000 -o 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_14
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.800000000\n800\n800\n$")

add_test (rdbfw_test_timespec_math_15 fn_test -t 1 -f 0.-500000000 -o 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_15
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.000000000\n1000\n1000\n$")

add_test (rdbfw_test_timespec_math_16 fn_test -t 1 -f 0.-500000000 -o 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_16
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.200000000\n1200\n1200\n$")

add_test (rdbfw_test_timespec_math_17 fn_test -t 1 -f 0.500000000 -o 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_17
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.800000000\n-800\n-800\n$")

add_test (rdbfw_test_timespec_math_18 fn_test -t 1 -f 0.500000000 -o 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_18
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.000000000\n-1000\n-1000\n$")

add_test (rdbfw_test_timespec_math_19 fn_test -t 1 -f 0.500000000 -o 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_19
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.200000000\n-1200\n-1200\n$")


add_test (rdbfw_test_timespec_math_20 fn_test -t 1 -f -5.500000000 -o 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_20
    PROPERTIES PASS_REGULAR_EXPRESSION "^5.800000000\n5800\n5800\n$")

add_test (rdbfw_test_timespec_math_21 fn_test -t 1 -f -5.500000000 -o 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_21
    PROPERTIES PASS_REGULAR_EXPRESSION "^6.000000000\n6000\n6000\n$")

add_test (rdbfw_test_timespec_math_22 fn_test -t 1 -f -5.500000000 -o 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_22
    PROPERTIES PASS_REGULAR_EXPRESSION "^6.200000000\n6200\n6200\n$")

add_test (rdbfw_test_timespec_math_23 fn_test -t 1 -f -5.500000000 -o 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_23
    PROPERTIES PASS_REGULAR_EXPRESSION "^5.200000000\n5200\n5200\n$")

add_test (rdbfw_test_timespec_math_24 fn_test -t 1 -f -5.500000000 -o 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_24
    PROPERTIES PASS_REGULAR_EXPRESSION "^5.000000000\n5000\n5000\n$")

add_test (rdbfw_test_timespec_math_25 fn_test -t 1 -f -5.500000000 -o 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_25
    PROPERTIES PASS_REGULAR_EXPRESSION "^4.800000000\n4800\n4800\n$")


# now in reverse

add_test (rdbfw_test_timespec_math_101 fn_test -t 1 -o 5.0 -f 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_101
    PROPERTIES PASS_REGULAR_EXPRESSION "^-2.500000000\n-2500\n-2500\n$")

add_test (rdbfw_test_timespec_math_102 fn_test -t 1 -o 5.500000000 -f 7.300000000)
set_tests_properties (rdbfw_test_timespec_math_102
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.800000000\n-1800\n-1800\n$")

add_test (rdbfw_test_timespec_math_103 fn_test -t 1 -o 5.500000000 -f 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_103
    PROPERTIES PASS_REGULAR_EXPRESSION "^-2.000000000\n-2000\n-2000\n$")

add_test (rdbfw_test_timespec_math_104 fn_test -t 1 -o 5.500000000 -f 7.700000000)
set_tests_properties (rdbfw_test_timespec_math_104
    PROPERTIES PASS_REGULAR_EXPRESSION "^-2.200000000\n-2200\n-2200\n$")

add_test (rdbfw_test_timespec_math_105 fn_test -t 1 -o 5.500000000 -f 5.300000000)
set_tests_properties (rdbfw_test_timespec_math_105
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_106 fn_test -t 1 -o 5.500000000 -f 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_106
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_107 fn_test -t 1 -o 5.500000000 -f 5.700000000)
set_tests_properties (rdbfw_test_timespec_math_107
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_108 fn_test -t 1 -o 0.500000000 -f 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_108
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_109 fn_test -t 1 -o 0.500000000 -f 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_109
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_110 fn_test -t 1 -o 0.500000000 -f 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_110
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_111 fn_test -t 1 -o 0.-500000000 -f 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_111
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_112 fn_test -t 1 -o 0.-500000000 -f 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_112
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_113 fn_test -t 1 -o 0.-500000000 -f 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_113
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_114 fn_test -t 1 -o 0.-500000000 -f 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_114
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.800000000\n-800\n-800\n$")

add_test (rdbfw_test_timespec_math_115 fn_test -t 1 -o 0.-500000000 -f 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_115
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.000000000\n-1000\n-1000\n$")

add_test (rdbfw_test_timespec_math_116 fn_test -t 1 -o 0.-500000000 -f 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_116
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.200000000\n-1200\n-1200\n$")

add_test (rdbfw_test_timespec_math_117 fn_test -t 1 -o 0.500000000 -f 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_117
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.800000000\n800\n800\n$")

add_test (rdbfw_test_timespec_math_118 fn_test -t 1 -o 0.500000000 -f 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_118
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.000000000\n1000\n1000\n$")

add_test (rdbfw_test_timespec_math_119 fn_test -t 1 -o 0.500000000 -f 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_119
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.200000000\n1200\n1200\n$")


add_test (rdbfw_test_timespec_math_120 fn_test -t 1 -o -5.500000000 -f 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_120
    PROPERTIES PASS_REGULAR_EXPRESSION "^-5.800000000\n-5800\n-5800\n$")

add_test (rdbfw_test_timespec_math_121 fn_test -t 1 -o -5.500000000 -f 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_121
    PROPERTIES PASS_REGULAR_EXPRESSION "^-6.000000000\n-6000\n-6000\n$")

add_test (rdbfw_test_timespec_math_122 fn_test -t 1 -o -5.500000000 -f 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_122
    PROPERTIES PASS_REGULAR_EXPRESSION "^-6.200000000\n-6200\n-6200\n$")

add_test (rdbfw_test_timespec_math_123 fn_test -t 1 -o -5.500000000 -f 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_123
    PROPERTIES PASS_REGULAR_EXPRESSION "^-5.200000000\n-5200\n-5200\n$")

add_test (rdbfw_test_timespec_math_124 fn_test -t 1 -o -5.500000000 -f 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_124
    PROPERTIES PASS_REGULAR_EXPRESSION "^-5.000000000\n-5000\n-5000\n$")

add_test (rdbfw_test_timespec_math_125 fn_test -t 1 -o -5.500000000 -f 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_125
    PROPERTIES PASS_REGULAR_EXPRESSION "^-4.800000000\n-4800\n-4800\n$")


add_test (rdbfw_test_timespec_math_200 fn_test -t 2 -f 3.000000000 -o 4.000000000)
set_tests_properties (rdbfw_test_timespec_math_200
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_201 fn_test -t 2 -f 3.500000000 -o 4.500000000)
set_tests_properties (rdbfw_test_timespec_math_201
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_202 fn_test -t 2 -f 3.500000000 -o 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_202
    PROPERTIES PASS_REGULAR_EXPRESSION "^2010\n1990\n$")

add_test (rdbfw_test_timespec_math_203 fn_test -t 2 -f 3.000000000 -o 3.100000000)
set_tests_properties (rdbfw_test_timespec_math_203
    PROPERTIES PASS_REGULAR_EXPRESSION "^110\n90\n$")

add_test (rdbfw_test_timespec_math_204 fn_test -t 2 -f 0.000000000 -o 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_204
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_205 fn_test -t 2 -f 0.500000000 -o 1.500000000)
set_tests_properties (rdbfw_test_timespec_math_205
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_206 fn_test -t 2 -f 0.500000000 -o 2.500000000)
set_tests_properties (rdbfw_test_timespec_math_206
    PROPERTIES PASS_REGULAR_EXPRESSION "^2010\n1990\n$")

add_test (rdbfw_test_timespec_math_207 fn_test -t 2 -f 0.000000000 -o 0.100000000)
set_tests_properties (rdbfw_test_timespec_math_207
    PROPERTIES PASS_REGULAR_EXPRESSION "^110\n90\n$")

add_test (rdbfw_test_timespec_math_208 fn_test -t 2 -f -1.000000000 -o 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_208
    PROPERTIES PASS_REGULAR_EXPRESSION "^2010\n1990\n$")

add_test (rdbfw_test_timespec_math_209 fn_test -t 2 -f 3.000000000 -o 3.995000000)
set_tests_properties (rdbfw_test_timespec_math_209
    PROPERTIES PASS_REGULAR_EXPRESSION "^1005\n985\n$")

add_test (rdbfw_test_timespec_math_20a fn_test -t 2 -f 3.000000000 -o 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_20a
    PROPERTIES PASS_REGULAR_EXPRESSION "^1015\n995\n$")


add_test (rdbfw_test_timespec_math_210 fn_test -t 2 -o 3.000000000 -f 4.000000000)
set_tests_properties (rdbfw_test_timespec_math_210
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_211 fn_test -t 2 -o 3.500000000 -f 4.500000000)
set_tests_properties (rdbfw_test_timespec_math_211
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_212 fn_test -t 2 -o 3.500000000 -f 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_212
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1990\n-2010\n$")

add_test (rdbfw_test_timespec_math_213 fn_test -t 2 -o 3.000000000 -f 3.100000000)
set_tests_properties (rdbfw_test_timespec_math_213
    PROPERTIES PASS_REGULAR_EXPRESSION "^-90\n-110\n$")

add_test (rdbfw_test_timespec_math_214 fn_test -t 2 -o 0.000000000 -f 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_214
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_215 fn_test -t 2 -o 0.500000000 -f 1.500000000)
set_tests_properties (rdbfw_test_timespec_math_215
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_216 fn_test -t 2 -o 0.500000000 -f 2.500000000)
set_tests_properties (rdbfw_test_timespec_math_216
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1990\n-2010\n$")

add_test (rdbfw_test_timespec_math_217 fn_test -t 2 -o 0.000000000 -f 0.100000000)
set_tests_properties (rdbfw_test_timespec_math_217
    PROPERTIES PASS_REGULAR_EXPRESSION "^-90\n-110\n$")

add_test (rdbfw_test_timespec_math_218 fn_test -t 2 -o -1.000000000 -f 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_218
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1990\n-2010\n$")

add_test (rdbfw_test_timespec_math_219 fn_test -t 2 -o 3.000000000 -f 3.995000000)
set_tests_properties (rdbfw_test_timespec_math_219
    PROPERTIES PASS_REGULAR_EXPRESSION "^-985\n-1005\n$")

add_test (rdbfw_test_timespec_math_21a fn_test -t 2 -o 3.000000000 -f 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_21a
    PROPERTIES PASS_REGULAR_EXPRESSION "^-995\n-1015\n$")


add_test (rdbfw_test_timespec_math_300 fn_test -t 3 -f 3.000000000 -o 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_300
    PROPERTIES PASS_REGULAR_EXPRESSION "^1100\n$")

add_test (rdbfw_test_timespec_math_301 fn_test -t 3 -f 5.000000000 -o 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_301
    PROPERTIES PASS_REGULAR_EXPRESSION "^0011\n$")

add_test (rdbfw_test_timespec_math_302 fn_test -t 3 -f 5.000000000 -o 5.000000000)
set_tests_properties (rdbfw_test_timespec_math_302
    PROPERTIES PASS_REGULAR_EXPRESSION "^0101\n$")

#add_test (NAME rdbfw_test_valgrind_1 COMMAND fw_test -v5 -t 10)
#set(ENV{RDBFW_UT} [1])

message (${MEMORYCHECK_COMMAND})

#find_program(VALGRIND "valgrind")
#if (VALGRIND)
    set(DART_CONFIG DartConfiguration.tcl)
    add_custom_target(${DART_CONFIG}
        COMMAND echo "MemoryCheckCommand: ${MEMORYCHECK_COMMAND}" >> ${DART_CONFIG}
        COMMENT "Generating ${DART_CONFIG}"
    )
    set(VALGRIND_ARGS
        --leak-check=full
        --error-exitcode=255
    )
    set(LOGFILE memcheck.log)
    add_custom_target(valgrind
        COMMAND ctest -O ${LOGFILE} -D ExperimentalMemCheck --overwrite MemoryCheckCommandOptions="${MEMORYCHECK_COMMAND_OPTIONS}"
        COMMAND tail -n1 ${LOGFILE} | grep 'Memory checking results:' > /dev/null
        COMMAND rm -f ${LOGFILE}
        DEPENDS ${DART_CONFIG}
    )
#endif()
#set_tests_properties (rdbfw_test_valgrind_1
#    PROPERTIES PASS_REGULAR_EXPRESSION "All heap blocks were freed -- no leaks are possible")

#function(add_memcheck_test
#    val1
#    fw_test)
#  set(memcheck_command "${MEMORYCHECK_COMMAND} ${MEMORYCHECK_COMMAND_OPTIONS}")
#  separate_arguments(fw_test
#      -v4
#      -t1)
#  add_test(${name} ${binary} ${ARGN})
#  add_test(memcheck_${name} ${memcheck_command} ./${binary} ${ARGN})
#endfunction(add_memcheck_test)

#function(set_memcheck_test_properties
#        val1)
#  set_tests_properties(${name} ${ARGN})
#  set_tests_properties(memcheck_${name} ${ARGN})
#endfunction(set_memcheck_test_properties)
