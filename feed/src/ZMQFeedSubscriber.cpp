#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <utility>
#include <zmq.hpp>
#include <atomic>

#include "trading/common/Types.hpp"
#include "trading/feed/Wire.hpp"
#include "trading/containers/SpscRing.hpp"
#include "trading/engine/MDEvent.hpp"

using namespace trading::common;
using namespace trading::feed;
using trading::containers::SpscRing;
using trading::engine::MDEvent;

struct LocalBook {
  TopOfBook tob{};
  std::map<std::pair<Side, PriceTicks>, std::pair<Qty, std::uint32_t>> levels;
  SeqNo last_l2{0};
  SeqNo last_l1{0};
};

static void apply_l1(LocalBook& b, const L1Payload& p, SeqNo seq) {
  if (seq <= b.last_l1) return;
  b.tob.best_bid_price = p.best_bid_px;
  b.tob.best_bid_qty   = p.best_bid_qty;
  b.tob.best_ask_price = p.best_ask_px;
  b.tob.best_ask_qty   = p.best_ask_qty;
  b.last_l1 = seq;
}
static void apply_l2_delta(LocalBook& b, const L2DeltaPayload& d, SeqNo seq) {
  auto key = std::make_pair(d.side, d.price);
  if (d.action == Action::Erase) b.levels.erase(key);
  else                           b.levels[key] = {d.agg_qty, d.order_count};
  b.last_l2 = seq;
}

// Non-blocking drain of pending messages (modern cppzmq typed get())
static void drain_sub(zmq::socket_t& sub) {
  zmq::message_t part;
  while (sub.recv(part, zmq::recv_flags::dontwait)) {
    while (sub.get(zmq::sockopt::rcvmore)) sub.recv(part, zmq::recv_flags::dontwait);
  }
}

// Snapshot both L2 and L1
static bool get_snapshot(zmq::socket_t& req, const std::string& symbol, LocalBook& out, std::uint32_t depth) {
  { // L2
    const auto txt = encode_snap_req(SnapRequest{"L2", symbol, depth});
    zmq::message_t msg(txt.data(), txt.size());
    if (!req.send(msg, zmq::send_flags::none)) return false;

    zmq::message_t hmsg, shmsg, arr;
    if (!req.recv(hmsg) || !req.recv(shmsg) || !req.recv(arr)) return false;

    auto h = from_bytes<MDHeader>(hmsg.data(), hmsg.size());
    if (h.kind != StreamKind::L2Snap) return false;

    const std::size_t n = arr.size() / sizeof(LevelSummary);
    std::vector<LevelSummary> v(n);
    std::memcpy(v.data(), arr.data(), n * sizeof(LevelSummary));

    out.levels.clear();
    for (const auto& L : v) if (L.agg_qty) out.levels[{L.side, L.price}] = {L.agg_qty, L.order_count};
    out.last_l2 = h.seqno;
  }
  { // L1
    const auto txt = encode_snap_req(SnapRequest{"L1", symbol, depth});
    zmq::message_t msg(txt.data(), txt.size());
    if (!req.send(msg, zmq::send_flags::none)) return false;

    zmq::message_t h, p;
    if (!req.recv(h) || !req.recv(p)) return false;
    auto H = from_bytes<MDHeader>(h.data(), h.size());
    auto P = from_bytes<L1Payload>(p.data(), p.size());
    apply_l1(out, P, H.seqno);
  }
  return true;
}

class ZmqFeedSubscriber {
 public:
 ZmqFeedSubscriber(std::string symbol,
                    std::string pub_ep,
                    std::string req_ep,
                    std::uint32_t depth,
                    SpscRing<MDEvent>* ring = nullptr,
                    std::atomic<bool>* stop = nullptr)  
  : symbol_(std::move(symbol)),
    ctx_(1), sub_(ctx_, zmq::socket_type::sub), req_(ctx_, zmq::socket_type::req),
    depth_(depth), ring_(ring), stop_(stop) {
    sub_.set(zmq::sockopt::rcvhwm, 10000);
    sub_.set(zmq::sockopt::linger, 0);
    // make receives interruptible
    sub_.set(zmq::sockopt::rcvtimeo, 50);     // 50 ms
    req_.set(zmq::sockopt::rcvtimeo, 200);    // snapshot won't hang forever
    req_.set(zmq::sockopt::linger, 0);

    sub_.connect(pub_ep);
    req_.connect(req_ep);

    sub_.set(zmq::sockopt::subscribe, topic_l1(symbol_));
    sub_.set(zmq::sockopt::subscribe, topic_l2(symbol_));

    drain_sub(sub_);
    if (!get_snapshot(req_, symbol_, book_, depth_))
      throw std::runtime_error("initial snapshot failed");

    std::cout << "[md_sub] snapshot ok. last_l2=" << book_.last_l2
              << " last_l1=" << book_.last_l1 << "\n";  }

  void run() {
    while (!stop_ || !stop_->load(std::memory_order_relaxed)) {
      zmq::message_t t, h, p;
      if (!sub_.recv(t)) {
      // timed out or interrupted; check stop again and continue
      continue;
    }
    if (!sub_.recv(h)) continue;
    if (!sub_.recv(p)) continue;

      auto head = from_bytes<MDHeader>(h.data(), h.size());

      if (head.kind == StreamKind::L2Delta) {
        if (head.seqno <= book_.last_l2) continue;                 // stale/dup
        if (head.seqno != book_.last_l2 + 1) {                     // true gap
          std::cerr << "[md_sub] L2 gap: have=" << book_.last_l2
                    << " got=" << head.seqno << " -> resnapshot\n";
          drain_sub(sub_);
          get_snapshot(req_, symbol_, book_, depth_);
          continue;
        }
        auto pay = from_bytes<L2DeltaPayload>(p.data(), p.size());
        apply_l2_delta(book_, pay, head.seqno);
        if (ring_) { MDEvent ev{head, pay}; ring_->try_push(std::move(ev)); }

      } else if (head.kind == StreamKind::L1) {
        auto pay = from_bytes<L1Payload>(p.data(), p.size());
        apply_l1(book_, pay, head.seqno);
        if (ring_) { MDEvent ev{head, pay}; ring_->try_push(std::move(ev)); }
      }

      if ((book_.last_l2 % 200) == 0) {
        std::cout << "[md_sub] l2_seq=" << book_.last_l2
                  << " bid=" << book_.tob.best_bid_price
                  << " ask=" << book_.tob.best_ask_price << "\n";
      }
    }
  }

 private:
  std::string   symbol_;
  zmq::context_t ctx_;
  zmq::socket_t  sub_;
  zmq::socket_t  req_;
  std::uint32_t  depth_;
  LocalBook      book_;
  SpscRing<MDEvent>* ring_{nullptr};
  std::atomic<bool>* stop_{nullptr};
};



// old demo entrypoint (standalone md_sub)
int run_md_subscriber(const std::string& symbol,
                      const std::string& pub_ep,
                      const std::string& req_ep,
                      std::uint32_t depth) {
  try {
    // runs forever (stop == nullptr)
    ZmqFeedSubscriber sub(symbol, pub_ep, req_ep, depth, nullptr, nullptr);
    sub.run();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "md_sub error: " << e.what() << "\n";
    return 1;
  }
}

// ring + stoppable variant (used by me_pipeline)
int run_md_subscriber_ring_until(const std::string& symbol,
                                 const std::string& pub_ep,
                                 const std::string& req_ep,
                                 std::uint32_t depth,
                                 SpscRing<MDEvent>& ring,
                                 std::atomic<bool>& stop) {
  try {
    ZmqFeedSubscriber sub(symbol, pub_ep, req_ep, depth, &ring, &stop);
    sub.run();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "md_sub error: " << e.what() << "\n";
    return 1;
  }
}

