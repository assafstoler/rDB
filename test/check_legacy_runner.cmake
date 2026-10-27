# These negative probes must fail even though most print the expected stdout.
foreach(mode pass mismatch nonzero stderr signal)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DBINARY=${PROBE}"
        "-DARGUMENTS=${mode}" "-DEXPECTED=^OK\n$" -P "${RUNNER}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(mode STREQUAL "pass")
        if(NOT "${status}" STREQUAL "0")
            message(FATAL_ERROR "Valid output rejected: ${out} ${err}")
        endif()
    elseif("${status}" STREQUAL "0")
        message(FATAL_ERROR "Runner incorrectly accepted ${mode}")
    elseif(NOT err MATCHES "Legacy process exited|Unexpected stderr|Legacy output did not match")
        message(FATAL_ERROR "Probe failed for an unrelated reason: ${mode}: ${err}")
    endif()
endforeach()
