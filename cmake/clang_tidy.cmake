# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
if(FWE_TEST_CLANG_TIDY)
  find_program(CLANG_TIDY_COMMAND NAMES clang-tidy)

  if(NOT CLANG_TIDY_COMMAND)
    message(WARNING "FWE_TEST_CLANG_TIDY is ON but clang-tidy is not found!")
  else()
    add_test(NAME ClangTidyTest
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/code_check/clang-tidy-test.sh ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}
    )
  endif()
endif()
