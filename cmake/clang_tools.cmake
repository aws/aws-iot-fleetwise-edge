# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
if(FWE_TEST_CLANG_FORMAT)
  find_program(CLANG_FORMAT NAMES clang-format clang-format-10.0)

  file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/src/*.[ch]pp
    ${PROJECT_SOURCE_DIR}/src/*.h
  )

  if (CLANG_FORMAT)
    message(STATUS "Found clang-format and adding formatting test.")
    add_test(NAME ClangFormatTest
      COMMAND ${CLANG_FORMAT} -style=file --dry-run --Werror ${ALL_CXX_SOURCE_FILES}
      )
  else()
    add_custom_target(clang-format-test ALL
      COMMAND ${CMAKE_COMMAND} -E 
      echo "Failed to find clang-format v10 or greater. Formatting test is not added."
    )
  endif ()
endif()
