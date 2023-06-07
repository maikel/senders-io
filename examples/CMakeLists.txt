cmake_minimum_required(VERSION 3.22)
project(senders-io_examples CXX)

if (PROJECT_IS_TOP_LEVEL)
  find_package(sio REQUIRED)
elseif (NOT TARGET sio::sio)
  message(FATAL_ERROR "sio::sio target is not defined. Please use add_subdirectory to add sio to your project")
endif ()

find_package(fmt REQUIRED)

add_executable(echo_standard_input echo_standard_input.cpp)
target_link_libraries(echo_standard_input PRIVATE sio::sio fmt::fmt)