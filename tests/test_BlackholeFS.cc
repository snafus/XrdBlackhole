#include <gtest/gtest.h>

#include "XrdBlackhole/BlackholeFS.hh"

#include <fcntl.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr const char* kPath  = "/test/file.root";
static constexpr const char* kPath2 = "/test/other.root";

// ---------------------------------------------------------------------------
// exists() / getStub()
// ---------------------------------------------------------------------------

TEST(BlackholeFS, NewFSIsEmpty) {
  BlackholeFS fs;
  EXPECT_FALSE(fs.exists(kPath));
  EXPECT_EQ(nullptr, fs.getStub(kPath));
}

TEST(BlackholeFS, GetStubUnknownReturnsNull) {
  BlackholeFS fs;
  EXPECT_EQ(nullptr, fs.getStub(kPath));
}

// ---------------------------------------------------------------------------
// open() — write path
// ---------------------------------------------------------------------------

TEST(BlackholeFS, OpenWriteCreatesFile) {
  BlackholeFS fs;
  int rc = fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  EXPECT_GT(rc, 0) << "open should return a positive fd";
  EXPECT_TRUE(fs.exists(kPath));
}

TEST(BlackholeFS, OpenWriteNewFileHasZeroSize) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  auto stub = fs.getStub(kPath);
  ASSERT_NE(nullptr, stub);
  EXPECT_EQ(0ULL, stub->m_size);
}

TEST(BlackholeFS, OpenWriteStubMarkedOpenForWrite) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  auto stub = fs.getStub(kPath);
  ASSERT_NE(nullptr, stub);
  EXPECT_TRUE(stub->m_isOpenWrite);
}

TEST(BlackholeFS, OpenWriteNonZeroInoAndDev) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  auto stub = fs.getStub(kPath);
  ASSERT_NE(nullptr, stub);
  EXPECT_NE(0UL, (unsigned long)stub->m_stat.st_ino);
  EXPECT_NE(0UL, (unsigned long)stub->m_stat.st_dev);
}

TEST(BlackholeFS, FdIsMonotonicallyIncreasing) {
  BlackholeFS fs;
  int fd1 = fs.open(kPath,  O_WRONLY | O_CREAT, 0644);
  int fd2 = fs.open(kPath2, O_WRONLY | O_CREAT, 0644);
  EXPECT_GT(fd1, 0);
  EXPECT_GT(fd2, fd1);
}

// ---------------------------------------------------------------------------
// open() — read path
// ---------------------------------------------------------------------------

TEST(BlackholeFS, OpenReadMissingFileReturnsEnoent) {
  BlackholeFS fs;
  int rc = fs.open(kPath, O_RDONLY, 0);
  EXPECT_EQ(-ENOENT, rc);
}

TEST(BlackholeFS, OpenReadAfterWriteSucceeds) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  int rc = fs.open(kPath, O_RDONLY, 0);
  EXPECT_GT(rc, 0);
}

// ---------------------------------------------------------------------------
// open() — O_TRUNC / O_EXCL
// ---------------------------------------------------------------------------

TEST(BlackholeFS, OpenTruncReplacesExistingStub) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  auto stub1 = fs.getStub(kPath);

  // Reopen with O_TRUNC: a fresh stub must be created.
  fs.open(kPath, O_WRONLY | O_TRUNC, 0644);
  auto stub2 = fs.getStub(kPath);

  ASSERT_NE(nullptr, stub2);
  EXPECT_NE(stub1.get(), stub2.get()) << "O_TRUNC must produce a new stub";
  EXPECT_EQ(0ULL, stub2->m_size);
}

TEST(BlackholeFS, OpenExclOnExistingFileReturnsEexist) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  int rc = fs.open(kPath, O_WRONLY | O_EXCL, 0644);
  EXPECT_EQ(-EEXIST, rc);
}

TEST(BlackholeFS, OpenExclOnNewFileSucceeds) {
  BlackholeFS fs;
  int rc = fs.open(kPath, O_WRONLY | O_CREAT | O_EXCL, 0644);
  EXPECT_GT(rc, 0);
}

// ---------------------------------------------------------------------------
// close()
// ---------------------------------------------------------------------------

TEST(BlackholeFS, CloseMarksClosed) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  fs.close(kPath);
  auto stub = fs.getStub(kPath);
  ASSERT_NE(nullptr, stub);
  EXPECT_FALSE(stub->m_isOpenWrite);
}

TEST(BlackholeFS, CloseNonExistentPathIsNoop) {
  BlackholeFS fs;
  // Must not crash or throw.
  EXPECT_NO_THROW(fs.close("/no/such/file"));
}

// ---------------------------------------------------------------------------
// unlink()
// ---------------------------------------------------------------------------

TEST(BlackholeFS, UnlinkRemovesFile) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  ASSERT_EQ(0, fs.unlink(kPath));
  EXPECT_FALSE(fs.exists(kPath));
}

TEST(BlackholeFS, UnlinkUnknownReturnsEnoent) {
  BlackholeFS fs;
  EXPECT_EQ(-ENOENT, fs.unlink(kPath));
}

TEST(BlackholeFS, UnlinkLeavesOtherFilesIntact) {
  BlackholeFS fs;
  fs.open(kPath,  O_WRONLY | O_CREAT, 0644);
  fs.open(kPath2, O_WRONLY | O_CREAT, 0644);
  fs.unlink(kPath);
  EXPECT_FALSE(fs.exists(kPath));
  EXPECT_TRUE(fs.exists(kPath2));
}

// Shared_ptr held by caller keeps the stub alive after unlink.
TEST(BlackholeFS, StubSurvivesUnlinkWhileHeld) {
  BlackholeFS fs;
  fs.open(kPath, O_WRONLY | O_CREAT, 0644);
  auto stub = fs.getStub(kPath);  // caller holds a ref
  ASSERT_NE(nullptr, stub);

  fs.unlink(kPath);
  EXPECT_FALSE(fs.exists(kPath));
  // stub pointer is still valid — shared_ptr prevents deallocation.
  EXPECT_EQ(0ULL, stub->m_size);
}

// ---------------------------------------------------------------------------
// create_defaults()
// ---------------------------------------------------------------------------

TEST(BlackholeFS, CreateDefaultsPopulatesFiles) {
  BlackholeFS fs;
  fs.create_defaults("/test");
  EXPECT_TRUE(fs.exists("/test/testfile_zeros_1MiB"));
  EXPECT_TRUE(fs.exists("/test/testfile_zeros_1GiB"));
  EXPECT_TRUE(fs.exists("/test/testfile_zeros_10GiB"));
}

TEST(BlackholeFS, CreateDefaultsCorrectSizes) {
  BlackholeFS fs;
  fs.create_defaults("/data");

  auto s1 = fs.getStub("/data/testfile_zeros_1MiB");
  auto s2 = fs.getStub("/data/testfile_zeros_1GiB");
  auto s3 = fs.getStub("/data/testfile_zeros_10GiB");

  ASSERT_NE(nullptr, s1);
  ASSERT_NE(nullptr, s2);
  ASSERT_NE(nullptr, s3);

  EXPECT_EQ(1ULL * 1024 * 1024,             s1->m_size);
  EXPECT_EQ(1ULL * 1024 * 1024 * 1024,      s2->m_size);
  EXPECT_EQ(10ULL * 1024 * 1024 * 1024,     s3->m_size);
}

TEST(BlackholeFS, CreateDefaultsStatSizeMatchesStubSize) {
  BlackholeFS fs;
  fs.create_defaults("/test");
  auto stub = fs.getStub("/test/testfile_zeros_1GiB");
  ASSERT_NE(nullptr, stub);
  EXPECT_EQ(static_cast<off_t>(stub->m_size), stub->m_stat.st_size);
}

TEST(BlackholeFS, CreateDefaultsMarkedSpecial) {
  BlackholeFS fs;
  fs.create_defaults("/test");
  auto stub = fs.getStub("/test/testfile_zeros_1MiB");
  ASSERT_NE(nullptr, stub);
  EXPECT_TRUE(stub->m_special);
}

TEST(BlackholeFS, CreateDefaultsNonZeroInoAndDev) {
  BlackholeFS fs;
  fs.create_defaults("/test");
  auto stub = fs.getStub("/test/testfile_zeros_1GiB");
  ASSERT_NE(nullptr, stub);
  EXPECT_NE(0UL, (unsigned long)stub->m_stat.st_ino);
  EXPECT_NE(0UL, (unsigned long)stub->m_stat.st_dev);
}

TEST(BlackholeFS, CreateDefaultsDifferentPaths) {
  // Two calls with different path prefixes produce independent file sets.
  BlackholeFS fs;
  fs.create_defaults("/alpha");
  fs.create_defaults("/beta");
  EXPECT_TRUE(fs.exists("/alpha/testfile_zeros_1MiB"));
  EXPECT_TRUE(fs.exists("/beta/testfile_zeros_1MiB"));
  EXPECT_FALSE(fs.exists("/gamma/testfile_zeros_1MiB"));
}
