#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <zmq.hpp>

#include "trading/common/Types.hpp"
#include "trading/engine/OrderBook.hpp"   // the market-data OrderBook sink
#include "trading/feed/Wire.hpp"          // defines Action, headers, payloads

using namespace trading::common;
using trading::engine::OrderBook;
using trading::feed::MDHeader;
using trading::feed::StreamKind;
using trading::feed::L2DeltaPayload;
using trading::feed::Action;              // Upsert/Erase live here
using trading::feed::topic_l2;

int main(int argc, char** argv) {
  const std::string symbol = (argc > 1 ? argv[1] : "ETH-USD");
  const std::string pub_ep = (argc > 2 ? argv[2] : "tcp://localhost:6001");
  const int seconds        = (argc > 3 ? std::stoi(argv[3]) : 5);

  zmq::context_t ctx(1);
  zmq::socket_t  sub(ctx, zmq::socket_type::sub);
  sub.set(zmq::sockopt::rcvtimeo, 1000);
  sub.set(zmq::sockopt::linger, 0);
  sub.connect(pub_ep);
  sub.set(zmq::sockopt::subscribe, topic_l2(symbol));

  std::atomic<bool> stop{false};
  OrderBook ob; // MD-driven book (l2 sink)

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  std::uint64_t applied = 0;

  while (std::chrono::steady_clock::now() < deadline) {
    zmq::message_t topic, hmsg, pmsg;
    if (!sub.recv(topic, zmq::recv_flags::none)) continue;
    if (!sub.recv(hmsg, zmq::recv_flags::none))  continue;
    if (!sub.recv(pmsg, zmq::recv_flags::none))  continue;

    const auto h = trading::feed::from_bytes<MDHeader>(hmsg.data(), hmsg.size());
    if (h.kind != StreamKind::L2Delta) continue;

    const auto p = trading::feed::from_bytes<L2DeltaPayload>(pmsg.data(), pmsg.size());

    // NOTE: payload field is 'agg_qty', not 'qty'
    
    if (p.action == Action::Upsert) {
      ob.on_l2_upsert(p.side, p.price, p.agg_qty, p.order_count);
    } else { // Action::Erase
      ob.on_l2_erase(p.side, p.price);
    }
    ++applied;

    if ((applied % 1000) == 0) {
      const auto t = ob.top();
      std::cout << "[replay] applied=" << applied
                << " bid=" << t.best_bid_price
                << " ask=" << t.best_ask_price << "\n";
    }
  }

  const auto t = ob.top();
  std::cout << "[replay] done. applied=" << applied
            << " bid=" << t.best_bid_price
            << " ask=" << t.best_ask_price << "\n";
  return 0;
}
