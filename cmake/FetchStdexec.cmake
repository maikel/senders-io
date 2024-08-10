include(FetchContent)
FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/Runner-2019/stdexec.git
  GIT_TAG update-with-senders-io 
)

set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "close stdexec examples")
message(STATUS "Downloading stdexec......")
FetchContent_MakeAvailable(stdexec)

