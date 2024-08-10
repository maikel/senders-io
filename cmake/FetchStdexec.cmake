Set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)
FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
  GIT_TAG 9428f3570b7778a3b60659ea5dfec0d2447e7ca7
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(stdexec)

