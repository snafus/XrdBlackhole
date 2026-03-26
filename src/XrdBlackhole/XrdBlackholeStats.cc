#include "XrdBlackhole/XrdBlackholeStats.hh"
#include "XrdSys/XrdSysError.hh"

#include <sstream>
#include <iomanip>

extern XrdSysError XrdBlackholeEroute;

XrdBlackholeStatsManager g_statsManager;

static double throughputMiBs(int64_t bytes, long long duration_us) {
  if (duration_us <= 0 || bytes <= 0) return 0.0;
  return static_cast<double>(bytes) / static_cast<double>(duration_us)
         * 1e6 / (1024.0 * 1024.0);
}

void XrdBlackholeStatsManager::recordTransfer(const TransferStats& s) {
  double write_MiBs = throughputMiBs(s.bytes_written, s.duration_us);
  double read_MiBs  = throughputMiBs(s.bytes_read,    s.duration_us);

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2)
      << "[XFER]"
      << " path="        << s.path
      << " op="          << (s.was_write ? "write" : "read")
      << " written="     << s.bytes_written
      << " read="        << s.bytes_read
      << " duration_us=" << s.duration_us
      << " write_MiBs="  << write_MiBs
      << " read_MiBs="   << read_MiBs
      << " write_ops="   << s.write_ops
      << " aio_ops="     << s.write_aio_ops
      << " read_ops="    << s.read_ops
      << " errors="      << s.errors;
  XrdBlackholeEroute.Say(oss.str().c_str());

  std::unique_lock<std::mutex> lock(m_mutex);
  m_total_transfers++;
  m_total_bytes_written += s.bytes_written;
  m_total_bytes_read    += s.bytes_read;
  m_total_errors        += s.errors;
  if (s.bytes_written > 0) { m_sum_write_MiBs += write_MiBs; m_write_transfers++; }
  if (s.bytes_read    > 0) { m_sum_read_MiBs  += read_MiBs;  m_read_transfers++;  }
}

XrdBlackholeStatsManager::Snapshot XrdBlackholeStatsManager::getSnapshot() const {
  std::unique_lock<std::mutex> lock(m_mutex);
  return {m_total_transfers, m_write_transfers, m_read_transfers,
          m_total_bytes_written, m_total_bytes_read, m_total_errors,
          m_sum_write_MiBs, m_sum_read_MiBs};
}

void XrdBlackholeStatsManager::logSummary() const {
  std::unique_lock<std::mutex> lock(m_mutex);
  double avg_write = m_write_transfers > 0 ? m_sum_write_MiBs / m_write_transfers : 0.0;
  double avg_read  = m_read_transfers  > 0 ? m_sum_read_MiBs  / m_read_transfers  : 0.0;

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2)
      << "[STATS]"
      << " transfers="       << m_total_transfers
      << " written="         << m_total_bytes_written
      << " read="            << m_total_bytes_read
      << " errors="          << m_total_errors
      << " avg_write_MiBs="  << avg_write
      << " avg_read_MiBs="   << avg_read;
  XrdBlackholeEroute.Say(oss.str().c_str());
}
