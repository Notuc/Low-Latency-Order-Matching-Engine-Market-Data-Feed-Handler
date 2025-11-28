// Market Data Stream (publisher + snapshot server): PUB + REP

#include <random>
#include <map>
#include <thread>
#include <chrono>
#include <iostream>
#include <unordered_map>
#include <zmq.hpp>
#include <cstring>

#include "trading/common/Types.hpp"
#include "trading/feed/Wire.hpp"

using namespace std::chrono_literals;
using namespace trading::common;
using namespace trading::feed;

// Simple in-memory book 

struct PriceLevel {
  Qty qty{0};
  std::uint32_t order_count{0};
};

struct BookSide {
  std::map<PriceTicks, PriceLevel, std::function<bool(PriceTicks, PriceTicks)>> levels;
  explicit BookSide(bool is_bid)
      : levels(is_bid
                   ? std::function<bool(PriceTicks, PriceTicks)>(std::greater<PriceTicks>())
                   : std::function<bool(PriceTicks, PriceTicks)>(std::less<PriceTicks>())) {}
};

struct Book {
  Instrument instrument;
  BookSide   bids{true};
  BookSide   asks{false};
  SeqNo      seqno{0};

  TopOfBook tob() const {
    TopOfBook t{};
    if (!bids.levels.empty()) {
      t.best_bid_price = bids.levels.begin()->first;
      t.best_bid_qty   = bids.levels.begin()->second.qty;
    }
    if (!asks.levels.empty()) {
      t.best_ask_price = asks.levels.begin()->first;
      t.best_ask_qty   = asks.levels.begin()->second.qty;
    }
    return t;
  }
};

static void upsert_level(Book& book, Side s, PriceTicks px, Qty qty, std::uint32_t oc) {
  auto& side = (s == Side::Bid) ? book.bids.levels : book.asks.levels;
  if (qty == 0) {
    auto it = side.find(px);
    if (it != side.end()) side.erase(it);
  } else {
    side[px] = PriceLevel{qty, oc};
  }
  ++book.seqno;
}

static std::vector<LevelSummary> top_levels(const Book& book, std::size_t depth) {
  std::vector<LevelSummary> out;
  // bids
  {
    std::size_t i = 0;
    for (auto it = book.bids.levels.begin(); it != book.bids.levels.end() && i < depth; ++it, ++i)
      out.push_back(LevelSummary{Side::Bid, it->first, it->second.qty, it->second.order_count});
  }
  // asks
  {
    std::size_t i = 0;
    for (auto it = book.asks.levels.begin(); it != book.asks.levels.end() && i < depth; ++it, ++i)
      out.push_back(LevelSummary{Side::Ask, it->first, it->second.qty, it->second.order_count});
  }
  return out;
}

// OU process 

struct OUParams { double kappa{0.2}, theta{0.0}, sigma{0.6}; };

static double ou_step(double x, OUParams p, double dt, std::mt19937_64& rng) {
  std::normal_distribution<> N(0.0, 1.0);
  const double m = x + p.kappa * (p.theta - x) * dt;
  const double s = p.sigma * std::sqrt(dt);
  return m + s * N(rng);
}

static PriceTicks snap_to_tick(const Instrument& ins, double price_cont) {
  const auto ticks = encode_price(price_cont, ins.price_scale);
  auto rem = ticks % ins.tick_size;
  if (rem != 0) {
    if (std::llabs(rem) * 2 >= std::llabs(ins.tick_size)) {
      return ticks + (ticks >= 0 ? (ins.tick_size - rem) : -(ins.tick_size + rem));
    } else {
      return ticks - rem;
    }
  }
  return ticks;
}

//  Wire helpers 

static void send_l1(zmq::socket_t& pub, const std::string& topic, const Book& book) {
  MDHeader h{};
  h.kind          = StreamKind::L1;
  h.instrument    = book.instrument.id;
  h.seqno         = book.seqno;
  h.event_time_ns = std::chrono::duration_cast<MonoTime>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();

  const auto t = book.tob();
  L1Payload p{t.best_bid_price, t.best_bid_qty, t.best_ask_price, t.best_ask_qty};

  auto tmsg = zmq::message_t(topic.data(), topic.size());
  auto hmsg = zmq::message_t(sizeof(MDHeader));
  std::memcpy(hmsg.data(), &h, sizeof(MDHeader));
  auto pmsg = zmq::message_t(sizeof(L1Payload));
  std::memcpy(pmsg.data(), &p, sizeof(L1Payload));

  pub.send(tmsg, zmq::send_flags::sndmore);
  pub.send(hmsg, zmq::send_flags::sndmore);
  pub.send(pmsg, zmq::send_flags::none);
}

static void send_l2_delta(zmq::socket_t& pub, const std::string& topic, const Book& book,
                          Action a, Side s, PriceTicks px, Qty q, std::uint32_t oc) {
  MDHeader h{};
  h.kind          = StreamKind::L2Delta;
  h.instrument    = book.instrument.id;
  h.seqno         = book.seqno;
  h.event_time_ns = std::chrono::duration_cast<MonoTime>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();

  L2DeltaPayload p{a, s, px, q, oc};

  auto tmsg = zmq::message_t(topic.data(), topic.size());
  auto hmsg = zmq::message_t(sizeof(MDHeader));
  std::memcpy(hmsg.data(), &h, sizeof(MDHeader));
  auto pmsg = zmq::message_t(sizeof(L2DeltaPayload));
  std::memcpy(pmsg.data(), &p, sizeof(L2DeltaPayload));

  pub.send(tmsg, zmq::send_flags::sndmore);
  pub.send(hmsg, zmq::send_flags::sndmore);
  pub.send(pmsg, zmq::send_flags::none);
}

// Final guard (should never trigger with clamping)
static void repair_cross_and_emit(zmq::socket_t& pub,
                                  const std::string& topicL2,
                                  Book& book) {
  const auto t = book.tob();
  if (t.best_bid_price != kNoPrice && t.best_ask_price != kNoPrice &&
      t.best_bid_price >= t.best_ask_price) {
    const PriceTicks new_ask = t.best_bid_price + book.instrument.tick_size;
    const Qty q = (t.best_ask_qty > 0 ? t.best_ask_qty : 1);
    const std::uint32_t oc = 1;
    upsert_level(book, Side::Ask, new_ask, q, oc);
    send_l2_delta(pub, topicL2, book, Action::Upsert, Side::Ask, new_ask, q, oc);
    std::cerr << "[mdsim] REPAIR: crossed TOB -> raised ask to "
              << new_ask << " above bid " << t.best_bid_price << "\n";
  }
}

//  Main simulator

int main(int argc, char** argv) {
  std::string symbol      = "ETH-USD";
  std::string pub_bind    = "tcp://*:6001";
  std::string rep_bind    = "tcp://*:6002";
  if (argc > 1) symbol   = argv[1];
  if (argc > 2) pub_bind = argv[2];
  if (argc > 3) rep_bind = argv[3];

  Instrument ins;
  ins.id          = 1;
  ins.symbol      = symbol;
  ins.currency    = "USD";
  ins.price_scale = 2;
  ins.tick_size   = 1;
  ins.lot_size    = 1;

  Book book{ins};

  double mid_cont = 2000.00; // dollars
  auto mid = snap_to_tick(ins, mid_cont);

  for (int i = 1; i <= 10; ++i) {
    upsert_level(book, Side::Bid, mid - i * ins.tick_size,  10 * i,  i);
    upsert_level(book, Side::Ask, mid + i * ins.tick_size,  10 * i,  i);
  }

  zmq::context_t ctx(1);
  zmq::socket_t  pub(ctx, zmq::socket_type::pub);
  zmq::socket_t  rep(ctx, zmq::socket_type::rep);

  pub.set(zmq::sockopt::sndhwm, 10000);
  pub.set(zmq::sockopt::immediate, 1);
  pub.set(zmq::sockopt::linger, 0);
  pub.bind(pub_bind);

  rep.set(zmq::sockopt::rcvhwm, 100);
  rep.set(zmq::sockopt::linger, 0);
  rep.bind(rep_bind);

  std::cout << "[mdsim] PUB " << pub_bind << "  REP " << rep_bind
            << "  symbol=" << symbol << "\n";

  std::mt19937_64 rng(42);
  OUParams ou{0.2, 0.0, 0.6};
  double x = 0.0;

  const std::string topicL1 = topic_l1(symbol);
  const std::string topicL2 = topic_l2(symbol);

  zmq::pollitem_t items[] = {
    { static_cast<void*>(rep), 0, ZMQ_POLLIN, 0 }
  };

  auto last_l1   = TopOfBook{};
  auto last_snap = std::chrono::steady_clock::now();

  while (true) {
    zmq::poll(items, 1, std::chrono::milliseconds(0));
    if (items[0].revents & ZMQ_POLLIN) {
      zmq::message_t req;
      rep.recv(req, zmq::recv_flags::none);
      std::string s(static_cast<char*>(req.data()), req.size());
      auto r = decode_snap_req(s);

      const auto depth = std::max<std::uint32_t>(1, r.depth);
      const auto lvls  = top_levels(book, depth);

      MDHeader h{};
      h.kind          = (r.stream == "L1" ? StreamKind::L1Snap : StreamKind::L2Snap);
      h.instrument    = book.instrument.id;
      h.seqno         = book.seqno;
      h.event_time_ns = std::chrono::duration_cast<MonoTime>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();

      zmq::message_t hmsg(sizeof(MDHeader));
      std::memcpy(hmsg.data(), &h, sizeof(MDHeader));
      rep.send(hmsg, zmq::send_flags::sndmore);

      if (h.kind == StreamKind::L1Snap) {
        const auto t = book.tob();
        L1Payload p{t.best_bid_price, t.best_bid_qty, t.best_ask_price, t.best_ask_qty};
        zmq::message_t pmsg(sizeof(L1Payload));
        std::memcpy(pmsg.data(), &p, sizeof(L1Payload));
        rep.send(pmsg, zmq::send_flags::none);
      } else {
        L2SnapHeader sh{depth};
        zmq::message_t shmsg(sizeof(L2SnapHeader));
        std::memcpy(shmsg.data(), &sh, sizeof(L2SnapHeader));
        rep.send(shmsg, zmq::send_flags::sndmore);

        const std::size_t bytes = lvls.size() * sizeof(LevelSummary);
        zmq::message_t arr(bytes);
        std::memcpy(arr.data(), lvls.data(), bytes);
        rep.send(arr, zmq::send_flags::none);
      }
    }

    std::this_thread::sleep_for(5ms);
    x = ou_step(x, ou, 0.005, rng);
    mid = snap_to_tick(ins, mid_cont + x);

    // Randomly mutate 1–3 levels per side near the mid
    std::uniform_int_distribution<int> dN(1, 3);
    std::uniform_int_distribution<int> dSide(0, 1);
    std::uniform_int_distribution<int> dQty(1, 25);
    std::uniform_int_distribution<int> dDepth(0, 4);

    const int edits = dN(rng);
    for (int i = 0; i < edits; ++i) {
      const bool is_bid = dSide(rng) == 0;
      const int  k      = 1 + dDepth(rng);
      PriceTicks px     = mid + (is_bid ? -k * ins.tick_size : +k * ins.tick_size);

      // Clamp so we never emit a crossing delta.
      const auto tcur = book.tob();
      if (is_bid && tcur.best_ask_price != kNoPrice) {
        const PriceTicks max_bid = tcur.best_ask_price - ins.tick_size;
        if (px >= max_bid) px = max_bid;
      }
      if (!is_bid && tcur.best_bid_price != kNoPrice) {
        const PriceTicks min_ask = tcur.best_bid_price + ins.tick_size;
        if (px <= min_ask) px = min_ask;
      }

      if (dN(rng) == 1) {
        // erase a random price at that level (ok; doesn't create crossing)
        upsert_level(book, is_bid ? Side::Bid : Side::Ask, px, 0, 0);
        send_l2_delta(pub, topicL2, book, Action::Erase, is_bid ? Side::Bid : Side::Ask, px, 0, 0);
      } else {
        const Qty q = dQty(rng);
        const std::uint32_t oc = 1 + (q % 5);
        upsert_level(book, is_bid ? Side::Bid : Side::Ask, px, q, oc);
        send_l2_delta(pub, topicL2, book, Action::Upsert, is_bid ? Side::Bid : Side::Ask, px, q, oc);
      }
    }

    // Final safeguard (should be a no-op with the clamp)
    repair_cross_and_emit(pub, topicL2, book);

    // Emit L1 if changed (cheap conflation)
    const auto tob = book.tob();
    if (tob.best_bid_price != last_l1.best_bid_price ||
        tob.best_ask_price != last_l1.best_ask_price ||
        tob.best_bid_qty   != last_l1.best_bid_qty   ||
        tob.best_ask_qty   != last_l1.best_ask_qty) {
      send_l1(pub, topicL1, book);
      last_l1 = tob;
    }

    (void)last_snap;
  }
}

