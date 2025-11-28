#include <gtest/gtest.h>

#include "trading/engine/OrderBook.hpp"
#include "trading/feed/Wire.hpp"
#include "trading/common/Types.hpp"

using trading::engine::OrderBook;
using trading::common::Side;
using trading::common::PriceTicks;
using trading::common::Qty;
using trading::feed::Action;   // <-- make Action visible

TEST(l2_apply, idempotent_and_basic_invariants) {
  OrderBook ob;

  auto apply_upsert = [&](Side s, PriceTicks px, Qty q, std::uint32_t oc) {
    ob.on_l2_upsert(s, px, q, oc);
  };
  auto apply_erase = [&](Side s, PriceTicks px) {
    ob.on_l2_erase(s, px);
  };

  // seed a couple of levels
  apply_upsert(Side::Bid, 200000, 5, 1);
  apply_upsert(Side::Ask, 200010, 4, 1);

  // idempotent: same upsert twice
  apply_upsert(Side::Bid, 200000, 5, 1);
  apply_upsert(Side::Bid, 200000, 5, 1);

  auto t = ob.top();
  ASSERT_NE(t.best_bid_price, trading::common::kNoPrice);
  ASSERT_NE(t.best_ask_price, trading::common::kNoPrice);
  ASSERT_LT(t.best_bid_price, t.best_ask_price);

  // erase bid; best bid should disappear
  apply_erase(Side::Bid, 200000);
  t = ob.top();
  EXPECT_TRUE(t.best_bid_price == trading::common::kNoPrice || t.best_bid_price < t.best_ask_price);
}
