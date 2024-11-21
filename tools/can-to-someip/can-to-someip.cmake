# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

add_executable(can-to-someip
    tools/can-to-someip/main.cpp
)

install(TARGETS can-to-someip
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

target_include_directories(can-to-someip PUBLIC
    ${VSOMEIP_INCLUDE_DIRS}
)

target_link_libraries(can-to-someip
    ${VSOMEIP_LIBRARIES}
    Boost::system
    Boost::thread
    Boost::filesystem
    Boost::program_options
)
