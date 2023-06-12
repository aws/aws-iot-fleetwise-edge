add_compile_options(-Wconversion -Wall -Wextra -pedantic -ffunction-sections -fdata-sections)
link_libraries(
  -Wl,--gc-sections # Remove all unreferenced sections
  $<$<BOOL:${FWE_STRIP_SYMBOLS}>:-Wl,-s> # Strip all symbols
)

if(FWE_STATIC_LINK)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  set(Boost_USE_STATIC_LIBS ON)
  link_libraries("-static-libstdc++" "-static-libgcc")
endif()

if(FWE_CODE_COVERAGE)
  add_compile_options("-fprofile-arcs" "-ftest-coverage")
  link_libraries("-lgcov" "--coverage")
endif()

# Only add -Werror when explicitly asked to avoid compilation errors when someone tries a compiler
# different from the ones we use on CI.
if(FWE_WERROR)
  add_compile_options(-Werror)
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

# Ensure that the default include system directories are added to the list of CMake implicit includes.
# This workarounds an issue that happens when using GCC 6 and using system includes (-isystem).
# For more details check: https://bugs.webkit.org/show_bug.cgi?id=161697
macro(DETERMINE_GCC_SYSTEM_INCLUDE_DIRS _lang _compiler _flags _result)
    file(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/dummy" "\n")
    separate_arguments(_buildFlags UNIX_COMMAND "${_flags}")
    execute_process(COMMAND ${_compiler} ${_buildFlags} -v -E -x ${_lang} -dD dummy
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/CMakeFiles OUTPUT_QUIET
                    ERROR_VARIABLE _gccOutput)
    file(REMOVE "${CMAKE_BINARY_DIR}/CMakeFiles/dummy")
    if ("${_gccOutput}" MATCHES "> search starts here[^\n]+\n *(.+) *\n *End of (search) list")
        set(${_result} ${CMAKE_MATCH_1})
        string(REPLACE "\n" " " ${_result} "${${_result}}")
        separate_arguments(${_result})
    endif()
endmacro()

if (CMAKE_COMPILER_IS_GNUCC)
    DETERMINE_GCC_SYSTEM_INCLUDE_DIRS("c" "${CMAKE_C_COMPILER}" "${CMAKE_C_FLAGS}" SYSTEM_INCLUDE_DIRS)
    set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES} ${SYSTEM_INCLUDE_DIRS})
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${SYSTEM_INCLUDE_DIRS})
endif ()

if (CMAKE_COMPILER_IS_GNUCXX)
    DETERMINE_GCC_SYSTEM_INCLUDE_DIRS("c++" "${CMAKE_CXX_COMPILER}" "${CMAKE_CXX_FLAGS}" SYSTEM_INCLUDE_DIRS)
    set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES} ${SYSTEM_INCLUDE_DIRS})
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${SYSTEM_INCLUDE_DIRS})
endif ()
