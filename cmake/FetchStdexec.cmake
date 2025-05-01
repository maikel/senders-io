include(FetchContent)
FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git

  # branch: main. date: 2025-04-28
  GIT_TAG b0b18b82e9a9166af7a51bceeb2d7229f7bef33d
)

set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "close stdexec examples")
message(STATUS "Downloading stdexec")
FetchContent_MakeAvailable(stdexec)

