# Shared, opt-in GCC coverage support. Kept in rDB; no runtime dependency.
option(TEAL_ENABLE_COVERAGE "Instrument this build and provide a coverage target" OFF)

function(_teal_coverage_instrument directory result)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set(instrumented "")
    foreach(target IN LISTS targets)
        get_target_property(kind "${target}" TYPE)
        if(kind MATCHES "^(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY|STATIC_LIBRARY|OBJECT_LIBRARY)$")
            # Apply last, after targets' normal -O2 options.
            target_compile_options("${target}" PRIVATE --coverage -g
                "${TEAL_COVERAGE_OPTIMIZATION}" -fprofile-abs-path -fprofile-update=atomic)
            set_property(TARGET "${target}" PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
            set_property(TARGET "${target}" PROPERTY EXPORT_COMPILE_COMMANDS ON)
            if(kind MATCHES "^(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)$")
                # Compatible with targets that use the plain target_link_libraries form.
                set_property(TARGET "${target}" APPEND_STRING PROPERTY LINK_FLAGS " --coverage")
            endif()
            list(APPEND instrumented "${target}")
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        _teal_coverage_instrument("${child}" child_targets)
        list(APPEND instrumented ${child_targets})
    endforeach()
    set("${result}" "${instrumented}" PARENT_SCOPE)
endfunction()

function(teal_enable_coverage config)
    if(NOT TEAL_ENABLE_COVERAGE)
        return()
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.21)
        message(FATAL_ERROR "Coverage reporting needs CMake 3.21+ (CTest JUnit); normal builds do not")
    endif()
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "The coverage target runs native tests; use a separate native coverage build")
    endif()
    foreach(language C CXX)
        if(CMAKE_${language}_COMPILER_LOADED AND
                (NOT CMAKE_${language}_COMPILER_ID STREQUAL "GNU" OR
                 CMAKE_${language}_COMPILER_VERSION VERSION_LESS 8))
            message(FATAL_ERROR "TEAL_ENABLE_COVERAGE requires GCC 8+")
        endif()
    endforeach()
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    find_program(TEAL_COVERAGE_GCOVR NAMES gcovr REQUIRED)
    # Ask the compiler first: the host's default gcov may be the wrong version.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-prog-name=gcov
        OUTPUT_VARIABLE gcov_program OUTPUT_STRIP_TRAILING_WHITESPACE)
    find_program(TEAL_COVERAGE_GCOV NAMES "${gcov_program}" REQUIRED)
    set(TEAL_COVERAGE_OPTIMIZATION "-O0" CACHE STRING "Optimization for coverage builds only")
    if(NOT TEAL_COVERAGE_OPTIMIZATION MATCHES "^-O[012g]$")
        message(FATAL_ERROR "TEAL_COVERAGE_OPTIMIZATION must be -O0, -O1, -O2 or -Og")
    endif()
    include(ProcessorCount)
    ProcessorCount(jobs)
    if(NOT jobs)
        set(jobs 1)
    endif()
    set(TEAL_COVERAGE_JOBS "${jobs}" CACHE STRING "Parallel coverage-test and gcov workers")
    get_filename_component(config_dir "${config}" DIRECTORY)
    get_filename_component(source "${config_dir}/.." ABSOLUTE)
    # The helper always lives beside ../tests/coverage_runner.py.
    get_filename_component(runner "${TEAL_COVERAGE_HELPER_DIR}/../tests/coverage_runner.py" ABSOLUTE)
    _teal_coverage_instrument("${CMAKE_SOURCE_DIR}" targets)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON PARENT_SCOPE)
    add_custom_target(coverage
        COMMAND "${Python3_EXECUTABLE}" -B "${runner}"
            --source "${source}" --build "${CMAKE_BINARY_DIR}" --config "${config}"
            --ctest "${CMAKE_CTEST_COMMAND}" --gcovr "${TEAL_COVERAGE_GCOVR}"
            --gcov "${TEAL_COVERAGE_GCOV}" --compiler "${CMAKE_CXX_COMPILER}"
            "--optimization=${TEAL_COVERAGE_OPTIMIZATION}"
            --jobs "${TEAL_COVERAGE_JOBS}"
        DEPENDS ${targets}
        USES_TERMINAL VERBATIM
        COMMENT "Run tests with isolated coverage counters and publish reports")
endfunction()

set(TEAL_COVERAGE_HELPER_DIR "${CMAKE_CURRENT_LIST_DIR}")
