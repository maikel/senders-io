Set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(Catch2)
