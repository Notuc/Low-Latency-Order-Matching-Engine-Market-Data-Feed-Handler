#pragma once
#include <optional>
#include "trading/engine/Rings.hpp"
#include "trading/engine/OrderFlow.hpp"

namespace trading::engine {

inline bool gateway_submit(OrderRing& ring, const Order& o) {
  return ring.try_push(OrderCmd{CmdNew{.order = o}});
}
inline bool gateway_cancel(OrderRing& ring, OrderId id) {
  return ring.try_push(OrderCmd{CmdCancel{.order_id = id}});
}
inline bool gateway_replace(OrderRing& ring, const Order& o) {
  return ring.try_push(OrderCmd{CmdReplace{.new_order = o}});
}

} 
