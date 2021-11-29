# -Werror will be eventually added when target toolchain is decided
add_compile_options(-Wconversion -Wall -Wextra -pedantic -ffunction-sections -fdata-sections)
link_libraries(
  -Wl,--gc-sections # Remove all unreferenced sections
  $<$<BOOL:${FWE_STRIP_SYMBOLS}>:-Wl,-s> # Strip all symbols
)

if(FWE_STATIC_LINK)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  link_libraries("-static-libstdc++" "-static-libgcc")
endif()

if(FWE_CODE_COVERAGE)
  add_compile_options("-fprofile-arcs" "-ftest-coverage")
  link_libraries("-lgcov" "--coverage")
endif()

# optimization -O3 is added by default by CMake when build type is RELEASE
add_compile_options(
    $<$<CONFIG:DEBUG>:-O0> # Debug: Optimize for debugging
    $<$<CONFIG:DEBUG>:-DDEBUG>
)

# For UNIX like systems, set the Linux macro
if(UNIX)
  add_compile_options("-DIOTFLEETWISE_LINUX")
endif()

# Add compiler flags for security. 
if(FWE_SECURITY_COMPILE_FLAGS)
  add_compile_options("-fstack-protector")
endif()
