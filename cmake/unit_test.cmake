include(GoogleTest)
function(add_unit_test TEST_NAME)
    gtest_discover_tests(${TEST_NAME} 
        # XML_OUTPUT_DIR supported from 3.18 only and should replace EXTRA_ARGS when available
        # XML_OUTPUT_DIR report-${TEST_NAME}.xml 
        # DISCOVERY_MODE supported from 3.18 only and is preferred in cross-compilation cases
        # DISCOVERY_MODE PRE_TEST 
        EXTRA_ARGS "--gtest_output=xml:report-${TEST_NAME}.xml")
endfunction()