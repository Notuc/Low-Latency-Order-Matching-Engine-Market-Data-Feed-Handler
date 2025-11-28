cmake_minimum_required(VERSION 3.16)
project(feed_md CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZeroMQ CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)

# Helper to be portable across distros:
if (TARGET ZeroMQ::libzmq)
  set(ZMQ_TGT ZeroMQ::libzmq)
elseif (TARGET libzmq)
  set(ZMQ_TGT libzmq)
else()
  message(FATAL_ERROR "Could not find imported target for libzmq")
endif()

#  mdsim (publisher + snapshot server)
add_executable(mdsim src/MDSim.cpp)
target_include_directories(mdsim PRIVATE
  ${CMAKE_SOURCE_DIR}/include        # trading/common + trading/feed
)
target_link_libraries(mdsim PRIVATE ${ZMQ_TGT} cppzmq)

#  md_sub (subscriber demo + main)
add_executable(md_sub
  src/ZmqFeedSubscriber.cpp
  ${CMAKE_SOURCE_DIR}/app/src/md_subscriber_main.cpp
)
target_include_directories(md_sub PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(md_sub PRIVATE ${ZMQ_TGT} cppzmq)

# If need for apple devices ZeroMQ bugs, may remove later
#[[  adfasdf

if(APPLE)
  # Let link dirs propagate to rpath automatically
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

  # Ask Homebrew where zeromq lives (works on Intel and Apple Silicon)
  execute_process(
    COMMAND brew --prefix zeromq
    OUTPUT_VARIABLE ZMQ_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  if(ZMQ_PREFIX)
    set(ZMQ_LIB_DIR "${ZMQ_PREFIX}/lib")
    # Add to build and install rpath of both binaries
    set_target_properties(server client PROPERTIES
      BUILD_RPATH   "${ZMQ_LIB_DIR}"
      INSTALL_RPATH "${ZMQ_LIB_DIR}"
      MACOSX_RPATH  ON
    )
  endif()
endif() ]]
