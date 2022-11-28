# Extra libraries are required to statically link with the AWS SDK. These are not always found by CMake, hence:
# - When FWE_STATIC_LINK is ON and FWE_AWS_SDK_EXTRA_LIBS is ON, automatically find the standard libraries: libcurl, libssl, libcrypto, libz
# - When FWE_AWS_SDK_EXTRA_LIBS is a list of libs, use those
# - When FWE_AWS_SDK_EXTRA_LIBS is OFF, don't link any extra libs
if(FWE_AWS_SDK_EXTRA_LIBS STREQUAL "ON")
  if(NOT FWE_STATIC_LINK)
    set(FWE_AWS_SDK_EXTRA_LIBS "")
  else()
    set(OLD_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_library(CURL_LIBRARY NAMES curl)
    find_library(OPENSSL_SSL_LIBRARY NAMES ssl)
    find_library(OPENSSL_CRYPTO_LIBRARY NAMES crypto)
    find_library(ZLIB_LIBRARY NAMES z)
    set(FWE_AWS_SDK_EXTRA_LIBS "${CURL_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${ZLIB_LIBRARY}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_CMAKE_FIND_LIBRARY_SUFFIXES})
  endif()
elseif(FWE_AWS_SDK_EXTRA_LIBS STREQUAL "OFF")
  set(FWE_AWS_SDK_EXTRA_LIBS "")
endif()
separate_arguments(FWE_AWS_SDK_EXTRA_LIBS)

add_subdirectory(bootstrap)
add_subdirectory(iotcpp)