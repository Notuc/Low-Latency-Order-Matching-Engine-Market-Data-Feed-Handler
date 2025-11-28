
#pragma once
#include <map>
#include <deque>
#include <unordered_map>
#include <functional>

#include "trading/common/Types.hpp"
#include "trading/engine/OrderFlow.hpp"   // ExecReport, Order, Cmd*

namespace trading::engine {

class EngineBook {
public:
  using Order = trading::common::Order;
  using Side  = trading::common::Side;
  using Price = trading::common::PriceTicks;
  using Qty   = trading::common::Qty;

  // The worker passes a lambda here that pushes ExecReport into ExecRing
  using ExecSink = std::function<void(const ExecReport&)>;

  explicit EngineBook(ExecSink sink);

  // Engine API (price-time book)
  void on_new(Order o);
  void on_cancel(trading::common::OrderId id);
  void on_replace(trading::common::OrderId id, Price new_px, Qty new_qty);

  // Debug/telemetry
  trading::common::TopOfBook top() const;

private:
  struct Node {
    Order ord{};
    Qty   leaves{0};
  };

  // Types the .cpp uses (keep these names) 
  using Queue      = std::deque<Node>;
  using PriceMap   = std::map<Price, Queue, std::greater<Price>>; // bids high->low
  using PriceMapAsc= std::map<Price, Queue, std::less<Price>>;    // asks low->high

  PriceMap    bids_;
  PriceMapAsc asks_;

  // O(1) cancel/replace: order_id -> (side, price, index in deque)
  struct Loc { Side side; Price px; std::size_t idx; };
  std::unordered_map<trading::common::OrderId, Loc> loc_;

  ExecSink sink_;

  // Internals
  void publish(const ExecReport& er) { if (sink_) sink_(er); }
  Queue& queue_for(Side s, Price px);
  PriceMap::iterator    best_bid();
  PriceMapAsc::iterator best_ask();

  static bool is_market(const Order& o);

  void enqueue(Node n);
  void erase_at(const Loc& lc);

  void match_buy(Node& taker);
  void match_sell(Node& taker);
  void fill(Node& maker, Node& taker, Price px, Qty qty);
};

} 
