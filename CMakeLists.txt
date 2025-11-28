cmake_minimum_required(VERSION 3.20)
project(OrderMatchingEng CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Expose include/ (include/trading/...)
add_library(Trading_Headers INTERFACE)
target_include_directories(Trading_Headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
add_library(trading::headers ALIAS Trading_Headers)



add_subdirectory(engine)
add_subdirectory(app)
add_subdirectory(third_party/googletest)
add_subdirectory(tests)
add_subdirectory(feed)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
