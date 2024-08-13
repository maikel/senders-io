cmake_minimum_required(VERSION 3.22)
project(senders-io_tests CXX)

if (PROJECT_IS_TOP_LEVEL)
  find_package(sio REQUIRED)
elseif (NOT TARGET sio::sio)
  message(FATAL_ERROR "sio::sio target is not defined. Please use add_subdirectory to add sio to your project")
endif ()

add_executable(test_sio
  sequence/test_first.cpp
  sequence/test_last.cpp
  sequence/test_fork.cpp
  sequence/test_merge_each.cpp
  sequence/test_scan.cpp
  sequence/test_transform_each.cpp
  sequence/test_repeat.cpp
  sequence/test_zip.cpp
  sequence/test_finally.cpp
  sequence/test_buffered_sequence.cpp
  test_const_buffer_subspan.cpp
  test_async_resource.cpp
  test_file_handle.cpp
  test_async_accept.cpp
  test_async_mutex.cpp
  # test_async_channel.cpp
  test_memory_pool.cpp
  test_read_batched.cpp
  test_tap.cpp
  net/test_can_endpoint.cpp
  # net/test_can_socket.cpp
  net/test_address.cpp
  net/test_endpoint.cpp
  net/test_resolve.cpp
  net/test_socket_handle.cpp
)
target_include_directories(test_sio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_sio PRIVATE sio::sio Catch2::Catch2WithMain)

include(CTest)
add_test(NAME test_sio COMMAND test_sio)
