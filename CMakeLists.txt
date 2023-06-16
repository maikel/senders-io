cmake_minimum_required(VERSION 3.22)
project(senders-io 
  DESCRIPTION "An adaption of Senders/Receivers for async networking and I/O"
  HOMEPAGE_URL "https://github.com/maikel/senders-io"
  LANGUAGES CXX)

find_package(Doxygen)
if (DOXYGEN_FOUND)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_MAN YES)
  doxygen_add_docs(
      doxygen 
      ${CMAKE_CURRENT_SOURCE_DIR}/source
  )
endif()

find_package(stdexec REQUIRED)

add_library(sio
  source/sio/io_uring/buffered_sequence.cpp
  source/sio/io_uring/file_handle.cpp
  source/sio/io_uring/socket_handle.cpp)
add_library(sio::sio ALIAS sio)
target_include_directories(sio
  PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source>
      $<INSTALL_INTERFACE:include>)
target_sources(sio PUBLIC
  FILE_SET sio_headers
  TYPE HEADERS
  BASE_DIRS source
  FILES
    source/sio/net/ip/address.hpp
    source/sio/net/ip/endpoint.hpp
    source/sio/net/ip/resolve.hpp
    source/sio/net/ip/tcp.hpp
    source/sio/net/ip/udp.hpp
    source/sio/sequence/empty_sequence.hpp
    source/sio/sequence/first.hpp
    source/sio/sequence/ignore_all.hpp
    source/sio/sequence/iterate.hpp
    source/sio/sequence/last.hpp
    source/sio/sequence/merge_each.hpp
    source/sio/sequence/reduce.hpp
    source/sio/sequence/scan.hpp
    source/sio/sequence/sequence_concepts.hpp
    source/sio/sequence/then_each.hpp
    source/sio/sequence/transform_each.hpp
    source/sio/sequence/zip.hpp
    source/sio/io_uring/buffered_sequence.hpp
    source/sio/io_uring/file_handle.hpp
    source/sio/io_uring/socket_handle.hpp
    source/sio/async_resource.hpp
    source/sio/concepts.hpp
    source/sio/deferred.hpp
    source/sio/intrusive_queue.hpp
    source/sio/io_concepts.hpp)
target_link_libraries(sio PUBLIC STDEXEC::stdexec)
target_compile_features(sio PUBLIC cxx_std_20)

if (PROJECT_IS_TOP_LEVEL)
  option(SIO_EXAMPLES "Build examples" ON)
else()
  option(SIO_EXAMPLES "Build examples" OFF)
endif()

if (SIO_EXAMPLES)
  add_subdirectory(examples)
endif()

option(BUILD_TESTING "Build tests" ON)
if (BUILD_TESTING)
  add_subdirectory(tests)
endif()

if (NOT CMAKE_SKIP_INSTALL_RULES)
  include(GNUInstallDirs)

  install(
    TARGETS sio
    EXPORT sio-targets
    FILE_SET sio_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT sio-targets
    FILE sio-targets.cmake
    NAMESPACE sio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sio)
  include(CPack)
endif()