# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

function(add_unit_tests)
    list(JOIN ARGN "_" TEST_NAME)
    add_pytest_cpp_unit_tests(${TEST_NAME} ${ARGN})
endfunction()

function(add_parallel_unit_tests)
    list(JOIN ARGN "_" TEST_NAME)
    add_pytest_cpp_unit_tests(${TEST_NAME} -n auto --dist worksteal --random-order-bucket=global ${ARGN})
endfunction()

function(add_pytest_cpp_unit_tests TEST_NAME)
    add_test(
        NAME ${TEST_NAME}
        # This uses the pytest-cpp plugin, which can discover GoogleTest tests:
        # https://github.com/pytest-dev/pytest-cpp
        # We use it mostly for parallelizing the tests but also it is nice to get the other benefits
        # from pytest like only showing output for failed tests and the html report.
        COMMAND pytest
            -vvv
            -ra
            --color=yes
            --junit-xml=report-${TEST_NAME}.xml
            --html=html_report/${TEST_NAME}/index.html
            ${ARGN}
    )
endfunction()

function(add_unit_test_with_faketime TEST_NAME)
    set(ORIGINAL_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    # We only have available the so with version since ".so" is used only during compiler/linker phase
    # and this library is not expected to be linked. Besides we may have changed the suffix to be ".a" only,
    # which would make the search below always fail.
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so.1")
    find_library(FAKETIME NAMES faketimeMT PATH_SUFFIXES faketime)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${ORIGINAL_CMAKE_FIND_LIBRARY_SUFFIXES})

    if (NOT FAKETIME)
        message(FATAL_ERROR "faketime library not found. Either install it or disable the tests that depend on it.")
    endif ()

    add_unit_tests(${TEST_NAME})
    set_tests_properties(${TEST_NAME}
        PROPERTIES
            # If the build env is different than the env where the test will run, the path might be different than
            # what we found. So we will tell the test to try preloading it with a path under /usr/lib too.
            # Older versions (e.g. 0.9.7) use DONT_FAKE_MONOTONIC variable
            ENVIRONMENT "LD_PRELOAD=:${FAKETIME}:/usr/lib/faketime/libfaketimeMT.so.1;FAKETIME_NO_CACHE=1;FAKETIME_DONT_FAKE_MONOTONIC=1;DONT_FAKE_MONOTONIC=1"
    )
endfunction()
