#include <variant>
#include <vector>
#include <thread>
#include <chrono>
#include <iostream>

#include "trading/common/Types.hpp"
#include "trading/engine/Rings.hpp"
#include "trading/engine/EngineBook.hpp"
#include "trading/engine/OrderFlow.hpp"

using namespace trading::common;
using trading::engine::EngineBook;

namespace {

// Helper to push with simple backpressure spin
template <class Ring, class T>
inline void push_or_spin(Ring& r, const T& v) {
  while (!r.try_push(v)) {
    std::this_thread::yield();
  }
}

} 

// struct CmdNew   { Order order; };
// struct CmdCancel{ OrderId order_id; };
// struct CmdReplace{ OrderId order_id; PriceTicks new_price; Qty new_qty; };
// using OrderCmd = std::variant<CmdNew, CmdCancel, CmdReplace>;

void matching_engine_worker(trading::engine::OrderRing& order_ring,
                            trading::engine::ExecRing&  exec_ring,
                            std::atomic<bool>& stop) {
  EngineBook book;
  using OrderCmd = trading::engine::OrderCmd; // wherever you typedef it
  OrderCmd cmd;

  while (!stop.load(std::memory_order_relaxed)) {
    if (!order_ring.try_pop(cmd)) {
      std::this_thread::yield();
      continue;
    }

    std::vector<trading::engine::ExecReport> reports;

    if (std::holds_alternative<trading::engine::CmdNew>(cmd)) {
      const auto& c = std::get<trading::engine::CmdNew>(cmd);
      book.on_new(c.order, reports);
    } else if (std::holds_alternative<trading::engine::CmdCancel>(cmd)) {
      const auto& c = std::get<trading::engine::CmdCancel>(cmd);
      book.on_cancel(c.order_id, reports);
    } else if (std::holds_alternative<trading::engine::CmdReplace>(cmd)) {
      const auto& c = std::get<trading::engine::CmdReplace>(cmd);
      book.on_replace(c.order_id, c.new_price, c.new_qty, reports);
    }

    for (const auto& er : reports) {
      push_or_spin(exec_ring, er);
    }
  }
}
