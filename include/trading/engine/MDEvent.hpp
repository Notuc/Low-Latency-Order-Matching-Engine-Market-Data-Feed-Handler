#pragma once
#include <variant>
#include "trading/feed/Wire.hpp"

namespace trading::engine {

using trading::feed::MDHeader;
using trading::feed::L1Payload;
using trading::feed::L2DeltaPayload;
using trading::feed::StreamKind;

struct MDEvent {
  MDHeader header{};
  std::variant<L1Payload, L2DeltaPayload> payload;
};

} 
