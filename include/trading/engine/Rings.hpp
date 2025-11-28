#pragma once
#include "trading/containers/SpscRing.hpp"
#include "trading/engine/MDEvent.hpp"
#include "trading/engine/OrderFlow.hpp"

namespace trading::engine {
using trading::containers::SpscRing;

using MDRing    = SpscRing<MDEvent>;      // market-data events (L1/L2)
using OrderRing = SpscRing<OrderCmd>;     // inbound order commands
using ExecRing  = SpscRing<ExecReport>;   // outbound exec reports
} 

