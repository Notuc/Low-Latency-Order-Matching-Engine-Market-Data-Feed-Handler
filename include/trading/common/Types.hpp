#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <limits>
#include <cmath>
#include <type_traits>

namespace trading::common {

// Scalar aliases 
using SeqNo        = std::uint64_t;
using OrderId      = std::uint64_t;
using ClientOrderId= std::uint64_t;
using AccountId    = std::uint64_t;
using UserId       = std::uint64_t;
using InstrumentId = std::uint32_t;
using Qty          = std::uint64_t;
using PriceTicks   = std::int64_t;     

using MonoTime     = std::chrono::nanoseconds;             // monotonic time (priority)
using WallTime     = std::chrono::system_clock::time_point; // wall clock (logging)

//  Sentinels 
inline constexpr OrderId    kInvalidOrderId   = 0;
inline constexpr ClientOrderId kNoClientOrder = 0;
inline constexpr PriceTicks kNoPrice          = std::numeric_limits<PriceTicks>::min();
inline constexpr Qty        kNoQty            = 0;

//  Sides, order types,  
enum class Side : std::uint8_t { Bid = 0, Ask = 1 };

enum class OrderType : std::uint8_t {
  Limit = 0,
  Market,
  Stop,
  StopLimit,
  Pegged,
  Iceberg
};
// TimeInForce (TIF) "How long is it valid"

enum class TimeInForce : std::uint8_t {
  Day = 0,
  GTC,      // Good-Till-Cancelled
  GTD,      // Good-Till-Date (requires expire_time)
  IOC,      // Immediate or cancel
  FOK       // Fill or Kill
};

// Execution status & events 
enum class ExecStatus : std::uint8_t {
  New = 0,
  PartiallyFilled,
  Filled,
  Canceled,
  Replaced,
  Rejected,
  Expired
};

enum class ExecType : std::uint8_t {
  New = 0,
  Trade,
  Canceled,
  Replaced,
  Rejected,
  Expired
};

enum class CancelReason : std::uint8_t {
  None = 0,
  UserRequested,
  TooLateToCancel,
  RiskKillSwitch,
  InstrumentHalted,
  AdminCancel
};

enum class RejectReason : std::uint8_t {
  None = 0,
  RiskCheck,
  PriceBands,
  DuplicateId,
  UnknownInstrument,
  InvalidFlags,
  SessionClosed
};

// Pegging, flags 
enum class PegType : std::uint8_t { None = 0, Mid, Primary, Market };

enum class OrderFlags : std::uint32_t {
  None            = 0,
  PostOnly        = 1u << 0,
  ReduceOnly      = 1u << 1,
  Hidden          = 1u << 2,
  AllowTaker      = 1u << 3,
  Discretionary   = 1u << 4,
  SelfTradePrevent= 1u << 5
};

// Bitmask ops for OrderFlags
[[nodiscard]] inline constexpr OrderFlags operator|(OrderFlags a, OrderFlags b) {
  using U = std::underlying_type_t<OrderFlags>;
  return static_cast<OrderFlags>(static_cast<U>(a) | static_cast<U>(b));
}
[[nodiscard]] inline constexpr OrderFlags operator&(OrderFlags a, OrderFlags b) {
  using U = std::underlying_type_t<OrderFlags>;
  return static_cast<OrderFlags>(static_cast<U>(a) & static_cast<U>(b));
}
inline constexpr OrderFlags& operator|=(OrderFlags& a, OrderFlags b) { a = a | b; return a; }
inline constexpr OrderFlags& operator&=(OrderFlags& a, OrderFlags b) { a = a & b; return a; }
[[nodiscard]] inline constexpr bool any(OrderFlags f) {
  using U = std::underlying_type_t<OrderFlags>;
  return static_cast<U>(f) != 0u;
}

// Instrument metadata 
struct Instrument final {
  InstrumentId id{};
  std::string  symbol;       // e.g., "ETH-USD", "ESZ5"
  std::string  mic;          // venue code if relevant
  std::string  currency;     // "USD", "EUR", ...
  std::int32_t price_scale{4};     // 10^scale multiplier used for encode/decode helpers
  PriceTicks   tick_size{1};       // size of one tick in scaled units (often 1)
  Qty          lot_size{1};        // minimum tradable quantity (in lots)
  PriceTicks   min_price{kNoPrice};
  PriceTicks   max_price{kNoPrice};
  double       band_pct{0.0};      // optional fat-finger band (% from ref)
};

// Helpers to encode/decode prices (UI-facing only; matching uses ticks directly)
namespace detail {
  [[nodiscard]] inline constexpr std::int64_t pow10i(std::int32_t n) {
    std::int64_t r = 1;
    for (std::int32_t i = 0; i < n; ++i) r *= 10;
    return r;
  }
}
[[nodiscard]] inline PriceTicks encode_price(double price, std::int32_t scale) {
  const auto m = detail::pow10i(scale);
  return static_cast<PriceTicks>(std::llround(price * static_cast<double>(m)));
}
[[nodiscard]] inline double decode_price(PriceTicks ticks, std::int32_t scale) {
  const auto m = detail::pow10i(scale);
  return static_cast<double>(ticks) / static_cast<double>(m);
}

// Lightweight level & top-of-book summaries 
struct LevelSummary {
  Side        side{Side::Bid};
  PriceTicks  price{kNoPrice};
  Qty         agg_qty{0};
  std::uint32_t order_count{0};
};

struct TopOfBook {
  PriceTicks best_bid_price{kNoPrice};
  Qty        best_bid_qty{0};
  PriceTicks best_ask_price{kNoPrice};
  Qty        best_ask_qty{0};

  [[nodiscard]] inline bool has_spread() const noexcept {
    return best_bid_price != kNoPrice && best_ask_price != kNoPrice && best_bid_price < best_ask_price;
  }
  [[nodiscard]] inline PriceTicks spread_ticks() const noexcept {
    return has_spread() ? (best_ask_price - best_bid_price) : 0;
  }
  [[nodiscard]] inline PriceTicks mid_ticks() const noexcept {
    return has_spread() ? ((best_ask_price + best_bid_price) / 2) : kNoPrice;
  }
};

// Trade print 
enum class Aggressor : std::uint8_t { Buyer = 0, Seller = 1 };

struct TradePrint {
  std::uint64_t trade_id{0};
  InstrumentId  instrument{};
  PriceTicks    price{kNoPrice};
  Qty           qty{0};
  Aggressor     aggressor{Aggressor::Buyer};
  SeqNo         match_seqno{0};
  MonoTime      exec_time{};  // monotonic
};

// Order (intent + state) -
struct Order final {
  // Identity / attribution 
  OrderId       order_id{kInvalidOrderId};   // engine-assigned
  ClientOrderId client_id{kNoClientOrder};   // user/session-provided
  AccountId     account_id{0};
  UserId        user_id{0};
  InstrumentId  instrument{0};
  std::uint32_t stp_group{0};                // self-trade prevention group
  OrderId       orig_order_id{kInvalidOrderId}; // first id in a replace chain
  OrderId       prev_order_id{kInvalidOrderId}; // immediately prior id (if replaced)

  // Intent (immutable after acceptance) 
  Side          side{Side::Bid};
  OrderType     type{OrderType::Limit};
  TimeInForce   tif{TimeInForce::GTC};
  OrderFlags    flags{OrderFlags::None};

  PriceTicks    price{kNoPrice};             // for Market, may be kNoPrice
  Qty           qty{0};                      // total requested
  Qty           display_qty{0};              // iceberg peak (0 => not iceberg)
  Qty           min_qty{0};                  // minimum acceptable execution size

  // Conditional / pegged fields
  PriceTicks    stop_price{kNoPrice};        // activates Stop/StopLimit
  PegType       peg_type{PegType::None};
  std::int32_t  peg_offset_ticks{0};

  //State (mutates during lifecycle)
  ExecStatus    status{ExecStatus::New};
  Qty           leaves_qty{0};
  Qty           cum_filled{0};
  PriceTicks    avg_fill_px{0};              // VWAP in ticks

  // Last execution snapshot (for reporting)
  Qty           last_fill_qty{0};
  PriceTicks    last_fill_px{0};
  MonoTime      last_exec_time{};

  CancelReason  cancel_reason{CancelReason::None};
  RejectReason  reject_reason{RejectReason::None};

  // Priority & sequencing
  SeqNo         entry_seqno{0};
  SeqNo         last_update_seqno{0};
  MonoTime      entry_time{};                // monotonic (priority)
  MonoTime      last_update_time{};
  WallTime      recv_wall_time{};            // wall clock

  // Optional UI/analytics hint (not authoritative for matching)
  std::uint32_t queue_pos_hint{0};
};

} 
