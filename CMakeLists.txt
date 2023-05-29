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

add_library(sio INTERFACE)
add_library(sio::sio ALIAS sio)
target_include_directories(sio
  INTERFACE
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source>
      $<INSTALL_INTERFACE:include>)
target_sources(sio INTERFACE
  FILE_SET sio_headers
  TYPE HEADERS
  BASE_DIRS source
  FILES
    source/sio/sequence/empty_sequence.hpp
    source/sio/sequence/first.hpp
    source/sio/sequence/ignore_all.hpp
    source/sio/sequence/iterate.hpp
    source/sio/sequence/sequence_concepts.hpp
    source/sio/sequence/then_each.hpp
    source/sio/sequence/transform_each.hpp
    source/sio/sequence/zip.hpp)
target_link_libraries(sio INTERFACE STDEXEC::stdexec)
target_compile_features(sio INTERFACE cxx_std_20)

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