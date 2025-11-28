# Library 
add_library(trading_engine)
add_library(trading::engine ALIAS trading_engine)

target_sources(trading_engine PRIVATE
    src/MatchingEngine.cpp
    src/OrderBook.cpp
    src/OrderMatchers.cpp
    src/MDBookConsumer.cpp
    src/MDToOrderBookConsumer.cpp 
    src/MatchingEngineWorker.cpp
)

# Public API headers for the engine
target_include_directories(trading_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Pull in global public headers (include/trading/**)
target_link_libraries(trading_engine PUBLIC trading::headers)
target_compile_features(trading_engine PUBLIC cxx_std_17)

