#pragma once
#include <atomic>
#include "trading/engine/Rings.hpp"      // defines OrderRing, ExecRing
#include "trading/engine/OrderFlow.hpp"  // defines CmdNew/CmdCancel/CmdReplace/OrderCmd

namespace trading::engine {

// Runs the matching engine loop: consumes OrderRing, produces ExecRing, stops when 'stop' becomes true.
void matching_engine_worker(OrderRing& order_ring,
                            ExecRing&  exec_ring,
                            std::atomic<bool>& stop);

} 
