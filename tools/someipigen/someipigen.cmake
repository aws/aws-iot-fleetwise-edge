# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

pybind11_add_module(someipigen
    tools/someipigen/src/SignalManager.cpp
    tools/someipigen/src/bindings.cpp
    tools/someipigen/src/ExampleSomeipInterfaceStubImpl.cpp
    $<TARGET_OBJECTS:fwe-someip-example>
)

target_include_directories(someipigen PUBLIC
    ${JSONCPP_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${VSOMEIP_INCLUDE_DIR}
    ${COMMONAPI_INCLUDE_DIRS}
    ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(someipigen PRIVATE
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
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/tools/someipigen/linker.lds
)

# This will create someipigen.so without the 'lib' prefix
set_target_properties(someipigen PROPERTIES
    PREFIX ""
    OUTPUT_NAME "someipigen"
    SUFFIX ".so"
)

# Disabling -Werror specifically for this target due to an issue:
# https://github.com/pybind/pybind11/issues/1917
target_compile_options(someipigen PRIVATE
    -Wno-error
)

install(TARGETS someipigen
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)
