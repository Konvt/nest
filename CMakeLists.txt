cmake_minimum_required(VERSION 3.21)

project(NEST_PROJECT
        LANGUAGES CXX
        DESCRIPTION "A C++20 implementation of the C++26 proposed `std::hive` container."
        HOMEPAGE_URL "https://github.com/Konvt/nest")

add_library(nest INTERFACE)
add_library(nest::nest ALIAS nest)

target_compile_features(nest INTERFACE cxx_std_20)
target_include_directories(
  nest INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if(PROJECT_IS_TOP_LEVEL)
  add_executable(nest_test nest_test.cpp)
  target_link_libraries(nest_test PRIVATE nest::nest)
  target_compile_features(nest_test PRIVATE cxx_std_23)
endif()
