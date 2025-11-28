#include <atomic>
#include <thread>
#include <iostream>

#include "trading/common/Types.hpp"
#include "trading/engine/MDEvent.hpp"
#include "trading/engine/Rings.hpp"
#include "trading/feed/Wire.hpp"

#include "trading/common/Types.hpp"
#include "trading/engine/MDEvent.hpp"
#include "trading/engine/Rings.hpp"
#include "trading/engine/OrderBook.hpp"
#include "trading/metrics/BookMetrics.hpp"
#include "trading/feed/Wire.hpp"

using namespace trading::common;
using trading::engine::MDEvent;
using trading::engine::MDRing;
using trading::engine::OrderBook;
using trading::metrics::BookMetrics;
using trading::feed::StreamKind;
using trading::feed::L1Payload;
using trading::feed::L2DeltaPayload;
using trading::feed::Action;

static constexpr std::uint32_t kHealthPeriod = 1000;   // print every N L2
static constexpr std::uint32_t kIdemDepth    = 0;      // 0 = full hash

void md_to_orderbook_consumer(MDRing& md_ring,
                              std::atomic<bool>& stop,
                              OrderBook& ob,
                              BookMetrics& m) {
  MDEvent ev;

  while (!stop.load(std::memory_order_relaxed)) {
    if (!md_ring.try_pop(ev)) { std::this_thread::yield(); continue; }

    if (ev.header.kind == StreamKind::L1) {
      // We treat L1 as advisory. Count it for visibility.
      (void)std::get<L1Payload>(ev.payload);
      m.l1_seen.fetch_add(1, std::memory_order_relaxed);
      continue;
    }

    if (ev.header.kind == StreamKind::L2Delta) {
      const auto& d = std::get<L2DeltaPayload>(ev.payload);

      // Apply once
      if (d.action == Action::Erase) ob.eraseLevel(d.side, d.price);
      else                           ob.upsertLevel(d.side, d.price, d.agg_qty, d.order_count);

      // Optional idempotency self-check: re-apply and compare hash 
      const auto h1 = ob.hashL2(kIdemDepth);
      if (d.action == Action::Erase) ob.eraseLevel(d.side, d.price);
      else                           ob.upsertLevel(d.side, d.price, d.agg_qty, d.order_count);
      const auto h2 = ob.hashL2(kIdemDepth);
      if (h1 == h2) m.l2_idempotent_hits.fetch_add(1, std::memory_order_relaxed);

      // Invariants
      if (!ob.validate()) m.invariants_fail.fetch_add(1, std::memory_order_relaxed);

      // Counter & periodic health print
      const auto n = m.l2_applied.fetch_add(1, std::memory_order_relaxed) + 1;
      if ((n % kHealthPeriod) == 0) {
        const auto tob = ob.top();
        std::cout << "[health] l2_applied=" << n
                  << " l1_seen=" << m.l1_seen.load(std::memory_order_relaxed)
                  << " invariants_fail=" << m.invariants_fail.load(std::memory_order_relaxed)
                  << " idempotent=" << m.l2_idempotent_hits.load(std::memory_order_relaxed)
                  << " levels=" << ob.levelCount()
                  << " bid=" << tob.best_bid_price
                  << " ask=" << tob.best_ask_price
                  << "\n";
      }
    }
  }
}

