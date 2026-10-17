# Shared, opt-in GCC coverage support. Kept in rDB; no runtime dependency.
option(TEAL_ENABLE_COVERAGE "Instrument this build and provide a coverage target" OFF)

function(_teal_coverage_instrument directory result libraries)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set(instrumented "")
    set(library_dirs "")
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
            if(kind MATCHES "^(SHARED_LIBRARY|MODULE_LIBRARY)$")
                list(APPEND library_dirs "$<TARGET_FILE_DIR:${target}>")
            endif()
            list(APPEND instrumented "${target}")
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        _teal_coverage_instrument("${child}" child_targets child_libraries)
        list(APPEND instrumented ${child_targets})
        list(APPEND library_dirs ${child_libraries})
    endforeach()
    set("${result}" "${instrumented}" PARENT_SCOPE)
    set("${libraries}" "${library_dirs}" PARENT_SCOPE)
endfunction()

function(teal_enable_coverage config)
    include(ProcessorCount)
    ProcessorCount(jobs)
    if(NOT jobs)
        set(jobs 1)
    endif()
    set(TEAL_COVERAGE_JOBS "${jobs}" CACHE STRING "Parallel coverage build/test/report workers")
    find_program(TEAL_COVERAGE_GCOVR NAMES gcovr
        HINTS "${TEAL_COVERAGE_HELPER_DIR}/../build/coverage-tools/bin")
    if(NOT TEAL_ENABLE_COVERAGE)
        # Keep ordinary binaries untouched. Only an explicit coverage target
        # invocation configures/builds the instrumented child directory.
        if(CMAKE_VERSION VERSION_LESS 3.21 OR CMAKE_CROSSCOMPILING)
            add_custom_target(coverage
                COMMAND "${CMAKE_COMMAND}" -E echo
                    "Coverage requires a native build configured with CMake 3.21 or newer."
                COMMAND "${CMAKE_COMMAND}" -E false VERBATIM)
            return()
        endif()
        set(coverage_build "${CMAKE_BINARY_DIR}/coverage")
        set(coverage_cache "${CMAKE_BINARY_DIR}/coverage-config.cmake")
        file(WRITE "${coverage_cache}" "# Generated coverage configuration; do not edit.\n")
        get_cmake_property(cache_variables CACHE_VARIABLES)
        foreach(name ${cache_variables})
            get_property(type CACHE "${name}" PROPERTY TYPE)
            if(type MATCHES "^(INTERNAL|STATIC)$")
                continue()
            endif()
            if(type STREQUAL "UNINITIALIZED")
                set(type STRING)
            endif()
            # Preserve project/dependency options, compilers and search paths,
            # but never transplant CMake's own build-directory/probe state.
            if(name MATCHES "^CMAKE_" AND NOT name MATCHES
                    "^CMAKE_(C_COMPILER|CXX_COMPILER|MAKE_PROGRAM|TOOLCHAIN_FILE|PREFIX_PATH|INCLUDE_PATH|LIBRARY_PATH|BUILD_RPATH|INSTALL_PREFIX|SYSROOT|[A-Z_]*FLAGS[A-Z_]*)$")
                continue()
            endif()
            get_property(value CACHE "${name}" PROPERTY VALUE)
            string(REPLACE "\\" "\\\\" value "${value}")
            string(REPLACE "\"" "\\\"" value "${value}")
            string(REPLACE "$" "\\$" value "${value}")
            file(APPEND "${coverage_cache}"
                "set(${name} \"${value}\" CACHE ${type} \"Inherited from the normal build\" FORCE)\n")
        endforeach()
        add_custom_target(coverage
            COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}" -B "${coverage_build}"
                -G "${CMAKE_GENERATOR}" -C "${coverage_cache}"
                -DTEAL_ENABLE_COVERAGE=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
                "-DTEAL_COVERAGE_TOOLS_DIR=${TEAL_COVERAGE_HELPER_DIR}" ${ARGN}
            COMMAND "${CMAKE_COMMAND}" --build "${coverage_build}" --target coverage
                --parallel "${TEAL_COVERAGE_JOBS}"
            USES_TERMINAL VERBATIM
            COMMENT "Build and report coverage separately from the normal binaries")
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
    if(NOT TEAL_COVERAGE_GCOVR)
        message(FATAL_ERROR
            "Coverage needs gcovr 8.6. Install the rDB tests/coverage-requirements.txt tools "
            "in rDB/build/coverage-tools, or set TEAL_COVERAGE_GCOVR to the gcovr executable.")
    endif()
    # Ask the compiler first: the host's default gcov may be the wrong version.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-prog-name=gcov
        OUTPUT_VARIABLE gcov_program OUTPUT_STRIP_TRAILING_WHITESPACE)
    find_program(TEAL_COVERAGE_GCOV NAMES "${gcov_program}" REQUIRED)
    set(TEAL_COVERAGE_OPTIMIZATION "-O0" CACHE STRING "Optimization for coverage builds only")
    if(NOT TEAL_COVERAGE_OPTIMIZATION MATCHES "^-O[012g]$")
        message(FATAL_ERROR "TEAL_COVERAGE_OPTIMIZATION must be -O0, -O1, -O2 or -Og")
    endif()
    get_filename_component(config_dir "${config}" DIRECTORY)
    get_filename_component(source "${config_dir}/.." ABSOLUTE)
    # The helper always lives beside ../tests/coverage_runner.py.
    get_filename_component(runner "${TEAL_COVERAGE_HELPER_DIR}/../tests/coverage_runner.py" ABSOLUTE)
    _teal_coverage_instrument("${CMAKE_SOURCE_DIR}" targets library_dirs)
    set(library_args "")
    foreach(directory ${library_dirs})
        list(APPEND library_args --library-dir "${directory}")
    endforeach()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON PARENT_SCOPE)
    add_custom_target(coverage
        COMMAND "${Python3_EXECUTABLE}" -B "${runner}"
            --source "${source}" --build "${CMAKE_BINARY_DIR}" --config "${config}"
            --ctest "${CMAKE_CTEST_COMMAND}" --gcovr "${TEAL_COVERAGE_GCOVR}"
            --gcov "${TEAL_COVERAGE_GCOV}" --compiler "${CMAKE_CXX_COMPILER}"
            "--optimization=${TEAL_COVERAGE_OPTIMIZATION}"
            --jobs "${TEAL_COVERAGE_JOBS}" ${library_args}
        DEPENDS ${targets}
        USES_TERMINAL VERBATIM
        COMMENT "Run tests with isolated coverage counters and publish reports")
endfunction()

set(TEAL_COVERAGE_HELPER_DIR "${CMAKE_CURRENT_LIST_DIR}")
