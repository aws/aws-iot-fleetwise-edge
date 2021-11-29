# check if Doxygen is installed
find_package(Doxygen)

if(DOXYGEN_FOUND)
  # set input and output files
  set(doxyfileIn  ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in)
  set(doxyfile ${CMAKE_BINARY_DIR}/Doxyfile)

  # request to configure the file
  configure_file(${doxyfileIn} ${doxyfile} @ONLY)
  message("Building documentation")

  # note the option ALL which allows to build the docs together with the application
  add_custom_target( 
    doc_doxygen ALL
    COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM 
  )

else (DOXYGEN_FOUND)
  message("Doxygen needs to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)
