function(expect_invalid_port command)
    execute_process(
        COMMAND "${command}" ${ARGN}
        RESULT_VARIABLE result
        ERROR_VARIABLE error
        TIMEOUT 3)
    if(NOT result STREQUAL "1" OR NOT error MATCHES "Invalid port:")
        message(FATAL_ERROR "Expected port validation for ${ARGN}. Got ${result}: ${error}")
    endif()
endfunction()

# An existing file blocks state creation if the parser accepts a bad port.
set(state_path "${CMAKE_SCRIPT_MODE_FILE}/state")
expect_invalid_port("${EXCHANGE}" --state-dir "${state_path}" --tcp-port 65536)
expect_invalid_port("${EXCHANGE}" --state-dir "${state_path}" --udp-a 10001oops)
expect_invalid_port("${EXCHANGE}" --state-dir "${state_path}" --udp-b -1)
expect_invalid_port("${CLIENT}" 9999oops)
