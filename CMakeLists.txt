CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(WebcamViewer)

file(GLOB_RECURSE ALL_SRC
  "src/*.h"
  "src/*.cc")

add_definitions(-DUNICODE -D_UNICODE)
add_executable(WebcamViewer WIN32 ${ALL_SRC})
