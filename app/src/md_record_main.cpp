#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <zmq.hpp>
#include "trading/feed/Wire.hpp"

using namespace trading::feed;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
  const std::string symbol = (argc>1?argv[1]:"ETH-USD");
  const std::string pub_ep = (argc>2?argv[2]:"tcp://localhost:6001");
  const int seconds = (argc>3?std::stoi(argv[3]):5);
  const std::string out = (argc>4?argv[4]:"/tmp/deltas.bin");

  zmq::context_t ctx(1);
  zmq::socket_t sub(ctx, zmq::socket_type::sub);
  sub.set(zmq::sockopt::subscribe, topic_l2(symbol));
  sub.connect(pub_ep);

  std::ofstream ofs(out, std::ios::binary);
  if (!ofs) { std::cerr << "cannot open " << out << "\n"; return 1; }

  auto t0 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(seconds)) {
    zmq::message_t tmsg, hmsg, pmsg;
    if (!sub.recv(tmsg, zmq::recv_flags::none)) continue;
    sub.recv(hmsg, zmq::recv_flags::none);
    sub.recv(pmsg, zmq::recv_flags::none);

    // write raw payload (L2DeltaPayload)
    ofs.write(reinterpret_cast<const char*>(pmsg.data()), pmsg.size());
  }
  std::cout << "[rec] wrote " << out << "\n";
  return 0;
}
