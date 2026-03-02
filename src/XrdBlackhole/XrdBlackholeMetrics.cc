#include "XrdBlackhole/XrdBlackholeMetrics.hh"
#include "XrdBlackhole/XrdBlackholeStats.hh"

#include "XrdHttp/XrdHttpExtHandler.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdVersion.hh"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

XrdVERSIONINFO(XrdHttpGetExtHandler, XrdBlackholeMetrics);

extern XrdBlackholeStatsManager g_statsManager;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Emit a single Prometheus metric block (HELP + TYPE + value line).
static void emitMetric(std::ostringstream &oss,
                       const char *name,
                       const char *type,
                       const char *help,
                       double      value,
                       const char *labels = nullptr)
{
  oss << "# HELP " << name << " " << help << "\n"
      << "# TYPE " << name << " " << type << "\n"
      << name;
  if (labels && *labels) oss << "{" << labels << "}";
  oss << " " << value << "\n";
}

// ---------------------------------------------------------------------------
// XrdBlackholeMetrics
// ---------------------------------------------------------------------------

XrdBlackholeMetrics::XrdBlackholeMetrics(XrdSysError *log) : m_log(log) {
  m_log->Say("blackhole_metrics: Prometheus metrics handler initialised");
}

bool XrdBlackholeMetrics::MatchesPath(const char *verb, const char *path) {
  return !strcmp(verb, "GET") && !strcmp(path, "/metrics");
}

std::string XrdBlackholeMetrics::buildMetrics() const {
  const auto s = g_statsManager.getSnapshot();

  const double avg_write = s.write_transfers > 0
                           ? s.sum_write_MiBs / static_cast<double>(s.write_transfers)
                           : 0.0;
  const double avg_read  = s.read_transfers > 0
                           ? s.sum_read_MiBs  / static_cast<double>(s.read_transfers)
                           : 0.0;

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);

  // Counters — values only ever increase.
  emitMetric(oss,
    "blackhole_transfers_total", "counter",
    "Total number of completed transfers",
    static_cast<double>(s.write_transfers), "op=\"write\"");

  // Reuse HELP/TYPE by emitting the second label line directly (same metric family).
  oss << "blackhole_transfers_total{op=\"read\"} " << s.read_transfers << "\n";

  emitMetric(oss,
    "blackhole_bytes_written_total", "counter",
    "Total bytes accepted for writing (data is discarded)",
    static_cast<double>(s.total_bytes_written));

  emitMetric(oss,
    "blackhole_bytes_read_total", "counter",
    "Total bytes synthesised and returned to readers",
    static_cast<double>(s.total_bytes_read));

  emitMetric(oss,
    "blackhole_errors_total", "counter",
    "Total error returns across all transfers",
    static_cast<double>(s.total_errors));

  // Gauges — instantaneous/average values.
  emitMetric(oss,
    "blackhole_write_throughput_MiBs_avg", "gauge",
    "Average write throughput across all completed write transfers (MiB/s)",
    avg_write);

  emitMetric(oss,
    "blackhole_read_throughput_MiBs_avg", "gauge",
    "Average read throughput across all completed read transfers (MiB/s)",
    avg_read);

  return oss.str();
}

int XrdBlackholeMetrics::ProcessReq(XrdHttpExtReq &req) {
  const std::string body = buildMetrics();
  return req.SendSimpleResp(
    200, "OK",
    "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n",
    body.c_str(),
    static_cast<long long>(body.size()));
}

// ---------------------------------------------------------------------------
// Plugin entry point
// ---------------------------------------------------------------------------

extern "C" XrdHttpExtHandler *
XrdHttpGetExtHandler(XrdSysError  *eDest,
                     const char   * /*confg*/,
                     const char   * /*parms*/,
                     XrdOucEnv    * /*myEnv*/)
{
  return new XrdBlackholeMetrics(eDest);
}
