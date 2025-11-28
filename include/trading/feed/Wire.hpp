#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <cstring>
#include "trading/common/Types.hpp"

namespace trading::feed {

using namespace trading::common;

enum class StreamKind : std::uint8_t {
  L3Delta = 1,
  L2Delta = 2,
  L1      = 3,
  L2Snap  = 10,
  L1Snap  = 11
};

enum class Action : std::uint8_t { Upsert = 0, Erase = 1 };

#pragma pack(push, 1)
struct MDHeader {
  std::uint8_t  version{1};
  StreamKind    kind{StreamKind::L2Delta};
  InstrumentId  instrument{0};
  SeqNo         seqno{0};
  std::int64_t  event_time_ns{0};   // MonoTime::count()
  std::uint8_t  flags{0};           // future use
};

// L1 payload (always the whole top-of-book)
struct L1Payload {
  PriceTicks best_bid_px{kNoPrice};
  Qty        best_bid_qty{0};
  PriceTicks best_ask_px{kNoPrice};
  Qty        best_ask_qty{0};
};

// L2 delta: one price level changed or removed
struct L2DeltaPayload {
  Action     action{Action::Upsert};
  Side       side{Side::Bid};
  PriceTicks price{kNoPrice};
  Qty        agg_qty{0};
  std::uint32_t order_count{0};
};

// L2 snapshot header followed by N LevelSummary rows (binary array)
struct L2SnapHeader {
  std::uint32_t depth{0};
};
#pragma pack(pop)

// Helpers 

inline std::int64_t to_ns(MonoTime t) { return t.count(); }
inline MonoTime     from_ns(std::int64_t ns) { return MonoTime{ns}; }

// Serialize POD to a byte vector (no endianness conversion; assume LE on both sides)
template <class T>
inline std::vector<std::uint8_t> to_bytes(const T& pod) {
  static_assert(std::is_trivially_copyable_v<T>, "POD only");
  std::vector<std::uint8_t> out(sizeof(T));
  std::memcpy(out.data(), &pod, sizeof(T));
  return out;
}

template <class T>
inline T from_bytes(const void* data, std::size_t len) {
  if (len < sizeof(T)) throw std::runtime_error("bad size");
  T t{};
  std::memcpy(&t, data, sizeof(T));
  return t;
}

// Topic helper: md.<symbol>.(l3|l2|l1)
inline std::string topic_l1(const std::string& sym) { return "md." + sym + ".l1"; }
inline std::string topic_l2(const std::string& sym) { return "md." + sym + ".l2"; }
inline std::string topic_l3(const std::string& sym) { return "md." + sym + ".l3"; }

// Snapshot request/response (very simple text request, binary response)
struct SnapRequest {
  std::string stream;       // "L2" or "L1"
  std::string symbol;       // symbol string
  std::uint32_t depth{10};  // for L2
};

inline std::string encode_snap_req(const SnapRequest& r) {
  return "SNAP " + r.stream + " " + r.symbol + " " + std::to_string(r.depth);
}

inline SnapRequest decode_snap_req(const std::string& s) {
  SnapRequest r{};
  // naive parsing: SNAP <stream> <symbol> <depth>
  auto p1 = s.find(' ');
  auto p2 = s.find(' ', p1+1);
  auto p3 = s.find(' ', p2+1);
  if (p1 == std::string::npos || p2 == std::string::npos) return r;
  r.stream = s.substr(p1+1, p2 - (p1+1));
  if (p3 == std::string::npos) {
    r.symbol = s.substr(p2+1);
    r.depth = 10;
  } else {
    r.symbol = s.substr(p2+1, p3 - (p2+1));
    r.depth  = static_cast<std::uint32_t>(std::stoul(s.substr(p3+1)));
  }
  return r;
}

} 
