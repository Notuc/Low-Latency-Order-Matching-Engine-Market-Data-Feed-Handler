#pragma once
#include <atomic>
#include <cstdint>

namespace trading::metrics {

struct BookMetrics {
  // Flow
  std::atomic<std::uint64_t> l2_applied{0};
  std::atomic<std::uint64_t> l1_seen{0};

  // Integrity
  std::atomic<std::uint64_t> invariants_fail{0};
  std::atomic<std::uint64_t> l2_idempotent_hits{0};

  // Cross-validation
  std::atomic<std::uint64_t> xval_runs{0};
  std::atomic<std::uint64_t> xval_mismatch_levels{0};

  // Transport/backpressure (optional; bump later in subscriber)
  std::atomic<std::uint64_t> md_ring_dropped{0};
  std::atomic<std::uint64_t> snapshots{0};
  std::atomic<std::uint64_t> gaps_detected{0};
  
  // The latest L2 seqno the consumer has applied.
  std::atomic<std::uint64_t> last_l2_seq_applied{0};   
};

};
