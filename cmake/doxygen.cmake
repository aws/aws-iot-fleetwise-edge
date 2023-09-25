# check if Doxygen is installed
find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)

  message(STATUS "Building documentation")

  set(DOXYGEN_DOT_IMAGE_FORMAT svg)
  set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
  set(DOXYGEN_EXTRACT_ALL YES)
  set(DOXYGEN_EXTRACT_PRIVATE YES)
  set(DOXYGEN_EXTRACT_STATIC YES)
  set(DOXYGEN_GENERATE_TREEVIEW YES)

  if (TARGET Doxygen::dot)
    set(DOXYGEN_HAVE_DOT YES)
  endif()

  set(DOXYGEN_HIDE_UNDOC_RELATIONS NO)
  set(DOXYGEN_HTML_DYNAMIC_SECTIONS YES)
  set(DOXYGEN_INCLUDE_PATH "/usr/local/include")
  set(DOXYGEN_INTERACTIVE_SVG YES)
  set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
  set(DOXYGEN_MACRO_EXPANSION YES)
  set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
  set(DOXYGEN_PROJECT_NAME "Reference Implementation for AWS IoT FleetWise")
  set(DOXYGEN_QUIET YES)
  set(DOXYGEN_SOURCE_BROWSER YES)
  set(DOXYGEN_TEMPLATE_RELATIONS YES)
  set(DOXYGEN_TOC_INCLUDE_HEADINGS 5)
  set(DOXYGEN_UML_LOOK YES)
  set(DOXYGEN_EXCLUDE_PATTERNS "*.md")

  doxygen_add_docs(
    doc_doxygen
    ${PROJECT_SOURCE_DIR}/src
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
  )

  # Note: From CMake 3.12 onwards, the ALL option is supported as part of
  # doxygen_add_docs
  # Until then, we have to set it manually
  set_target_properties(doc_doxygen PROPERTIES EXCLUDE_FROM_ALL FALSE)
else (DOXYGEN_FOUND)
  message("Doxygen needs to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)
