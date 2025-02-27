# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

pybind11_add_module(someip_device_shadow_editor
    tools/someip_device_shadow_editor/src/bindings.cpp
    tools/someip_device_shadow_editor/src/DeviceShadowOverSomeipExampleApplication.cpp
    $<TARGET_OBJECTS:fwe-device-shadow-over-someip>
    )

target_include_directories(someip_device_shadow_editor PUBLIC
    ${JSONCPP_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${VSOMEIP_INCLUDE_DIR}
    ${COMMONAPI_INCLUDE_DIRS}
    ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(someip_device_shadow_editor PRIVATE
    ${JSONCPP_LIBRARY}
    CommonAPI-SomeIP
    CommonAPI
    ${VSOMEIP_LIBRARIES}
    Boost::thread
    Boost::filesystem
    Boost::system
    # Workaround for GCC bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68479
    # Only export the PyInit_<MODULE> symbol, otherwise if the .so is statically linked with
    # -static-libstdc++, then std::stringstream breaks when multiple shared libraries are imported.
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/tools/someip_device_shadow_editor/linker.lds
)

# This will create someip_device_shadow_editor.so without the 'lib' prefix
set_target_properties(someip_device_shadow_editor PROPERTIES
    PREFIX ""
    OUTPUT_NAME "someip_device_shadow_editor"
    SUFFIX ".so"
)

# Disabling -Werror specifically for this target due to an issue:
# https://github.com/pybind/pybind11/issues/1917
target_compile_options(someip_device_shadow_editor PRIVATE
    -Wno-error
)

install(TARGETS someip_device_shadow_editor
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)
