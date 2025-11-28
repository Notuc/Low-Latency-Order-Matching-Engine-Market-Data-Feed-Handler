#include <atomic>
#include <iostream>
#include <string>
#include "trading/common/Types.hpp"
#include "trading/engine/OrderFlow.hpp"
#include "trading/engine/Rings.hpp"

using namespace trading::common;
using namespace trading::engine;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage:\n"
              << "  oi_cli NEW <side:bid|ask> <px> <qty> <oid>\n"
              << "  oi_cli CXL <oid>\n"
              << "  oi_cli REPL <oid> <new_px> <new_qty>\n";
    return 1;
  }
  std::string cmd = argv[1];

  OrderRing order_ring(1u<<12);
  ExecRing  exec_ring (1u<<12);
  std::atomic<bool> stop{false};


  if (cmd == "NEW") {
    if (argc < 6) return 1;
    const std::string s = argv[2];
    PriceTicks px = static_cast<PriceTicks>(std::stoll(argv[3]));
    Qty        q  = static_cast<Qty>(std::stoull(argv[4]));
    OrderId    id = static_cast<OrderId>(std::stoull(argv[5]));
    Order o{};
    o.order_id = id; o.side = (s=="bid"?Side::Bid:Side::Ask); o.type = OrderType::Limit;
    o.price = px; o.qty = q; o.leaves_qty = q;
    order_ring.try_push(OrderCmd{CmdNew{ .order = o }});
  } else if (cmd == "CXL") {
    if (argc < 3) return 1;
    OrderId id = static_cast<OrderId>(std::stoull(argv[2]));
    order_ring.try_push(OrderCmd{CmdCancel{ .order_id = id }});
  } else if (cmd == "REPL") {
    if (argc < 5) return 1;
    OrderId id = static_cast<OrderId>(std::stoull(argv[2]));
    PriceTicks new_px = static_cast<PriceTicks>(std::stoll(argv[3]));
    Qty        new_q  = static_cast<Qty>(std::stoull(argv[4]));
    order_ring.try_push(OrderCmd{CmdReplace{ .order_id = id, .new_price = new_px, .new_qty = new_q }});
  } else {
    return 1;
  }

  // Drain any execs for a moment
  for (int i=0;i<200;++i) {
    ExecReport er;
    if (exec_ring.try_pop(er)) {
      std::cout << "[exec] oid=" << er.order_id << " type=" << (int)er.exec_type
                << " status=" << (int)er.status << " last_qty=" << er.last_qty
                << " last_px=" << er.last_px << " leaves=" << er.leaves_qty << "\n";
    }
  }
  return 0;
}
