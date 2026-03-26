#include <gtest/gtest.h>

#include <thread>

#include "XrdBlackhole/XrdBlackholeStats.hh"

// XrdBlackholeStats.cc defines g_statsManager, but each test should use its
// own isolated instance to avoid cross-test state.  Direct instantiation works
// because XrdBlackholeStatsManager has no singleton enforcement.

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static TransferStats makeWriteStats(int64_t bytes, long long duration_us,
                                    uint32_t ops = 1) {
  TransferStats s;
  s.path         = "/test/file.root";
  s.was_write    = true;
  s.bytes_written = bytes;
  s.bytes_read    = 0;
  s.duration_us   = duration_us;
  s.write_ops     = ops;
  s.errors        = 0;
  return s;
}

static TransferStats makeReadStats(int64_t bytes, long long duration_us,
                                   uint32_t ops = 1) {
  TransferStats s;
  s.path         = "/test/file.root";
  s.was_write    = false;
  s.bytes_written = 0;
  s.bytes_read    = bytes;
  s.duration_us   = duration_us;
  s.read_ops      = ops;
  s.errors        = 0;
  return s;
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST(StatsManager, InitialSnapshotIsAllZero) {
  XrdBlackholeStatsManager mgr;
  auto snap = mgr.getSnapshot();
  EXPECT_EQ(0ULL, snap.total_transfers);
  EXPECT_EQ(0ULL, snap.write_transfers);
  EXPECT_EQ(0ULL, snap.read_transfers);
  EXPECT_EQ(0LL,  snap.total_bytes_written);
  EXPECT_EQ(0LL,  snap.total_bytes_read);
  EXPECT_EQ(0ULL, snap.total_errors);
  EXPECT_DOUBLE_EQ(0.0, snap.sum_write_MiBs);
  EXPECT_DOUBLE_EQ(0.0, snap.sum_read_MiBs);
}

// ---------------------------------------------------------------------------
// recordTransfer — counters
// ---------------------------------------------------------------------------

TEST(StatsManager, RecordWriteIncrementsTotalTransfers) {
  XrdBlackholeStatsManager mgr;
  mgr.recordTransfer(makeWriteStats(1024, 1000));
  EXPECT_EQ(1ULL, mgr.getSnapshot().total_transfers);
}

TEST(StatsManager, RecordWriteAccumulatesBytes) {
  XrdBlackholeStatsManager mgr;
  mgr.recordTransfer(makeWriteStats(1'000'000, 1000));
  EXPECT_EQ(1'000'000LL, mgr.getSnapshot().total_bytes_written);
}

TEST(StatsManager, RecordReadAccumulatesBytes) {
  XrdBlackholeStatsManager mgr;
  mgr.recordTransfer(makeReadStats(2'000'000, 1000));
  EXPECT_EQ(2'000'000LL, mgr.getSnapshot().total_bytes_read);
}

TEST(StatsManager, WriteTransferCountedInWriteTransfers) {
  XrdBlackholeStatsManager mgr;
  mgr.recordTransfer(makeWriteStats(1024 * 1024, 1000));
  auto snap = mgr.getSnapshot();
  EXPECT_EQ(1ULL, snap.write_transfers);
  EXPECT_EQ(0ULL, snap.read_transfers);
}

TEST(StatsManager, ReadTransferCountedInReadTransfers) {
  XrdBlackholeStatsManager mgr;
  mgr.recordTransfer(makeReadStats(1024 * 1024, 1000));
  auto snap = mgr.getSnapshot();
  EXPECT_EQ(0ULL, snap.write_transfers);
  EXPECT_EQ(1ULL, snap.read_transfers);
}

TEST(StatsManager, ErrorsAccumulate) {
  XrdBlackholeStatsManager mgr;
  TransferStats s = makeWriteStats(0, 100);
  s.errors = 3;
  mgr.recordTransfer(s);
  EXPECT_EQ(3ULL, mgr.getSnapshot().total_errors);
}

// ---------------------------------------------------------------------------
// recordTransfer — throughput accumulation
// ---------------------------------------------------------------------------

TEST(StatsManager, WriteThroughputAccumulated) {
  XrdBlackholeStatsManager mgr;
  // 1 MiB written in 1 second → 1.0 MiB/s
  int64_t  bytes    = 1LL * 1024 * 1024;
  long long dur_us  = 1'000'000LL;
  mgr.recordTransfer(makeWriteStats(bytes, dur_us));
  auto snap = mgr.getSnapshot();
  EXPECT_NEAR(1.0, snap.sum_write_MiBs, 1e-6);
}

TEST(StatsManager, ReadThroughputAccumulated) {
  XrdBlackholeStatsManager mgr;
  // 2 MiB read in 1 second → 2.0 MiB/s
  int64_t  bytes   = 2LL * 1024 * 1024;
  long long dur_us = 1'000'000LL;
  mgr.recordTransfer(makeReadStats(bytes, dur_us));
  auto snap = mgr.getSnapshot();
  EXPECT_NEAR(2.0, snap.sum_read_MiBs, 1e-6);
}

TEST(StatsManager, ZeroDurationGivesZeroThroughput) {
  XrdBlackholeStatsManager mgr;
  mgr.recordTransfer(makeWriteStats(1024 * 1024, 0));
  EXPECT_DOUBLE_EQ(0.0, mgr.getSnapshot().sum_write_MiBs);
}

TEST(StatsManager, ZeroBytesWrittenNotCountedInWriteTransfers) {
  // A transfer with bytes_written==0 is not a write-throughput sample.
  XrdBlackholeStatsManager mgr;
  TransferStats s = makeWriteStats(0, 1000);
  mgr.recordTransfer(s);
  auto snap = mgr.getSnapshot();
  EXPECT_EQ(1ULL,  snap.total_transfers);   // transfer was recorded
  EXPECT_EQ(0ULL,  snap.write_transfers);   // but not as a throughput sample
  EXPECT_DOUBLE_EQ(0.0, snap.sum_write_MiBs);
}

// ---------------------------------------------------------------------------
// Multiple transfers
// ---------------------------------------------------------------------------

TEST(StatsManager, MultipleTransfersAccumulate) {
  XrdBlackholeStatsManager mgr;
  mgr.recordTransfer(makeWriteStats(1024, 1000));
  mgr.recordTransfer(makeWriteStats(2048, 1000));
  mgr.recordTransfer(makeReadStats (512,  500));
  auto snap = mgr.getSnapshot();
  EXPECT_EQ(3ULL,  snap.total_transfers);
  EXPECT_EQ(2ULL,  snap.write_transfers);
  EXPECT_EQ(1ULL,  snap.read_transfers);
  EXPECT_EQ(3072LL, snap.total_bytes_written);
  EXPECT_EQ(512LL,  snap.total_bytes_read);
}

TEST(StatsManager, LogSummaryDoesNotCrash) {
  // Smoke test: logSummary() must not crash on an empty manager or after
  // transfers.  Output goes to the /dev/null logger defined in test_globals.cc.
  XrdBlackholeStatsManager mgr;
  EXPECT_NO_THROW(mgr.logSummary());
  mgr.recordTransfer(makeWriteStats(1024 * 1024, 1000));
  mgr.recordTransfer(makeReadStats(512 * 1024, 500));
  EXPECT_NO_THROW(mgr.logSummary());
}

TEST(StatsManager, SnapshotIsConsistentUnderLoad) {
  // Two threads record transfers concurrently; snapshot fields must not tear.
  XrdBlackholeStatsManager mgr;

  auto worker = [&mgr]() {
    for (int i = 0; i < 500; ++i) {
      mgr.recordTransfer(makeWriteStats(1024 * 1024, 1000));
    }
  };

  std::thread t1(worker), t2(worker);
  t1.join();
  t2.join();

  auto snap = mgr.getSnapshot();
  EXPECT_EQ(1000ULL, snap.total_transfers);
  EXPECT_EQ(1000LL * 1024 * 1024, snap.total_bytes_written);
}
