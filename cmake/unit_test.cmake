include(GoogleTest)
function(add_unit_test TEST_NAME)
    gtest_discover_tests(${TEST_NAME}
        # XML_OUTPUT_DIR supported from 3.18 only and should replace EXTRA_ARGS when available
        # XML_OUTPUT_DIR report-${TEST_NAME}.xml
        # DISCOVERY_MODE supported from 3.18 only and is preferred in cross-compilation cases
        # DISCOVERY_MODE PRE_TEST
        EXTRA_ARGS "--gtest_output=xml:report-${TEST_NAME}.xml"
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

    add_unit_test(${TEST_NAME}
        PROPERTIES ${properties}
            # If the build env is different than the env where the test will run, the path might be different than
            # what we found. So we will tell the test to try preloading it with a path under /usr/lib too.
            ENVIRONMENT LD_PRELOAD=:${FAKETIME}:/usr/lib/faketime/libfaketimeMT.so.1
            ENVIRONMENT FAKETIME_NO_CACHE=1
            ENVIRONMENT FAKETIME_DONT_FAKE_MONOTONIC=1
            ENVIRONMENT DONT_FAKE_MONOTONIC=1 # Older versions (e.g. 0.9.7) use this variable
    )
endfunction()
