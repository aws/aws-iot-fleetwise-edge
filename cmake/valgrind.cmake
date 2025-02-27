# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

if(FWE_VALGRIND)
  find_program(VALGRIND_COMMAND valgrind)
  if(NOT VALGRIND_COMMAND)
    message(FATAL_ERROR "valgrind is not installed")
  endif()
  set(VALGRIND_FULL_COMMAND ${VALGRIND_COMMAND} --error-exitcode=1 --max-threads=2000 --leak-check=full --show-leak-kinds=all --gen-suppressions=all --suppressions=${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/valgrind.supp)
endif()

function(add_valgrind_tests)
  if(FWE_VALGRIND)
    foreach(BINARY ${ARGN})
      set(TEST_NAME valgrind_${BINARY})
      add_test(NAME ${TEST_NAME}
        COMMAND ${VALGRIND_FULL_COMMAND} ./${BINARY} --gtest_output=xml:report-${TEST_NAME}.xml)
    endforeach()
  endif()
endfunction()

function(add_parallel_valgrind_tests)
  if(FWE_VALGRIND)
    list(JOIN VALGRIND_FULL_COMMAND " " VALGRIND_FULL_COMMAND_AS_STRING)
    foreach(BINARY ${ARGN})
      set(TEST_NAME valgrind_${BINARY})
      add_test(NAME ${TEST_NAME}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/valgrind-unit-tests-parallel.sh --binary ${BINARY} --valgrind-command ${VALGRIND_FULL_COMMAND_AS_STRING})
    endforeach()
  endif()
endfunction()
