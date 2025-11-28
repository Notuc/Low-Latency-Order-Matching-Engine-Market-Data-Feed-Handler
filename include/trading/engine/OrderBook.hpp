#pragma once
#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "trading/common/Types.hpp"
#include "trading/feed/Wire.hpp"   // LevelSummary

namespace trading::engine {

using namespace trading::common;

// L2/L3 OrderBook (single-writer).
class OrderBook {
  public:
    struct L3Order {
      OrderId   order_id{kInvalidOrderId};
      Qty       leaves{0};
      MonoTime  entry_time{};
   };

  // Cheap validation of basic invariants.
  bool validate() const {
    // best_bid < best_ask if both exist
    if (!bids_.empty() && !asks_.empty()) {
      if (bids_.begin()->first >= asks_.begin()->first) return false;
    }
    // For every level: if order_count>0, agg_qty>0 (unsigned, so only zero to check)
    for (const auto& [px, L] : bids_) {
      (void)px;
      if (L.order_count > 0 && L.agg_qty == 0) return false;
    }
    for (const auto& [px, L] : asks_) {
      (void)px;
      if (L.order_count > 0 && L.agg_qty == 0) return false;
    }
    return true;
  }

  // Hash the full L2 state (order-insensitive within each side’s map order).
  std::uint64_t hashL2(std::uint32_t depth_limit = 0) const {
    auto mix = [](std::uint64_t h, std::uint64_t v) {
      // xorshift + mul
      h ^= v + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
      return h;
    };
    std::uint64_t h = 0xcbf29ce484222325ULL;
    std::uint32_t n = 0;

    for (const auto& [px, L] : bids_) {
      if (depth_limit && n++ >= depth_limit) break;
      h = mix(h, static_cast<std::uint64_t>(px));
      h = mix(h, static_cast<std::uint64_t>(L.agg_qty));
      h = mix(h, static_cast<std::uint64_t>(L.order_count));
    }
    n = 0;
    for (const auto& [px, L] : asks_) {
      if (depth_limit && n++ >= depth_limit) break;
      h = mix(h, static_cast<std::uint64_t>(px));
      h = mix(h, static_cast<std::uint64_t>(L.agg_qty));
      h = mix(h, static_cast<std::uint64_t>(L.order_count));
    }
    return h;
  }

  struct Level {
    Qty                 agg_qty{0};
    std::uint32_t       order_count{0};
    std::list<L3Order>  fifo;              // populated when using L3 ops
  };

  // Comparator for bids (highest price first).
  struct Desc {
    bool operator()(PriceTicks a, PriceTicks b) const noexcept { return a > b; }
  };

  // L2 operations 

  // Upsert or insert a level with aggregated values.
  void upsertLevel(Side side, PriceTicks px, Qty qty, std::uint32_t cnt) {
    if (qty == 0 || cnt == 0) { eraseLevel(side, px); return; }
    if (side == Side::Bid) {
      auto& L = bids_[px];
      L.agg_qty = qty;
      L.order_count = cnt;
    } else {
      auto& L = asks_[px];
      L.agg_qty = qty;
      L.order_count = cnt;
    }
  }

  // Remove a level fully.
  void eraseLevel(Side side, PriceTicks px) {
    if (side == Side::Bid) bids_.erase(px);
    else                   asks_.erase(px);
  }

  // Derived top-of-book from L2 levels.
  TopOfBook top() const {
    TopOfBook t{};
    if (!bids_.empty()) {
      const auto& [px, L] = *bids_.begin();  // highest bid at begin()
      t.best_bid_price = px; t.best_bid_qty = L.agg_qty;
    }
    if (!asks_.empty()) {
      const auto& [px, L] = *asks_.begin();  // lowest ask at begin()
      t.best_ask_price = px; t.best_ask_qty = L.agg_qty;
    }
    return t;
  }

  std::size_t levelCount() const noexcept { return bids_.size() + asks_.size(); }

  // Depth-limited L2 snapshot.
  std::vector<trading::feed::LevelSummary> snapshotL2(std::uint32_t depth) const {
    using trading::feed::LevelSummary;
    std::vector<LevelSummary> out; out.reserve(depth * 2);

    std::uint32_t n = 0;
    for (const auto& [px, L] : bids_) {
      if (n++ >= depth) break;
      out.push_back(LevelSummary{Side::Bid, px, L.agg_qty, L.order_count});
    }
    n = 0;
    for (const auto& [px, L] : asks_) {
      if (n++ >= depth) break;
      out.push_back(LevelSummary{Side::Ask, px, L.agg_qty, L.order_count});
    }
    return out;
  }

  // Compatibility wrappers used by tools/tests 
  // Feed-style L2 apply: aggregate upsert/erase
  inline void on_l2_upsert(Side s,
                           PriceTicks px,
                           Qty agg_qty,
                           std::uint32_t order_count)
  {
    upsertLevel(s, px, agg_qty, order_count);
  }

  inline void on_l2_erase(Side s, PriceTicks px)
  {
    eraseLevel(s, px);
  }

  // L3 operations (engine-side) 

  void addOrder(const Order& o) {
    if (o.side == Side::Bid) {
      auto& L = bids_[o.price];
      L.fifo.push_back(L3Order{o.order_id, o.qty, o.entry_time});
      L.agg_qty += o.qty;
      L.order_count += 1;
      l3_index_[o.order_id] = { Side::Bid, o.price, std::prev(L.fifo.end()) };
    } else {
      auto& L = asks_[o.price];
      L.fifo.push_back(L3Order{o.order_id, o.qty, o.entry_time});
      L.agg_qty += o.qty;
      L.order_count += 1;
      l3_index_[o.order_id] = { Side::Ask, o.price, std::prev(L.fifo.end()) };
    }
  }

  bool cancelOrder(OrderId id) {
    auto it = l3_index_.find(id);
    if (it == l3_index_.end()) return false;
    auto [side, px, lit] = it->second;

    if (side == Side::Bid) {
      auto bit = bids_.find(px); if (bit == bids_.end()) { l3_index_.erase(it); return false; }
      Level& L = bit->second;
      L.agg_qty = (L.agg_qty > lit->leaves ? L.agg_qty - lit->leaves : 0);
      if (L.order_count) --L.order_count;
      L.fifo.erase(lit);
      if (L.order_count == 0 && L.agg_qty == 0 && L.fifo.empty()) bids_.erase(bit);
    } else {
      auto bit = asks_.find(px); if (bit == asks_.end()) { l3_index_.erase(it); return false; }
      Level& L = bit->second;
      L.agg_qty = (L.agg_qty > lit->leaves ? L.agg_qty - lit->leaves : 0);
      if (L.order_count) --L.order_count;
      L.fifo.erase(lit);
      if (L.order_count == 0 && L.agg_qty == 0 && L.fifo.empty()) asks_.erase(bit);
    }

    l3_index_.erase(it);
    return true;
  }

  bool replaceOrder(const Order& o_new) {
    cancelOrder(o_new.prev_order_id != kInvalidOrderId ? o_new.prev_order_id : o_new.order_id);
    addOrder(o_new);
    return true;
  }

 private:
  // Storage
  std::map<PriceTicks, Level, Desc>        bids_; // best at begin()
  std::map<PriceTicks, Level, std::less<>> asks_; // best at begin()

  struct IndexEntry { Side side; PriceTicks px; std::list<L3Order>::iterator it; };
  std::unordered_map<OrderId, IndexEntry> l3_index_;
};

}
