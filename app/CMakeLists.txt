find_package(ZeroMQ CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)

if (TARGET ZeroMQ::libzmq)
  set(ZMQ_TGT ZeroMQ::libzmq)
elseif (TARGET libzmq)
  set(ZMQ_TGT libzmq)
else()
  message(FATAL_ERROR "Could not find imported target for libzmq")
endif()

#  me_pipeline (end-to-end)
add_executable(me_pipeline
  src/me_pipeline_main.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/MDToOrderBookConsumer.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/MatchingEngineWorker.cpp
  ${CMAKE_SOURCE_DIR}/feed/src/ZmqFeedSubscriber.cpp
  ${CMAKE_SOURCE_DIR}/app/src/BookCrossValidator.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/EngineBook.cpp
)
target_include_directories(me_pipeline PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(me_pipeline PRIVATE trading_engine ${ZMQ_TGT} cppzmq)
target_compile_definitions(me_pipeline PRIVATE
  TRADING_USE_ORDERBOOK_SINK=1
  TRADING_USE_REAL_ME=0
)

#  Order gateway CLI (no zmq) 
add_executable(oi_cli src/oi_cli.cpp)
target_link_libraries(oi_cli PRIVATE trading_engine)

#  Recorder (needs ZMQ/cppzmq) 
add_executable(md_record src/md_record_main.cpp)
target_include_directories(md_record PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(md_record PRIVATE trading_engine ${ZMQ_TGT} cppzmq)

#  Replayer (needs ZMQ/cppzmq)
add_executable(md_replay src/md_replay_main.cpp)
target_include_directories(md_replay PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(md_replay PRIVATE trading_engine ${ZMQ_TGT} cppzmq)

