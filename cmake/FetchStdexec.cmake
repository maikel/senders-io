include(FetchContent)
FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git

  # branch: main. date: 2024-08-13
  GIT_TAG 54b38c99e8cbb348c1dd3508fa540728882247db
)

set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "close stdexec examples")
message(STATUS "Downloading stdexec")
FetchContent_MakeAvailable(stdexec)

