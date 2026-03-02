#ifndef __XRD_BLACKHOLE_METRICS_HH__
#define __XRD_BLACKHOLE_METRICS_HH__

//------------------------------------------------------------------------------
//! XrdHttpExtHandler that exposes Prometheus-compatible metrics at GET /metrics.
//!
//! Loaded by XRootD via:
//!   http.exthandler bhmetrics /path/to/libXrdBlackhole-<N>.so
//!
//! The handler reads a consistent snapshot from XrdBlackholeStatsManager and
//! renders it as Prometheus text exposition format (version 0.0.4).
//!
//! Exposed metrics:
//!   blackhole_transfers_total{op="write"|"read"}  counter
//!   blackhole_bytes_written_total                 counter
//!   blackhole_bytes_read_total                    counter
//!   blackhole_errors_total                        counter
//!   blackhole_write_throughput_MiBs_avg           gauge
//!   blackhole_read_throughput_MiBs_avg            gauge
//------------------------------------------------------------------------------

#include "XrdHttp/XrdHttpExtHandler.hh"

#include <string>

class XrdSysError;

class XrdBlackholeMetrics : public XrdHttpExtHandler {
public:
  explicit XrdBlackholeMetrics(XrdSysError *log);
  virtual ~XrdBlackholeMetrics() {}

  /// Returns true for GET /metrics only.
  virtual bool MatchesPath(const char *verb, const char *path) override;

  /// Renders current stats as Prometheus text and sends an HTTP 200 response.
  virtual int ProcessReq(XrdHttpExtReq &req) override;

  /// No per-handler configuration needed.
  virtual int Init(const char *cfgfile) override { return 0; }

private:
  XrdSysError *m_log;

  /// Build the full Prometheus text body from the current stats snapshot.
  std::string buildMetrics() const;
};

#endif /* __XRD_BLACKHOLE_METRICS_HH__ */
