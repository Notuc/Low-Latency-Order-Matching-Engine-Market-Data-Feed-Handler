#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <string>

#include "trading/common/Types.hpp"
#include "trading/engine/Rings.hpp"                 // MDRing / OrderRing / ExecRing
#include "trading/engine/OrderFlow.hpp"             // CmdNew/CmdCancel/CmdReplace/OrderCmd
#include "trading/engine/MDEvent.hpp"
#include "trading/metrics/BookMetrics.hpp"
#include "trading/engine/MatchingEngineWorker.hpp"  // matching_engine_worker()

#ifndef TRADING_USE_ORDERBOOK_SINK
#define TRADING_USE_ORDERBOOK_SINK 1
#endif
#ifndef TRADING_USE_REAL_ME
#define TRADING_USE_REAL_ME 0
#endif

#if TRADING_USE_ORDERBOOK_SINK
  #include "trading/engine/OrderBook.hpp"
#endif
#if TRADING_USE_REAL_ME
  #include "trading/engine/MatchingEngine.hpp"
#endif

using namespace trading::common;

//  externs from other compilation units 

// ZMQ subscriber that feeds the MDRing until 'stop' is set.
int run_md_subscriber_ring_until(const std::string& symbol,
                                 const std::string& pub_ep,
                                 const std::string& req_ep,
                                 std::uint32_t depth,
                                 trading::engine::MDRing& ring,
                                 std::atomic<bool>& stop);

// MD → OrderBook consumer (with metrics)
#if TRADING_USE_ORDERBOOK_SINK
void md_to_orderbook_consumer(trading::engine::MDRing& md_ring,
                              std::atomic<bool>& stop,
                              trading::engine::OrderBook& order_book,
                              trading::metrics::BookMetrics& metrics);
#else
void md_to_orderbook_consumer(trading::engine::MDRing& md_ring,
                              std::atomic<bool>& stop);
#endif

// Cross-validator: compare OrderBook vs simulator snapshot (REQ endpoint)
void book_cross_validator_loop(trading::engine::OrderBook& ob,
                               trading::metrics::BookMetrics& m,
                               const std::string& symbol,
                               const std::string& req_ep,
                               std::uint32_t depth,
                               std::atomic<bool>& stop);

int main(int argc, char** argv) {
  //  CLI args (symbol, PUB, REQ, depth)
  const std::string symbol = (argc > 1 ? argv[1] : "ETH-USD");
  const std::string pub_ep = (argc > 2 ? argv[2] : "tcp://localhost:6001");
  const std::string req_ep = (argc > 3 ? argv[3] : "tcp://localhost:6002");
  const std::uint32_t depth = (argc > 4 ? static_cast<std::uint32_t>(std::stoul(argv[4])) : 8u);

  //  Rings 
  using trading::engine::MDRing;
  using trading::engine::OrderRing;
  using trading::engine::ExecRing;

  constexpr std::size_t MD_CAP    = 1u << 15;
  constexpr std::size_t ORDER_CAP = 1u << 14;
  constexpr std::size_t EXEC_CAP  = 1u << 14;

  MDRing    md_ring(MD_CAP);       // market-data events from ZMQ → consumer
  OrderRing order_ring(ORDER_CAP); // orders from gateway/strategy → ME
  ExecRing  exec_ring(EXEC_CAP);   // execs out of ME → printer/OMS

  //  Shared stop flag 
  std::atomic<bool> stop{false};

  // Metrics (health, idempotency, xvalidation, etc.) 
  trading::metrics::BookMetrics metrics;

#if TRADING_USE_ORDERBOOK_SINK
  trading::engine::OrderBook engine_order_book; // MD-driven market book (pricing view)
#endif
#if TRADING_USE_REAL_ME
  trading::engine::MatchingEngine me;          
#endif

  //  Threads 
  std::thread t_xval; 

  // Market-data consumer → OrderBook (and health prints)
  std::thread t_md_consumer([&]{
#if TRADING_USE_ORDERBOOK_SINK
    md_to_orderbook_consumer(md_ring, stop, engine_order_book, metrics);
#else
    md_to_orderbook_consumer(md_ring, stop);
#endif
  });

  // ZMQ subscriber feeding the MDRing  
    std::thread t_md_sub([&]{
    run_md_subscriber_ring_until(symbol, pub_ep, req_ep, depth, md_ring, stop);
  });

  // Matching engine worker (orders → execs)
  std::thread t_me_worker(trading::engine::matching_engine_worker,
                          std::ref(order_ring),
                          std::ref(exec_ring),
                          std::ref(stop));

#if TRADING_USE_ORDERBOOK_SINK
  // Cross-validator: periodically diff our book vs simulator snapshot
  t_xval = std::thread([&]{
    book_cross_validator_loop(engine_order_book, metrics, symbol, req_ep, depth, stop);
  });
#endif

  std::cout << "[me_pipeline] running. Submitting a demo order...\n";

  // A Demo: first a resting maker ask, then a market buy taker (ensures a trade) 
  {
    Order ask{};
    ask.order_id   = 2001;
    ask.client_id  = 7;
    ask.account_id = 1;
    ask.user_id    = 1;
    ask.instrument = 1;
    ask.side       = Side::Ask;
    ask.type       = OrderType::Limit;
    ask.tif        = TimeInForce::GTC;
    ask.price      = 200181;
    ask.qty        = 3;
    ask.leaves_qty = ask.qty;

    (void)order_ring.try_push(trading::engine::OrderCmd{ trading::engine::CmdNew{ .order = ask } });
  }
  {
    Order buy{};
    buy.order_id   = 1001;
    buy.client_id  = 42;
    buy.account_id = 1;
    buy.user_id    = 1;
    buy.instrument = 1;
    buy.side       = Side::Bid;
    buy.type       = OrderType::Market;
    buy.tif        = TimeInForce::GTC;
    buy.price      = kNoPrice;
    buy.qty        = 5;
    buy.leaves_qty = buy.qty;

    (void)order_ring.try_push(trading::engine::OrderCmd{ trading::engine::CmdNew{ .order = buy } });
  }

//  Observe execs briefly (demo) 
auto t0 = std::chrono::steady_clock::now();
while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5)) {
  trading::engine::ExecReport er;
  if (exec_ring.try_pop(er)) {
    const bool is_trade =
        static_cast<int>(er.exec_type) == static_cast<int>(trading::engine::ExecType::Trade);
    std::cout << "[exec] oid=" << er.order_id
              << " type="     << static_cast<int>(er.exec_type)
              << " status="   << static_cast<int>(er.status)
              << " last_qty=" << er.last_qty
              << " last_px="  << (er.last_px == kNoPrice ? 0 : er.last_px)
              << " leaves="   << er.leaves_qty
              << (is_trade ? " trade=yes" : " trade=no")
              << "\n";
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

  //  Stop & join 
  std::cout << "[me_pipeline] stopping...\n";
  stop.store(true, std::memory_order_relaxed);

  t_md_sub.join();
  t_md_consumer.join();
  t_me_worker.join();
  if (t_xval.joinable()) t_xval.join();

  return 0;
}

