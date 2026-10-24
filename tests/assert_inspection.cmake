execute_process(
    COMMAND "${INSPECTOR}" "${FIXTURE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Inspector failed with ${result}: ${error}")
endif()

set(expected_fragments
    "Nodes                                      4"
    "Ways                                       2"
    "Relations                                  1"
    "Address-only nodes                         1"
    "Building ways                              1"
    "Building-way node references               4"
    "Highway ways                               1"
    "Street node references                     2"
    "Administrative relations                   1"
    "Administrative outer members               1"
    "Administrative inner members               1"
)

foreach(fragment IN LISTS expected_fragments)
    string(FIND "${output}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing expected output: ${fragment}\nFull output:\n${output}")
    endif()
endforeach()
