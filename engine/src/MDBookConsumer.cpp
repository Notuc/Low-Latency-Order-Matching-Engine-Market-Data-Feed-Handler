#include <thread>
#include <map>
#include <utility>
#include <iostream>

#include "trading/common/Types.hpp"
#include "trading/containers/SpscRing.hpp"
#include "trading/engine/MDEvent.hpp"
#include "trading/feed/Wire.hpp"

using namespace trading::common;
using trading::containers::SpscRing;
using trading::engine::MDEvent;
using trading::feed::StreamKind;
using trading::feed::L1Payload;
using trading::feed::L2DeltaPayload;
using trading::feed::Action;

struct SimpleBook {
  TopOfBook tob{};
  std::map<std::pair<Side, PriceTicks>, std::pair<Qty, std::uint32_t>> levels;
};

void md_book_consumer(SpscRing<MDEvent>& ring, SimpleBook& book) {
  MDEvent ev;
  while (true) {
    if (!ring.try_pop(ev)) { std::this_thread::yield(); continue; }

    if (ev.header.kind == StreamKind::L1) {
      const auto& p = std::get<L1Payload>(ev.payload);
      book.tob.best_bid_price = p.best_bid_px;
      book.tob.best_bid_qty   = p.best_bid_qty;
      book.tob.best_ask_price = p.best_ask_px;
      book.tob.best_ask_qty   = p.best_ask_qty;

    } else if (ev.header.kind == StreamKind::L2Delta) {
      const auto& d = std::get<L2DeltaPayload>(ev.payload);
      auto key = std::make_pair(d.side, d.price);
      if (d.action == Action::Erase) book.levels.erase(key);
      else                           book.levels[key] = {d.agg_qty, d.order_count};
    }

    // Example: print occasionally to verify flow
    static std::uint64_t c{0};
    if ((++c % 1000) == 0) {
      std::cout << "[book] bid=" << book.tob.best_bid_price
                << " ask=" << book.tob.best_ask_price
                << " levels=" << book.levels.size() << "\n";
    }
  }
}
