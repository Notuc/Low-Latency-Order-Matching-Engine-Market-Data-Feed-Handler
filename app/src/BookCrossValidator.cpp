
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <zmq.hpp>

#include "trading/engine/OrderBook.hpp"
#include "trading/metrics/BookMetrics.hpp"
#include "trading/feed/Wire.hpp"

using trading::engine::OrderBook;
using trading::metrics::BookMetrics;
using namespace trading::feed;

namespace {

// Return number of mismatched levels and optionally collect up to K examples
static std::size_t diff_levels(const std::vector<LevelSummary>& sim,
                               const std::vector<LevelSummary>& mine,
                               std::vector<LevelSummary>* examples, std::size_t max_examples = 5) {
  auto key = [](const LevelSummary& L){ return std::make_pair(L.side, L.price); };

  std::size_t mismatches = 0;

  // sim -> mine
  for (const auto& L : sim) {
    bool found = false;
    for (const auto& R : mine) {
      if (key(L) == key(R)) {
        found = true;
        if (L.agg_qty != R.agg_qty || L.order_count != R.order_count) {
          ++mismatches;
          if (examples && examples->size() < max_examples) examples->push_back(L);
        }
        break;
      }
    }
    if (!found) {
      ++mismatches;
      if (examples && examples->size() < max_examples) examples->push_back(L);
    }
  }

  // mine -> sim 
  for (const auto& R : mine) {
    bool found = false;
    for (const auto& L : sim) {
      if (key(L) == key(R)) { found = true; break; }
    }
    if (!found) {
      ++mismatches;
      if (examples && examples->size() < max_examples) examples->push_back(R);
    }
  }

  return mismatches;
}

} 


void book_cross_validator_loop(OrderBook& ob,
                               BookMetrics& m,
                               const std::string& symbol,
                               const std::string& req_ep,
                               std::uint32_t depth,
                               std::atomic<bool>& stop) {
  zmq::context_t ctx(1);
  zmq::socket_t req(ctx, zmq::socket_type::req);
  req.set(zmq::sockopt::linger, 0);
  req.set(zmq::sockopt::rcvtimeo, 500);
  req.connect(req_ep);

  using namespace std::chrono_literals;

  while (!stop.load(std::memory_order_relaxed)) {
    // Request an L2 snapshot
    const auto txt = encode_snap_req(SnapRequest{"L2", symbol, depth});
    zmq::message_t msg(txt.data(), txt.size());
    if (!req.send(msg, zmq::send_flags::none)) {
      std::this_thread::sleep_for(500ms);
      continue;
    }

    //  Receive frames: [MDHeader][L2SnapHeader][array of LevelSummary]
    zmq::message_t hmsg, shmsg, arr;
    if (!req.recv(hmsg)) { std::this_thread::sleep_for(300ms); continue; }
    if (!req.recv(shmsg)) { std::this_thread::sleep_for(300ms); continue; }
    if (!req.recv(arr))   { std::this_thread::sleep_for(300ms); continue; }

    // Validate header kind
    if (hmsg.size() != sizeof(MDHeader)) continue;
    const auto* hp = static_cast<const MDHeader*>(hmsg.data());
    if (hp->kind != StreamKind::L2Snap) {
      // Some older sims might label differently or remove; ignore non-L2 snapshots
      continue;
    }
    const std::uint64_t snap_seq = hp->seqno;

    // Validate L2SnapHeader 
    if (shmsg.size() != sizeof(L2SnapHeader)) continue;
    const auto* shp = static_cast<const L2SnapHeader*>(shmsg.data());
    const std::uint32_t snap_depth = shp->depth;

    // Validate payload size
    if (arr.size() % sizeof(LevelSummary) != 0) continue;
    const std::size_t n = arr.size() / sizeof(LevelSummary);
    std::vector<LevelSummary> sim(n);
    std::memcpy(sim.data(), arr.data(), n * sizeof(LevelSummary));

    // Wait until our consumer has applied at least snap_seq so it prevents false mismatches
    while (!stop.load(std::memory_order_relaxed) &&
           m.last_l2_seq_applied.load(std::memory_order_acquire) < snap_seq) {
      std::this_thread::sleep_for(2ms);
    }
    if (stop.load(std::memory_order_relaxed)) break;

    // Take our book snapshot 
    const std::uint32_t use_depth = std::min<std::uint32_t>(depth, snap_depth);
    auto mine = ob.snapshotL2(use_depth);

    // Diff
    std::vector<LevelSummary> examples;
    const auto mm = diff_levels(sim, mine, &examples, 3);

    m.xval_runs.fetch_add(1, std::memory_order_relaxed);
    m.xval_mismatch_levels.store(mm, std::memory_order_relaxed);

    // Print a compact line 
    std::cout << "[xval] runs=" << m.xval_runs.load(std::memory_order_relaxed)
              << " depth=" << use_depth
              << " snap_seq=" << snap_seq
              << " mismatch_levels=" << mm;

    if (mm > 0) {
      std::cout << " examples=";
      for (const auto& ex : examples) {
        std::cout << "{side=" << (ex.side == trading::common::Side::Bid ? "B" : "A")
                  << " px=" << ex.price
                  << " qty=" << ex.agg_qty
                  << " cnt=" << ex.order_count
                  << "} ";
      }
    }
    std::cout << "\n";

    std::this_thread::sleep_for(1200ms);
  }
}

