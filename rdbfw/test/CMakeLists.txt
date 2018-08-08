project (rdbfw_test)

SET(rdbfw_test_SRCS
   rdbfw_test.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -g -lm")

include_directories ("${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/rdbfw")

link_directories ("${PROJECT_SOURCE_DIR}/")

link_libraries (rdb_beta)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -export-dynamic -avoid-version -g")
 
add_executable(rdbfw_test rdbfw_test.c ../utils.c)

target_link_libraries(rdbfw_test m)

#install(TARGETS rdb_test DESTINATION test)

enable_testing()

add_test (rdbfw_test_timespec_math_1 rdbfw_test -t 1 -f 5.0 -o 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_1
    PROPERTIES PASS_REGULAR_EXPRESSION "^2.500000000\n2500\n2500\n$")

add_test (rdbfw_test_timespec_math_2 rdbfw_test -t 1 -f 5.500000000 -o 7.300000000)
set_tests_properties (rdbfw_test_timespec_math_2
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.800000000\n1800\n1800\n$")

add_test (rdbfw_test_timespec_math_3 rdbfw_test -t 1 -f 5.500000000 -o 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_3
    PROPERTIES PASS_REGULAR_EXPRESSION "^2.000000000\n2000\n2000\n$")

add_test (rdbfw_test_timespec_math_4 rdbfw_test -t 1 -f 5.500000000 -o 7.700000000)
set_tests_properties (rdbfw_test_timespec_math_4
    PROPERTIES PASS_REGULAR_EXPRESSION "^2.200000000\n2200\n2200\n$")

add_test (rdbfw_test_timespec_math_5 rdbfw_test -t 1 -f 5.500000000 -o 5.300000000)
set_tests_properties (rdbfw_test_timespec_math_5
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_6 rdbfw_test -t 1 -f 5.500000000 -o 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_6
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_7 rdbfw_test -t 1 -f 5.500000000 -o 5.700000000)
set_tests_properties (rdbfw_test_timespec_math_7
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_8 rdbfw_test -t 1 -f 0.500000000 -o 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_8
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_9 rdbfw_test -t 1 -f 0.500000000 -o 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_9
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_10 rdbfw_test -t 1 -f 0.500000000 -o 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_10
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_11 rdbfw_test -t 1 -f 0.-500000000 -o 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_11
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_12 rdbfw_test -t 1 -f 0.-500000000 -o 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_12
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_13 rdbfw_test -t 1 -f 0.-500000000 -o 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_13
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_14 rdbfw_test -t 1 -f 0.-500000000 -o 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_14
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.800000000\n800\n800\n$")

add_test (rdbfw_test_timespec_math_15 rdbfw_test -t 1 -f 0.-500000000 -o 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_15
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.000000000\n1000\n1000\n$")

add_test (rdbfw_test_timespec_math_16 rdbfw_test -t 1 -f 0.-500000000 -o 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_16
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.200000000\n1200\n1200\n$")

add_test (rdbfw_test_timespec_math_17 rdbfw_test -t 1 -f 0.500000000 -o 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_17
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.800000000\n-800\n-800\n$")

add_test (rdbfw_test_timespec_math_18 rdbfw_test -t 1 -f 0.500000000 -o 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_18
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.000000000\n-1000\n-1000\n$")

add_test (rdbfw_test_timespec_math_19 rdbfw_test -t 1 -f 0.500000000 -o 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_19
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.200000000\n-1200\n-1200\n$")


add_test (rdbfw_test_timespec_math_20 rdbfw_test -t 1 -f -5.500000000 -o 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_20
    PROPERTIES PASS_REGULAR_EXPRESSION "^5.800000000\n5800\n5800\n$")

add_test (rdbfw_test_timespec_math_21 rdbfw_test -t 1 -f -5.500000000 -o 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_21
    PROPERTIES PASS_REGULAR_EXPRESSION "^6.000000000\n6000\n6000\n$")

add_test (rdbfw_test_timespec_math_22 rdbfw_test -t 1 -f -5.500000000 -o 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_22
    PROPERTIES PASS_REGULAR_EXPRESSION "^6.200000000\n6200\n6200\n$")

add_test (rdbfw_test_timespec_math_23 rdbfw_test -t 1 -f -5.500000000 -o 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_23
    PROPERTIES PASS_REGULAR_EXPRESSION "^5.200000000\n5200\n5200\n$")

add_test (rdbfw_test_timespec_math_24 rdbfw_test -t 1 -f -5.500000000 -o 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_24
    PROPERTIES PASS_REGULAR_EXPRESSION "^5.000000000\n5000\n5000\n$")

add_test (rdbfw_test_timespec_math_25 rdbfw_test -t 1 -f -5.500000000 -o 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_25
    PROPERTIES PASS_REGULAR_EXPRESSION "^4.800000000\n4800\n4800\n$")


# now in reverse

add_test (rdbfw_test_timespec_math_101 rdbfw_test -t 1 -o 5.0 -f 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_101
    PROPERTIES PASS_REGULAR_EXPRESSION "^-2.500000000\n-2500\n-2500\n$")

add_test (rdbfw_test_timespec_math_102 rdbfw_test -t 1 -o 5.500000000 -f 7.300000000)
set_tests_properties (rdbfw_test_timespec_math_102
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.800000000\n-1800\n-1800\n$")

add_test (rdbfw_test_timespec_math_103 rdbfw_test -t 1 -o 5.500000000 -f 7.500000000)
set_tests_properties (rdbfw_test_timespec_math_103
    PROPERTIES PASS_REGULAR_EXPRESSION "^-2.000000000\n-2000\n-2000\n$")

add_test (rdbfw_test_timespec_math_104 rdbfw_test -t 1 -o 5.500000000 -f 7.700000000)
set_tests_properties (rdbfw_test_timespec_math_104
    PROPERTIES PASS_REGULAR_EXPRESSION "^-2.200000000\n-2200\n-2200\n$")

add_test (rdbfw_test_timespec_math_105 rdbfw_test -t 1 -o 5.500000000 -f 5.300000000)
set_tests_properties (rdbfw_test_timespec_math_105
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_106 rdbfw_test -t 1 -o 5.500000000 -f 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_106
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_107 rdbfw_test -t 1 -o 5.500000000 -f 5.700000000)
set_tests_properties (rdbfw_test_timespec_math_107
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_108 rdbfw_test -t 1 -o 0.500000000 -f 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_108
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_109 rdbfw_test -t 1 -o 0.500000000 -f 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_109
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_110 rdbfw_test -t 1 -o 0.500000000 -f 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_110
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_111 rdbfw_test -t 1 -o 0.-500000000 -f 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_111
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.200000000\n-200\n-200\n$")

add_test (rdbfw_test_timespec_math_112 rdbfw_test -t 1 -o 0.-500000000 -f 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_112
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.000000000\n0\n0\n$")

add_test (rdbfw_test_timespec_math_113 rdbfw_test -t 1 -o 0.-500000000 -f 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_113
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.200000000\n200\n200\n$")

add_test (rdbfw_test_timespec_math_114 rdbfw_test -t 1 -o 0.-500000000 -f 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_114
    PROPERTIES PASS_REGULAR_EXPRESSION "^-0.800000000\n-800\n-800\n$")

add_test (rdbfw_test_timespec_math_115 rdbfw_test -t 1 -o 0.-500000000 -f 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_115
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.000000000\n-1000\n-1000\n$")

add_test (rdbfw_test_timespec_math_116 rdbfw_test -t 1 -o 0.-500000000 -f 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_116
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1.200000000\n-1200\n-1200\n$")

add_test (rdbfw_test_timespec_math_117 rdbfw_test -t 1 -o 0.500000000 -f 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_117
    PROPERTIES PASS_REGULAR_EXPRESSION "^0.800000000\n800\n800\n$")

add_test (rdbfw_test_timespec_math_118 rdbfw_test -t 1 -o 0.500000000 -f 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_118
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.000000000\n1000\n1000\n$")

add_test (rdbfw_test_timespec_math_119 rdbfw_test -t 1 -o 0.500000000 -f 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_119
    PROPERTIES PASS_REGULAR_EXPRESSION "^1.200000000\n1200\n1200\n$")


add_test (rdbfw_test_timespec_math_120 rdbfw_test -t 1 -o -5.500000000 -f 0.300000000)
set_tests_properties (rdbfw_test_timespec_math_120
    PROPERTIES PASS_REGULAR_EXPRESSION "^-5.800000000\n-5800\n-5800\n$")

add_test (rdbfw_test_timespec_math_121 rdbfw_test -t 1 -o -5.500000000 -f 0.500000000)
set_tests_properties (rdbfw_test_timespec_math_121
    PROPERTIES PASS_REGULAR_EXPRESSION "^-6.000000000\n-6000\n-6000\n$")

add_test (rdbfw_test_timespec_math_122 rdbfw_test -t 1 -o -5.500000000 -f 0.700000000)
set_tests_properties (rdbfw_test_timespec_math_122
    PROPERTIES PASS_REGULAR_EXPRESSION "^-6.200000000\n-6200\n-6200\n$")

add_test (rdbfw_test_timespec_math_123 rdbfw_test -t 1 -o -5.500000000 -f 0.-300000000)
set_tests_properties (rdbfw_test_timespec_math_123
    PROPERTIES PASS_REGULAR_EXPRESSION "^-5.200000000\n-5200\n-5200\n$")

add_test (rdbfw_test_timespec_math_124 rdbfw_test -t 1 -o -5.500000000 -f 0.-500000000)
set_tests_properties (rdbfw_test_timespec_math_124
    PROPERTIES PASS_REGULAR_EXPRESSION "^-5.000000000\n-5000\n-5000\n$")

add_test (rdbfw_test_timespec_math_125 rdbfw_test -t 1 -o -5.500000000 -f 0.-700000000)
set_tests_properties (rdbfw_test_timespec_math_125
    PROPERTIES PASS_REGULAR_EXPRESSION "^-4.800000000\n-4800\n-4800\n$")


add_test (rdbfw_test_timespec_math_200 rdbfw_test -t 2 -f 3.000000000 -o 4.000000000)
set_tests_properties (rdbfw_test_timespec_math_200
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_201 rdbfw_test -t 2 -f 3.500000000 -o 4.500000000)
set_tests_properties (rdbfw_test_timespec_math_201
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_202 rdbfw_test -t 2 -f 3.500000000 -o 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_202
    PROPERTIES PASS_REGULAR_EXPRESSION "^2010\n1990\n$")

add_test (rdbfw_test_timespec_math_203 rdbfw_test -t 2 -f 3.000000000 -o 3.100000000)
set_tests_properties (rdbfw_test_timespec_math_203
    PROPERTIES PASS_REGULAR_EXPRESSION "^110\n90\n$")

add_test (rdbfw_test_timespec_math_204 rdbfw_test -t 2 -f 0.000000000 -o 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_204
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_205 rdbfw_test -t 2 -f 0.500000000 -o 1.500000000)
set_tests_properties (rdbfw_test_timespec_math_205
    PROPERTIES PASS_REGULAR_EXPRESSION "^1010\n990\n$")

add_test (rdbfw_test_timespec_math_206 rdbfw_test -t 2 -f 0.500000000 -o 2.500000000)
set_tests_properties (rdbfw_test_timespec_math_206
    PROPERTIES PASS_REGULAR_EXPRESSION "^2010\n1990\n$")

add_test (rdbfw_test_timespec_math_207 rdbfw_test -t 2 -f 0.000000000 -o 0.100000000)
set_tests_properties (rdbfw_test_timespec_math_207
    PROPERTIES PASS_REGULAR_EXPRESSION "^110\n90\n$")

add_test (rdbfw_test_timespec_math_208 rdbfw_test -t 2 -f -1.000000000 -o 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_208
    PROPERTIES PASS_REGULAR_EXPRESSION "^2010\n1990\n$")

add_test (rdbfw_test_timespec_math_209 rdbfw_test -t 2 -f 3.000000000 -o 3.995000000)
set_tests_properties (rdbfw_test_timespec_math_209
    PROPERTIES PASS_REGULAR_EXPRESSION "^1005\n985\n$")

add_test (rdbfw_test_timespec_math_20a rdbfw_test -t 2 -f 3.000000000 -o 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_20a
    PROPERTIES PASS_REGULAR_EXPRESSION "^1015\n995\n$")


add_test (rdbfw_test_timespec_math_210 rdbfw_test -t 2 -o 3.000000000 -f 4.000000000)
set_tests_properties (rdbfw_test_timespec_math_210
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_211 rdbfw_test -t 2 -o 3.500000000 -f 4.500000000)
set_tests_properties (rdbfw_test_timespec_math_211
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_212 rdbfw_test -t 2 -o 3.500000000 -f 5.500000000)
set_tests_properties (rdbfw_test_timespec_math_212
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1990\n-2010\n$")

add_test (rdbfw_test_timespec_math_213 rdbfw_test -t 2 -o 3.000000000 -f 3.100000000)
set_tests_properties (rdbfw_test_timespec_math_213
    PROPERTIES PASS_REGULAR_EXPRESSION "^-90\n-110\n$")

add_test (rdbfw_test_timespec_math_214 rdbfw_test -t 2 -o 0.000000000 -f 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_214
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_215 rdbfw_test -t 2 -o 0.500000000 -f 1.500000000)
set_tests_properties (rdbfw_test_timespec_math_215
    PROPERTIES PASS_REGULAR_EXPRESSION "^-990\n-1010\n$")

add_test (rdbfw_test_timespec_math_216 rdbfw_test -t 2 -o 0.500000000 -f 2.500000000)
set_tests_properties (rdbfw_test_timespec_math_216
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1990\n-2010\n$")

add_test (rdbfw_test_timespec_math_217 rdbfw_test -t 2 -o 0.000000000 -f 0.100000000)
set_tests_properties (rdbfw_test_timespec_math_217
    PROPERTIES PASS_REGULAR_EXPRESSION "^-90\n-110\n$")

add_test (rdbfw_test_timespec_math_218 rdbfw_test -t 2 -o -1.000000000 -f 1.000000000)
set_tests_properties (rdbfw_test_timespec_math_218
    PROPERTIES PASS_REGULAR_EXPRESSION "^-1990\n-2010\n$")

add_test (rdbfw_test_timespec_math_219 rdbfw_test -t 2 -o 3.000000000 -f 3.995000000)
set_tests_properties (rdbfw_test_timespec_math_219
    PROPERTIES PASS_REGULAR_EXPRESSION "^-985\n-1005\n$")

add_test (rdbfw_test_timespec_math_21a rdbfw_test -t 2 -o 3.000000000 -f 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_21a
    PROPERTIES PASS_REGULAR_EXPRESSION "^-995\n-1015\n$")


add_test (rdbfw_test_timespec_math_300 rdbfw_test -t 3 -f 3.000000000 -o 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_300
    PROPERTIES PASS_REGULAR_EXPRESSION "^1100\n$")

add_test (rdbfw_test_timespec_math_301 rdbfw_test -t 3 -f 5.000000000 -o 4.005000000)
set_tests_properties (rdbfw_test_timespec_math_301
    PROPERTIES PASS_REGULAR_EXPRESSION "^0011\n$")

add_test (rdbfw_test_timespec_math_302 rdbfw_test -t 3 -f 5.000000000 -o 5.000000000)
set_tests_properties (rdbfw_test_timespec_math_302
    PROPERTIES PASS_REGULAR_EXPRESSION "^0101\n$")
