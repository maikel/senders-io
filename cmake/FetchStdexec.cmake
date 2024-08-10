Set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)
FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/Runner-2019/stdexec.git
  GIT_TAG update-with-senders-io 
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(stdexec)

