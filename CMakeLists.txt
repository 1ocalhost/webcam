CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(webcam)

file(GLOB_RECURSE ALL_SRC
  "src/*.h"
  "src/*.cc")

set(CMAKE_CXX_FLAGS_RELEASE "/MT")
add_definitions(-DUNICODE -D_UNICODE)
add_executable(webcam WIN32 ${ALL_SRC} "res/res.rc")
