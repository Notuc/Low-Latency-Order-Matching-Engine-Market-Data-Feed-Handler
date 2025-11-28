#include <string>
#include <iostream>

int run_md_subscriber(const std::string& symbol,
                      const std::string& pub_ep,
                      const std::string& req_ep,
                      std::uint32_t depth);

int main(int argc, char** argv) {
  std::string symbol  = "ETH-USD";
  std::string pub_ep  = "tcp://localhost:6001";
  std::string req_ep  = "tcp://localhost:6002";
  std::uint32_t depth = 8;

  if (argc > 1) symbol = argv[1];
  if (argc > 2) pub_ep = argv[2];
  if (argc > 3) req_ep = argv[3];
  if (argc > 4) depth  = static_cast<std::uint32_t>(std::stoul(argv[4]));

  std::cout << "[md_subscriber_main] symbol=" << symbol
            << " PUB=" << pub_ep << " REQ=" << req_ep
            << " depth=" << depth << "\n";
  return run_md_subscriber(symbol, pub_ep, req_ep, depth);
}
