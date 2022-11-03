# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
if(FWE_TEST_CLANG_TIDY)
  find_program(CLANG_TIDY_COMMAND NAMES clang-tidy)

  if(NOT CLANG_TIDY_COMMAND)
    message(WARNING "FWE_TEST_CLANG_TIDY is ON but clang-tidy is not found!")
  else()
    add_test(NAME ClangTidyTest
      COMMAND bash -c "python3 ${CMAKE_CURRENT_LIST_DIR}/../tools/code_check/compile_db_remove_test.py ${CMAKE_BINARY_DIR} \
        && run-clang-tidy-10 -header-filter=${CMAKE_CURRENT_SOURCE_DIR}/src/.* -p ${CMAKE_BINARY_DIR}/Testing/Temporary ${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
  endif()
endif()
