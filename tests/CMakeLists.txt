cmake_minimum_required(VERSION 3.22)
project(senders-io_tests CXX)

if (PROJECT_IS_TOP_LEVEL)
  find_package(sio REQUIRED)
elseif (NOT TARGET sio::sio)
  message(FATAL_ERROR "sio::sio target is not defined. Please use add_subdirectory to add sio to your project")
endif ()

find_package(Catch2 REQUIRED)

add_executable(test_sio
  test_main.cpp
  test_async_resource.cpp
  test_file_handle.cpp
  test_async_accept.cpp
  net/test_address.cpp
  net/test_endpoint.cpp
  net/test_resolve.cpp
  sequence/test_empty_sequence.cpp
  sequence/test_first.cpp
  sequence/test_ignore_all.cpp
  sequence/test_iterate.cpp
  sequence/test_last.cpp
  sequence/test_merge_each.cpp
  sequence/test_scan.cpp
  sequence/test_transform_each.cpp
  sequence/test_zip.cpp
)
target_include_directories(test_sio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_sio PRIVATE sio::sio Catch2::Catch2)

include(CTest)
add_test(NAME test_sio COMMAND test_sio)