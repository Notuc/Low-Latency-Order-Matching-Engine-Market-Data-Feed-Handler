#pragma once
#include <variant>
#include <optional>
#include <cstdint>
#include "trading/common/Types.hpp"

namespace trading::engine {

using trading::common::Order;
using trading::common::OrderId;
using trading::common::ClientOrderId;
using trading::common::TradePrint;
using trading::common::ExecStatus;
using trading::common::ExecType;
using trading::common::Qty;
using trading::common::PriceTicks;

// Commands into MatchingEngine 
struct CmdNew {
  Order order;                    // Complete order intent/state per your Types.hpp
};
struct CmdCancel {
  OrderId order_id{0};
};
struct CmdReplace {
  OrderId    order_id;
  PriceTicks new_price;
  Qty        new_qty;
};

using OrderCmd = std::variant<CmdNew, CmdCancel, CmdReplace>;

// Exec reports out of MatchingEngine 
struct ExecReport {
  OrderId      order_id{0};
  ClientOrderId client_id{0};
  ExecStatus   status{ExecStatus::New};
  ExecType     exec_type{ExecType::New};
  Qty          last_qty{0};
  PriceTicks   last_px{0};        // ticks
  Qty          leaves_qty{0};
  std::optional<TradePrint> trade; // present if a trade occurred
};

} 

