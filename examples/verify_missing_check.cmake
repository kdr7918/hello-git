execute_process(
    COMMAND "${EXAMPLE}" "${RDB_FILE}" "MISSING.CHECK"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 3)
    message(FATAL_ERROR
        "expected exit code 3 for a missing named Check, got ${result}\n"
        "stdout:\n${output}\nstderr:\n${error}")
endif()

if(NOT error MATCHES "Check not found: MISSING.CHECK")
    message(FATAL_ERROR "missing-check diagnostic not found in stderr: ${error}")
endif()
