#include "trading/engine/EngineBook.hpp"

namespace trading::engine {
using namespace trading::common;

static inline bool is_limit_cross(Side takerSide, PriceTicks takerPx, PriceTicks oppPx) {
  if (takerPx == kNoPrice || oppPx == kNoPrice) return true; // MARKET matches
  return (takerSide == Side::Bid) ? (takerPx >= oppPx) : (takerPx <= oppPx);
}

bool EngineBook::is_market(const Order& o) { return o.type == OrderType::Market; }

EngineBook::EngineBook(ExecSink sink) : sink_(std::move(sink)) {}

EngineBook::Queue& EngineBook::queue_for(Side s, Price px) {
  return (s == Side::Bid) ? bids_[px] : asks_[px];
}

EngineBook::PriceMap::iterator EngineBook::best_bid() { return bids_.begin(); }
EngineBook::PriceMapAsc::iterator EngineBook::best_ask() { return asks_.begin(); }

TopOfBook EngineBook::top() const {
  TopOfBook t{};
  if (!bids_.empty()) {
    t.best_bid_price = bids_.begin()->first;
    t.best_bid_qty   = bids_.begin()->second.empty() ? 0 : bids_.begin()->second.front().leaves;
  }
  if (!asks_.empty()) {
    t.best_ask_price = asks_.begin()->first;
    t.best_ask_qty   = asks_.begin()->second.empty() ? 0 : asks_.begin()->second.front().leaves;
  }
  return t;
}



void EngineBook::on_new(Order o) {
  Node taker{ o, o.qty };

  {
    ExecReport er{};                       // zero-init; assign fields to avoid brace-order mismatches
    er.order_id   = o.order_id;
    er.exec_type  = ExecType::New;
    er.status     = ExecStatus::New;
    er.last_qty   = 0;
    er.last_px    = kNoPrice;
    er.leaves_qty = taker.leaves;
    publish(er);
  }

  if (o.side == Side::Bid) match_buy(taker);
  else                     match_sell(taker);

  if (taker.leaves > 0 && !is_market(o)) {
    enqueue(std::move(taker));
  } else if (taker.leaves == 0) {
    ExecReport er{};
    er.order_id   = o.order_id;
    er.exec_type  = ExecType::Trade;
    er.status     = ExecStatus::Filled;
    er.last_qty   = 0;
    er.last_px    = kNoPrice;
    er.leaves_qty = 0;
    publish(er);
  }
}

void EngineBook::on_cancel(OrderId id) {
  auto it = loc_.find(id);
  if (it == loc_.end()) return;
  erase_at(it->second);
  loc_.erase(it);

  ExecReport er{};
  er.order_id   = id;
  er.exec_type  = ExecType::Canceled;
  er.status     = ExecStatus::Canceled;
  er.last_qty   = 0;
  er.last_px    = kNoPrice;
  er.leaves_qty = 0;
  publish(er);
}

void EngineBook::on_replace(OrderId id, Price new_px, Qty new_qty) {
  auto it = loc_.find(id);
  if (it == loc_.end()) return;

  // pull out node
  Loc lc = it->second;
  Queue& q = (lc.side == Side::Bid) ? bids_[lc.px] : asks_[lc.px];
  Node n   = q[lc.idx];

  // erase old spot
  erase_at(lc);
  loc_.erase(it);

  // adjust qty
  if (new_qty < n.ord.qty) {
    const Qty cut = n.ord.qty - new_qty;
    n.leaves = (n.leaves > cut ? n.leaves - cut : 0);
  } else if (new_qty > n.ord.qty) {
    n.leaves += (new_qty - n.ord.qty);
  }
  n.ord.qty = new_qty;

  const bool px_change = (new_px != kNoPrice && new_px != n.ord.price);
  if (px_change) n.ord.price = new_px;

  // price change -> loses priority; re-enter as new order (will match then rest)
  on_new(n.ord);

  ExecReport er{};
  er.order_id   = n.ord.order_id;
  er.exec_type  = ExecType::Replaced;
  er.status     = ExecStatus::Replaced;
  er.last_qty   = 0;
  er.last_px    = kNoPrice;
  er.leaves_qty = n.leaves;
  publish(er);
}

void EngineBook::enqueue(Node n) {
  auto& q = queue_for(n.ord.side, n.ord.price);
  q.emplace_back(std::move(n));
  const std::size_t idx = q.size() - 1;
  loc_[q.back().ord.order_id] = Loc{ q.back().ord.side, q.back().ord.price, idx };
}

void EngineBook::erase_at(const Loc& lc) {
  auto& q = (lc.side == Side::Bid) ? bids_[lc.px] : asks_[lc.px];
  if (lc.idx >= q.size()) return;

  const OrderId victim = q[lc.idx].ord.order_id;
  const bool last = (lc.idx == q.size() - 1);
  if (!last) std::swap(q[lc.idx], q.back());
  q.pop_back();
  loc_.erase(victim);

  if (!last) {
    auto& moved = q[lc.idx].ord;
    auto it = loc_.find(moved.order_id);
    if (it != loc_.end()) it->second.idx = lc.idx;
  }

  if (q.empty()) {
    if (lc.side == Side::Bid) bids_.erase(lc.px);
    else                      asks_.erase(lc.px);
  }
}

void EngineBook::fill(Node& maker, Node& taker, Price px, Qty qty) {
  maker.leaves -= qty;
  taker.leaves -= qty;

  // taker fill
  {
    ExecReport er{};
    er.order_id   = taker.ord.order_id;
    er.exec_type  = ExecType::Trade;
    er.status     = (taker.leaves == 0 ? ExecStatus::Filled : ExecStatus::PartiallyFilled);
    er.last_qty   = qty;
    er.last_px    = px;
    er.leaves_qty = taker.leaves;
    publish(er);
  }
  // maker fill
  {
    ExecReport er{};
    er.order_id   = maker.ord.order_id;
    er.exec_type  = ExecType::Trade;
    er.status     = (maker.leaves == 0 ? ExecStatus::Filled : ExecStatus::PartiallyFilled);
    er.last_qty   = qty;
    er.last_px    = px;
    er.leaves_qty = maker.leaves;
    publish(er);
  }
}

void EngineBook::match_buy(Node& taker) {
  while (taker.leaves > 0 && !asks_.empty()) {
    auto it = best_ask();
    const Price ask_px = it->first;
    if (!is_limit_cross(Side::Bid, taker.ord.price, ask_px)) break;

    auto& q = it->second;
    while (taker.leaves > 0 && !q.empty()) {
      Node& maker = q.front();
      const Qty dq = std::min(maker.leaves, taker.leaves);
      fill(maker, taker, ask_px, dq);
      if (maker.leaves == 0) { loc_.erase(maker.ord.order_id); q.pop_front(); }
    }
    if (q.empty()) asks_.erase(it);
  }
}

void EngineBook::match_sell(Node& taker) {
  while (taker.leaves > 0 && !bids_.empty()) {
    auto it = best_bid();
    const Price bid_px = it->first;
    if (!is_limit_cross(Side::Ask, taker.ord.price, bid_px)) break;

    auto& q = it->second;
    while (taker.leaves > 0 && !q.empty()) {
      Node& maker = q.front();
      const Qty dq = std::min(maker.leaves, taker.leaves);
      fill(maker, taker, bid_px, dq);
      if (maker.leaves == 0) { loc_.erase(maker.ord.order_id); q.pop_front(); }
    }
    if (q.empty()) bids_.erase(it);
  }
}

} 
