#include <atomic>
#include <chrono>
#include <thread>
#include <variant>   
#include "trading/engine/Rings.hpp"
#include "trading/engine/OrderFlow.hpp"
#include "trading/engine/EngineBook.hpp"

namespace trading::engine {

template <class... Ts> struct Overload : Ts... { using Ts::operator()...; };
template <class... Ts> Overload(Ts...) -> Overload<Ts...>;

void matching_engine_worker(OrderRing& order_ring,
                            ExecRing&  exec_ring,
                            std::atomic<bool>& stop)
{
  // EngineBook needs an ExecSink that publishes ExecReport to the exec ring
  EngineBook book([&](const ExecReport& er){
    (void)exec_ring.try_push(er);  
  });

  while (!stop.load(std::memory_order_relaxed)) {
    OrderCmd cmd;                           // OrderCmd is a std::variant<CmdNew, CmdCancel, CmdReplace>
    if (order_ring.try_pop(cmd)) {
      std::visit(Overload{
        [&](const CmdNew&     c){ book.on_new(c.order); },
        [&](const CmdCancel&  c){ book.on_cancel(c.order_id); },
        [&](const CmdReplace& c){ book.on_replace(c.order_id, c.new_price, c.new_qty); },
        [&](const auto&){  }
      }, cmd);                              
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
}

} 

