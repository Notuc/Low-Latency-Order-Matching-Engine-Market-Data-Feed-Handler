#include <chrono>
#include <iostream>
#include <vector>
#include "trading/common/Types.hpp"
#include "trading/engine/EngineBook.hpp"

using namespace trading::common;
using namespace trading::engine;

static void null_sink(const ExecReport&) {}

int main() {
  EngineBook book(&null_sink);

  constexpr size_t N = 1'000'000;
  std::vector<Order> orders; orders.reserve(N);
  for (size_t i=0;i<N;++i) {
    Order o{};
    o.order_id = 1'000'000 + i;
    o.side = Side::Bid; o.type = OrderType::Limit;
    o.price = 200000 - (i % 50); o.qty = 1; o.leaves_qty = o.qty;
    orders.push_back(o);
  }

  auto t0 = std::chrono::steady_clock::now();
  for (auto& o : orders) book.on_new(o);
  auto t1 = std::chrono::steady_clock::now();

  const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
  std::cout << "on_new: " << ns << " ns/op (" << N << " orders)\n";
  return 0;
}
