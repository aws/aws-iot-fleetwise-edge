#
#  Tries to find Snappy headers and libraries.
#
#  Usage of this module as follows:
#
#  find_package(Snappy)
#
#  Variables used by this module:
#
#  SNAPPY_ROOT_DIR  Set this variable to the root installation of
#                    Snappy if the module has problems finding
#                    the proper installation path.
#
#  Variables defined by this module:
#
#  SNAPPY_LIBRARIES          The Snappy libraries
#  SNAPPY_INCLUDE_DIR        The location of Snappy headers

# Find Snappy Library
find_package(Snappy REQUIRED)

# Set Library Alias
set(SNAPPY_PKG libsnappy)

# find a directory containing the NAMES
find_path(
        SNAPPY_INCLUDE_DIR
        NAMES snappy.h
        HINTS ${SNAPPY_ROOT_DIR}/include)

message(STATUS "Snappy Libray Found")

# Search for Custom Library Location
find_library(
        SNAPPY_LIBRARIES
        NAMES snappy
        HINTS ${SNAPPY_ROOT_DIR}/lib
)


include(FindPackageHandleStandardArgs)

# Finds the package(s), package(s) are considered found if all variables listed contain
# valid results, e.g. valid file paths.
find_package_handle_standard_args(
        Snappy DEFAULT_MSG
        SNAPPY_LIBRARIES
        SNAPPY_INCLUDE_DIR
)

message(STATUS "SNAPPY_LIBRARY: ${SNAPPY_LIBRARIES}")
message(STATUS "SNAPPY_INCLUDE_DIR:  ${SNAPPY_INCLUDE_DIR}")

include_directories(${SNAPPY_INCLUDE_DIR})


