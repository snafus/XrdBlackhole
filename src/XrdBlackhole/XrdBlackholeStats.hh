#ifndef __XRD_BLACKHOLE_STATS_HH__
#define __XRD_BLACKHOLE_STATS_HH__

#include <string>
#include <mutex>
#include <cstdint>
#include <chrono>

//------------------------------------------------------------------------------
//! Per-transfer statistics recorded over a single Open→Close lifecycle.
//------------------------------------------------------------------------------
struct TransferStats {
  std::string path;
  std::chrono::system_clock::time_point open_time;
  long long duration_us{0};     ///< Elapsed wall time from open to close (µs)
  int64_t   bytes_written{0};   ///< Total bytes written (sync + AIO)
  int64_t   bytes_read{0};      ///< Total bytes read
  uint32_t  write_ops{0};       ///< Synchronous write calls
  uint32_t  write_aio_ops{0};   ///< AIO write calls
  uint32_t  read_ops{0};        ///< Buffered read calls
  uint32_t  errors{0};          ///< Error returns during the transfer
  bool      was_write{false};   ///< True if opened for writing
};

//------------------------------------------------------------------------------
//! Thread-safe manager that records completed transfers, logs per-transfer
//! summaries, and accumulates global aggregates across the server lifetime.
//!
//! Per-transfer log line format:
//!   [XFER] path=<p> op=<write|read> written=<N> read=<N>
//!          duration_us=<N> write_MiBs=<F> read_MiBs=<F>
//!          write_ops=<N> aio_ops=<N> read_ops=<N> errors=<N>
//!
//! Global summary format:
//!   [STATS] transfers=<N> written=<N> read=<N> errors=<N>
//!           avg_write_MiBs=<F> avg_read_MiBs=<F>
//------------------------------------------------------------------------------
class XrdBlackholeStatsManager {
public:
  /// Record a completed transfer: log its stats and update global aggregates.
  void recordTransfer(const TransferStats& stats);

  /// Log a one-line summary of global aggregates. Called on server shutdown.
  void logSummary() const;

private:
  mutable std::mutex m_mutex;
  uint64_t m_total_transfers{0};
  int64_t  m_total_bytes_written{0};
  int64_t  m_total_bytes_read{0};
  uint64_t m_total_errors{0};
  double   m_sum_write_MiBs{0.0};
  double   m_sum_read_MiBs{0.0};
  uint64_t m_write_transfers{0};  ///< Transfers that wrote at least one byte
  uint64_t m_read_transfers{0};   ///< Transfers that read at least one byte
};

extern XrdBlackholeStatsManager g_statsManager;

#endif /* __XRD_BLACKHOLE_STATS_HH__ */
