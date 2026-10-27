# Check exit status AND output. CTest PASS_REGULAR_EXPRESSION alone can mask
# a nonzero exit; sanitizer diagnostics must never be accepted as a success.
execute_process(COMMAND "${BINARY}" ${ARGUMENTS}
    RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE diagnostics)
if(NOT "${status}" STREQUAL "0")
    message(FATAL_ERROR "Legacy process exited with ${status}\n${actual}\n${diagnostics}")
endif()
if(NOT "${diagnostics}" STREQUAL "")
    message(FATAL_ERROR "Unexpected stderr from legacy process:\n${diagnostics}")
endif()
# CTest's historical output matching includes a final newline.
if(NOT actual MATCHES "\n$")
    set(actual "${actual}\n")
endif()
if(NOT actual MATCHES "${EXPECTED}")
    message(FATAL_ERROR "Legacy output did not match:\n${actual}\nExpected:\n${EXPECTED}")
endif()
message("${actual}")
