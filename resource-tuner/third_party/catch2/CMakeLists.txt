cmake_minimum_required(VERSION 3.6)

# Build a small static lib from the vendored Catch2 amalgamation
add_library(catch2_amalgamated STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/catch_amalgamated.cpp
)

# Expose the include directory so dependents can include catch_amalgamated.hpp
target_include_directories(catch2_amalgamated PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Ensure PIC for portability when linked into shared libraries/executables
set_target_properties(catch2_amalgamated PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
