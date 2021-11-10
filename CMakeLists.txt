cmake_minimum_required(VERSION 3.9)
project(InitKit)

include(FindPkgConfig)

find_package(Doxygen)
find_package(Threads)

if (CMAKE_SYSTEM_NAME MATCHES ".*Linux|Linux")
	pkg_check_modules(libkqueue REQUIRED IMPORTED_TARGET libkqueue)
	set(KQ_LIB PkgConfig::libkqueue)
endif ()

if (DOXYGEN_FOUND)
	set(DOXYGEN_OUTPUT_DIRECTORY doxygen)
	set(DOXYGEN_EXCLUDE_PATTERNS "*/vendor/*")
	doxygen_add_docs(doxygen ${PROJECT_SOURCE_DIR})
endif ()

add_subdirectory(vendor/quickjs)
add_subdirectory(lib/iwng_compat)
add_subdirectory(cmd/schedulerd)