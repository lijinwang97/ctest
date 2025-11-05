#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "prometheus-cpp::core" for configuration "Debug"
set_property(TARGET prometheus-cpp::core APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(prometheus-cpp::core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libprometheus-cpp-core.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS prometheus-cpp::core )
list(APPEND _IMPORT_CHECK_FILES_FOR_prometheus-cpp::core "${_IMPORT_PREFIX}/lib/libprometheus-cpp-core.a" )

# Import target "prometheus-cpp::pull" for configuration "Debug"
set_property(TARGET prometheus-cpp::pull APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(prometheus-cpp::pull PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C;CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libprometheus-cpp-pull.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS prometheus-cpp::pull )
list(APPEND _IMPORT_CHECK_FILES_FOR_prometheus-cpp::pull "${_IMPORT_PREFIX}/lib/libprometheus-cpp-pull.a" )

# Import target "prometheus-cpp::push" for configuration "Debug"
set_property(TARGET prometheus-cpp::push APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(prometheus-cpp::push PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libprometheus-cpp-push.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS prometheus-cpp::push )
list(APPEND _IMPORT_CHECK_FILES_FOR_prometheus-cpp::push "${_IMPORT_PREFIX}/lib/libprometheus-cpp-push.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
