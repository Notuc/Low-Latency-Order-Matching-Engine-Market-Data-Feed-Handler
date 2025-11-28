#include <thread>
#include <iostream>
#include <map>
#include <utility>

#include "trading/containers/SpscRing.hpp"
#include "trading/engine/MDEvent.hpp"
#include "trading/common/Types.hpp"

using trading::containers::SpscRing;
using trading::engine::MDEvent;

// forward decls
struct SimpleBook;
void md_book_consumer(SpscRing<MDEvent>& ring, SimpleBook& book);
int  run_md_subscriber_ring(const std::string& symbol,
                            const std::string& pub_ep,
                            const std::string& req_ep,
                            std::uint32_t depth,
                            SpscRing<MDEvent>& ring);

// duplicate SimpleBook shape 
struct SimpleBook {
  trading::common::TopOfBook tob{};
  std::map<std::pair<trading::common::Side, trading::common::PriceTicks>,
           std::pair<trading::common::Qty, std::uint32_t>> levels;
};

int main() {
  SpscRing<MDEvent> ring(1u << 15); // 32768 events
  SimpleBook book;

  std::thread consumer([&]{ md_book_consumer(ring, book); });
  std::thread subscriber([&]{ run_md_subscriber_ring("ETH-USD",
                                                     "tcp://localhost:6001",
                                                     "tcp://localhost:6002",
                                                     8,
                                                     ring); });

  std::cout << "[md_pipeline] running. Ctrl+C to stop.\n";
  subscriber.join();
  consumer.join();
}
