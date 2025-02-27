# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set(_CAPICXX_GENERATE_SOMEIP_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(capicxx_generate_someip FIDL_FILE FDEPL_FILE OUTPUT_FILES)
    find_package(Java COMPONENTS Runtime)
    if(NOT Java_FOUND)
        message(FATAL_ERROR "Java not found and is required for CommonAPI code generators")
    endif()
    if(NOT CAPICXX_GENERATOR_PATH)
        set(CAPICXX_GENERATOR_PATH ${CMAKE_SYSTEM_PREFIX_PATH})
    endif()
    foreach(SEARCH_PATH ${CAPICXX_GENERATOR_PATH})
        file(GLOB_RECURSE LAUNCHERS "${SEARCH_PATH}/org.eclipse.equinox.launcher_*.jar")
        foreach(LAUNCHER ${LAUNCHERS})
            if(NOT CAPICXX_CORE_GENERATOR AND LAUNCHER MATCHES "commonapi-core-generator")
                set(CAPICXX_CORE_GENERATOR ${LAUNCHER})
            elseif(NOT CAPICXX_SOMEIP_GENERATOR AND LAUNCHER MATCHES "commonapi-someip-generator")
                set(CAPICXX_SOMEIP_GENERATOR ${LAUNCHER})
            endif()
        endforeach()
        if(CAPICXX_CORE_GENERATOR AND CAPICXX_SOMEIP_GENERATOR)
            break()
        endif()
    endforeach()
    if(NOT CAPICXX_CORE_GENERATOR)
        message(FATAL_ERROR "CommonAPI core code generator not found")
    endif()
    message(STATUS "Found CommonAPI core code generator: ${CAPICXX_CORE_GENERATOR}")
    if(NOT CAPICXX_SOMEIP_GENERATOR)
        message(FATAL_ERROR "CommonAPI SOME/IP code generator not found")
    endif()
    message(STATUS "Found CommonAPI SOME/IP code generator: ${CAPICXX_SOMEIP_GENERATOR}")
    add_custom_command(
        OUTPUT ${OUTPUT_FILES}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${FIDL_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${FDEPL_FILE}
        COMMAND ${CMAKE_COMMAND} -E env bash -c "if ! LOG_OUTPUT=`java -Dlog4j.configuration=file://${_CAPICXX_GENERATE_SOMEIP_BASE_DIR}/capicxx_gen_log4j_config.xml -jar ${CAPICXX_CORE_GENERATOR} -sk -d ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${FIDL_FILE}`; then echo \"${LOG_OUTPUT}\" >&2; exit -1; else echo \"Finished generating CommonAPI SOME/IP files for ${FIDL_FILE}\"; fi"
        COMMAND ${CMAKE_COMMAND} -E env bash -c "if ! LOG_OUTPUT=`java -Dlog4j.configuration=file://${_CAPICXX_GENERATE_SOMEIP_BASE_DIR}/capicxx_gen_log4j_config.xml -jar ${CAPICXX_SOMEIP_GENERATOR} -d ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${FDEPL_FILE}`; then echo \"${LOG_OUTPUT}\" >&2; exit -1; else echo \"Finished generating CommonAPI SOME/IP files for ${FDEPL_FILE}\"; fi"
        COMMENT "Generating CommonAPI SOME/IP files for ${FIDL_FILE} and ${FDEPL_FILE}"
        VERBATIM
    )
endfunction()
