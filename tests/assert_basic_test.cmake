execute_process(
    COMMAND "${SHEET1}" --pbf "${FIXTURE}" --basicTest "${INPUT}" "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Basic test failed with ${result}: ${stderr}\n${stdout}")
endif()

file(READ "${OUTPUT}" actual)
set(expected "way/11, relation/20\nway/11\n\n\n")

if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Unexpected basic-test output.\nExpected:\n${expected}\nActual:\n${actual}")
endif()
