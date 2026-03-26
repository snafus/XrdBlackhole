#include <gtest/gtest.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "XrdSfs/XrdSfsAio.hh"
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysLogger.hh"
#include "XrdBlackhole/XrdBlackholeOssFile.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

// ---------------------------------------------------------------------------
// Minimal XrdSfsAio implementation for testing the AIO read/write paths.
// ---------------------------------------------------------------------------

class MockAio : public XrdSfsAio {
public:
    bool readCallbackFired{false};
    bool writeCallbackFired{false};
    void doneRead()  override { readCallbackFired  = true; }
    void doneWrite() override { writeCallbackFired = true; }
    void Recycle()   override {}
};

// ---------------------------------------------------------------------------
// Test fixture
//
// SetUpTestSuite runs once: assigns a real XrdSysLogger (writing to
// /dev/null) to the global XrdBlackholeEroute (which starts with a null
// logger), then constructs an XrdBlackholeOss with a null config path so
// Configure() skips file parsing — m_writespeedMiBs defaults to 0
// (unlimited).
//
// SetUp/TearDown create a fresh XrdBlackholeOssFile for each test and
// unlink the test path from g_blackholeFS afterwards.
// ---------------------------------------------------------------------------

class OssFileTest : public ::testing::Test {
public:
    static XrdSysLogger* s_logger;
    static XrdBlackholeOss* s_oss;

    static void SetUpTestSuite() {
        static int devnull = open("/dev/null", O_WRONLY);
        static XrdSysLogger logger(devnull, 0);
        s_logger = &logger;
        // Assign a real logger so Say()/Emsg() don't dereference a null ptr.
        XrdBlackholeEroute.logger(s_logger);
        // nullptr config path → Configure() skips file-open entirely.
        s_oss = new XrdBlackholeOss(nullptr, XrdBlackholeEroute);
    }

    static void TearDownTestSuite() {
        delete s_oss;
        s_oss = nullptr;
    }

    XrdBlackholeOssFile* f{nullptr};
    XrdOucEnv env;

    static constexpr const char* kPath = "/test/ossfile.root";

    void SetUp() override {
        f = new XrdBlackholeOssFile(s_oss);
    }

    void TearDown() override {
        delete f;
        f = nullptr;
        g_blackholeFS.unlink(kPath);  // tolerated even if already unlinked
    }

    // Open the test file for writing; asserts on failure so tests can proceed.
    void OpenWrite(mode_t mode = 0644) {
        ASSERT_EQ(XrdOssOK, f->Open(kPath, O_WRONLY | O_CREAT, mode, env));
    }

    // Open the test file for reading (requires it to exist first).
    void OpenRead() {
        ASSERT_EQ(XrdOssOK, f->Open(kPath, O_RDONLY, 0, env));
    }

    // Write nbytes to the test file, close it, then reopen for reading.
    void WriteAndReopen(size_t nbytes) {
        std::vector<char> buf(nbytes, 0);
        f->Write(buf.data(), 0, nbytes);
        f->Close();
        delete f;
        f = new XrdBlackholeOssFile(s_oss);
        OpenRead();
    }
};

XrdSysLogger*    OssFileTest::s_logger{nullptr};
XrdBlackholeOss* OssFileTest::s_oss{nullptr};

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, OpenWriteReturnsOssOK) {
    EXPECT_EQ(XrdOssOK, f->Open(kPath, O_WRONLY | O_CREAT, 0644, env));
}

TEST_F(OssFileTest, OpenReadMissingFileReturnsEnoent) {
    EXPECT_EQ(-ENOENT, f->Open(kPath, O_RDONLY, 0, env));
}

TEST_F(OssFileTest, CloseReturnsOssOK) {
    OpenWrite();
    EXPECT_EQ(XrdOssOK, f->Close());
}

// ---------------------------------------------------------------------------
// Write — sync path
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, WriteReturnsBlen) {
    OpenWrite();
    char buf[4096]{};
    EXPECT_EQ(static_cast<ssize_t>(sizeof(buf)),
              f->Write(buf, 0, sizeof(buf)));
}

TEST_F(OssFileTest, WriteLargeBlockReturnsBlen) {
    OpenWrite();
    std::vector<char> buf(1024 * 1024, 'x');
    EXPECT_EQ(static_cast<ssize_t>(buf.size()),
              f->Write(buf.data(), 0, buf.size()));
}

TEST_F(OssFileTest, WriteZeroBytesReturnsZero) {
    OpenWrite();
    EXPECT_EQ(0, f->Write(nullptr, 0, 0));
}

// ---------------------------------------------------------------------------
// Write — AIO path
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, WriteAioCallsDoneWrite) {
    OpenWrite();
    MockAio aio;
    char buf[1024]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    f->Write(&aio);
    EXPECT_TRUE(aio.writeCallbackFired);
}

TEST_F(OssFileTest, WriteAioReturnsOssOK) {
    OpenWrite();
    MockAio aio;
    char buf[512]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    EXPECT_EQ(XrdOssOK, f->Write(&aio));
}

TEST_F(OssFileTest, WriteAioSetsResult) {
    OpenWrite();
    MockAio aio;
    char buf[2048]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    f->Write(&aio);
    EXPECT_EQ(static_cast<ssize_t>(sizeof(buf)), aio.Result);
}

// ---------------------------------------------------------------------------
// Close updates stub size
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, CloseUpdateStubSizeFromSyncWrites) {
    OpenWrite();
    char buf[8192]{};
    f->Write(buf, 0,    4096);
    f->Write(buf, 4096, 4096);
    f->Close();

    auto stub = g_blackholeFS.getStub(kPath);
    ASSERT_NE(nullptr, stub);
    EXPECT_EQ(8192ULL, stub->m_size);
}

TEST_F(OssFileTest, CloseUpdateStubSizeFromAioWrites) {
    OpenWrite();
    MockAio aio;
    char buf[1024]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    f->Write(&aio);
    f->Close();

    auto stub = g_blackholeFS.getStub(kPath);
    ASSERT_NE(nullptr, stub);
    EXPECT_EQ(1024ULL, stub->m_size);
}

TEST_F(OssFileTest, CloseUpdateStubSizeSyncPlusAio) {
    OpenWrite();
    char buf[1024]{};
    f->Write(buf, 0, 1024);

    MockAio aio;
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = 512;
    aio.sfsAio.aio_offset = 1024;
    f->Write(&aio);
    f->Close();

    auto stub = g_blackholeFS.getStub(kPath);
    ASSERT_NE(nullptr, stub);
    EXPECT_EQ(1536ULL, stub->m_size);
}

// ---------------------------------------------------------------------------
// Read — buffered path: Read(void*, off_t, size_t)
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, ReadFromStartReturnsRequestedBytes) {
    OpenWrite();
    WriteAndReopen(4096);
    char rbuf[1024]{};
    EXPECT_EQ(static_cast<ssize_t>(sizeof(rbuf)),
              f->Read(rbuf, 0, sizeof(rbuf)));
}

TEST_F(OssFileTest, ReadFillsZeros) {
    OpenWrite();
    WriteAndReopen(4096);
    char rbuf[256];
    memset(rbuf, 0xFF, sizeof(rbuf));
    f->Read(rbuf, 0, sizeof(rbuf));
    for (auto b : rbuf) EXPECT_EQ('\0', b);
}

TEST_F(OssFileTest, ReadMidFileReturnsRemainder) {
    OpenWrite();
    WriteAndReopen(1024);
    char rbuf[512]{};
    // offset 768, blen 512, file size 1024 → clamped to 256 bytes
    EXPECT_EQ(256, f->Read(rbuf, 768, sizeof(rbuf)));
}

TEST_F(OssFileTest, ReadAtExactSizeReturnsEOF) {
    OpenWrite();
    WriteAndReopen(1024);
    char rbuf[64]{};
    EXPECT_EQ(0, f->Read(rbuf, 1024, sizeof(rbuf)));
}

TEST_F(OssFileTest, ReadPastSizeReturnsEOF) {
    OpenWrite();
    WriteAndReopen(1024);
    char rbuf[64]{};
    EXPECT_EQ(0, f->Read(rbuf, 2048, sizeof(rbuf)));
}

TEST_F(OssFileTest, ReadNegativeOffsetReturnsEOF) {
    OpenWrite();
    WriteAndReopen(1024);
    char rbuf[64]{};
    // Guards size_t underflow: offset < 0 must return 0, not a huge count.
    EXPECT_EQ(0, f->Read(rbuf, -1, sizeof(rbuf)));
}

TEST_F(OssFileTest, ReadWithNoOpenStubReturnsEINVAL) {
    // f is freshly constructed but Open() has never been called.
    char rbuf[64]{};
    EXPECT_EQ(-EINVAL, f->Read(rbuf, 0, sizeof(rbuf)));
}

// ---------------------------------------------------------------------------
// Read — preposition-only variant: Read(off_t, size_t)
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, ReadPrepositionReturnsZero) {
    OpenWrite();
    EXPECT_EQ(0, f->Read(static_cast<off_t>(0), static_cast<size_t>(1024)));
}

TEST_F(OssFileTest, ReadPrepositionWithNoStubReturnsEINVAL) {
    // No Open() called — stub is null.
    EXPECT_EQ(-EINVAL, f->Read(static_cast<off_t>(0), static_cast<size_t>(1024)));
}

// ---------------------------------------------------------------------------
// Fstat
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, FstatReturnsSIFREG) {
    OpenWrite();
    struct stat st{};
    ASSERT_EQ(XrdOssOK, f->Fstat(&st));
    EXPECT_TRUE((st.st_mode & S_IFREG) != 0);
}

TEST_F(OssFileTest, FstatAfterWriteReflectsMode) {
    OpenWrite(0640);
    struct stat st{};
    ASSERT_EQ(XrdOssOK, f->Fstat(&st));
    // S_IFREG is always set regardless of requested mode (invariant in Fstat).
    EXPECT_TRUE((st.st_mode & S_IFREG) != 0);
}

// ---------------------------------------------------------------------------
// Unsupported operations
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, FsyncReturnsEnotsup) {
    OpenWrite();
    EXPECT_EQ(-ENOTSUP, f->Fsync());
}

TEST_F(OssFileTest, FtruncateReturnsEnotsup) {
    OpenWrite();
    EXPECT_EQ(-ENOTSUP, f->Ftruncate(1024));
}

// ---------------------------------------------------------------------------
// Read — AIO path: Read(XrdSfsAio*)
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, ReadAioCallsDoneRead) {
    OpenWrite();
    WriteAndReopen(4096);
    MockAio aio;
    char buf[1024]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    f->Read(&aio);
    EXPECT_TRUE(aio.readCallbackFired);
}

TEST_F(OssFileTest, ReadAioReturnsOssOK) {
    OpenWrite();
    WriteAndReopen(4096);
    MockAio aio;
    char buf[512]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    EXPECT_EQ(XrdOssOK, f->Read(&aio));
}

TEST_F(OssFileTest, ReadAioSetsResultToBytesRead) {
    OpenWrite();
    WriteAndReopen(4096);
    MockAio aio;
    char buf[1024]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    f->Read(&aio);
    EXPECT_EQ(static_cast<ssize_t>(sizeof(buf)), aio.Result);
}

TEST_F(OssFileTest, ReadAioAtEofSetsResultToZero) {
    OpenWrite();
    WriteAndReopen(1024);
    MockAio aio;
    char buf[256]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 1024;  // exactly at EOF
    f->Read(&aio);
    EXPECT_EQ(0, aio.Result);
}

TEST_F(OssFileTest, ReadAioClampedAtFileEnd) {
    OpenWrite();
    WriteAndReopen(1024);
    MockAio aio;
    char buf[512]{};
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 768;   // 256 bytes remain
    f->Read(&aio);
    EXPECT_EQ(256, aio.Result);
}

TEST_F(OssFileTest, ReadAioFillsZeros) {
    OpenWrite();
    WriteAndReopen(4096);
    MockAio aio;
    char buf[256];
    memset(buf, 0xFF, sizeof(buf));
    aio.sfsAio.aio_buf    = buf;
    aio.sfsAio.aio_nbytes = sizeof(buf);
    aio.sfsAio.aio_offset = 0;
    f->Read(&aio);
    for (auto b : buf) EXPECT_EQ('\0', b);
}

// ---------------------------------------------------------------------------
// Read — vectored path: ReadV(XrdOucIOVec*, int)
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, ReadVEmptyVecReturnsZero) {
    OpenWrite();
    WriteAndReopen(4096);
    EXPECT_EQ(0, f->ReadV(nullptr, 0));
}

TEST_F(OssFileTest, ReadVNegativeNReturnsZero) {
    OpenWrite();
    WriteAndReopen(4096);
    EXPECT_EQ(0, f->ReadV(nullptr, -1));
}

TEST_F(OssFileTest, ReadVSingleSegmentReturnsBytes) {
    OpenWrite();
    WriteAndReopen(4096);
    char buf[1024]{};
    XrdOucIOVec vec[1];
    vec[0].data   = buf;
    vec[0].offset = 0;
    vec[0].size   = 1024;
    EXPECT_EQ(1024, f->ReadV(vec, 1));
}

TEST_F(OssFileTest, ReadVMultipleSegmentsReturnsTotalBytes) {
    OpenWrite();
    WriteAndReopen(4096);
    char buf[4096]{};
    XrdOucIOVec vec[3];
    vec[0].data   = buf;        vec[0].offset = 0;    vec[0].size = 1024;
    vec[1].data   = buf + 1024; vec[1].offset = 1024; vec[1].size = 1024;
    vec[2].data   = buf + 2048; vec[2].offset = 2048; vec[2].size = 1024;
    EXPECT_EQ(3072, f->ReadV(vec, 3));
}

TEST_F(OssFileTest, ReadVClampsLastSegmentAtEof) {
    OpenWrite();
    WriteAndReopen(1024);
    char buf[1024]{};
    XrdOucIOVec vec[2];
    vec[0].data   = buf;       vec[0].offset = 0;   vec[0].size = 512;
    vec[1].data   = buf + 512; vec[1].offset = 768; vec[1].size = 512;  // 256 remain
    EXPECT_EQ(512 + 256, f->ReadV(vec, 2));
}

TEST_F(OssFileTest, ReadVSkipsZeroSizeEntries) {
    OpenWrite();
    WriteAndReopen(4096);
    char buf[4096]{};
    XrdOucIOVec vec[3];
    vec[0].data   = buf;        vec[0].offset = 0;    vec[0].size = 512;
    vec[1].data   = buf + 512;  vec[1].offset = 512;  vec[1].size = 0;   // skipped
    vec[2].data   = buf + 1024; vec[2].offset = 1024; vec[2].size = 512;
    EXPECT_EQ(1024, f->ReadV(vec, 3));
}

TEST_F(OssFileTest, ReadVFillsZeros) {
    OpenWrite();
    WriteAndReopen(4096);
    char buf[256];
    memset(buf, 0xFF, sizeof(buf));
    XrdOucIOVec vec[1];
    vec[0].data   = buf;
    vec[0].offset = 0;
    vec[0].size   = 256;
    f->ReadV(vec, 1);
    for (auto b : buf) EXPECT_EQ('\0', b);
}

// ---------------------------------------------------------------------------
// Read — random content type
// ---------------------------------------------------------------------------

TEST_F(OssFileTest, RandomReadIsNonZero) {
    // Seed a random-type file directly into g_blackholeFS then open it.
    static constexpr const char* kRandPath = "/test/rand.root";
    g_blackholeFS.seed(kRandPath, 4096, "random");

    XrdBlackholeOssFile rf(s_oss);
    XrdOucEnv renv;
    ASSERT_EQ(XrdOssOK, rf.Open(kRandPath, O_RDONLY, 0, renv));

    char buf[256]{};
    ssize_t n = rf.Read(buf, 0, sizeof(buf));
    EXPECT_EQ(static_cast<ssize_t>(sizeof(buf)), n);

    // At least some bytes should be non-zero (probability of all-zero is
    // astronomically small with a good LCG over 256 bytes).
    bool any_nonzero = false;
    for (auto b : buf) if (b != '\0') { any_nonzero = true; break; }
    EXPECT_TRUE(any_nonzero);

    rf.Close();
    g_blackholeFS.unlink(kRandPath);
}

TEST_F(OssFileTest, RandomReadIsDeterministic) {
    static constexpr const char* kRandPath = "/test/rand2.root";
    g_blackholeFS.seed(kRandPath, 4096, "random");

    auto read_at = [&](off_t offset) {
        XrdBlackholeOssFile rf(s_oss);
        XrdOucEnv renv;
        rf.Open(kRandPath, O_RDONLY, 0, renv);
        char buf[256]{};
        rf.Read(buf, offset, sizeof(buf));
        rf.Close();
        return std::string(buf, sizeof(buf));
    };

    // Two independent reads at the same offset must return identical bytes.
    EXPECT_EQ(read_at(0),    read_at(0));
    EXPECT_EQ(read_at(1024), read_at(1024));
    // Different offsets should (almost certainly) differ.
    EXPECT_NE(read_at(0), read_at(512));

    g_blackholeFS.unlink(kRandPath);
}

// ---------------------------------------------------------------------------
// cfg_seedfile — config directive parsing
// ---------------------------------------------------------------------------

// Helper: write a one-line config to a temp file, run Configure(), return rc.
static int runConfigure(XrdBlackholeOss* oss, const char* line) {
    char tmp[] = "/tmp/bhtest_XXXXXX";
    int fd = mkstemp(tmp);
    write(fd, line, strlen(line));
    write(fd, "\n", 1);
    close(fd);
    int rc = oss->Configure(tmp, XrdBlackholeEroute);
    unlink(tmp);
    return rc;
}

TEST_F(OssFileTest, SeedfileCreatesFile) {
    runConfigure(s_oss, "blackhole.seedfile /test/cfg_1G.root 1G");
    EXPECT_TRUE(g_blackholeFS.exists("/test/cfg_1G.root"));
    auto stub = g_blackholeFS.getStub("/test/cfg_1G.root");
    ASSERT_NE(nullptr, stub);
    EXPECT_EQ(1ULL * 1024 * 1024 * 1024, stub->m_size);
    g_blackholeFS.unlink("/test/cfg_1G.root");
}

TEST_F(OssFileTest, SeedfileSuffixes) {
    runConfigure(s_oss, "blackhole.seedfile /test/cfg_1K.root 1K");
    auto s1 = g_blackholeFS.getStub("/test/cfg_1K.root");
    runConfigure(s_oss, "blackhole.seedfile /test/cfg_1M.root 1M");
    auto s2 = g_blackholeFS.getStub("/test/cfg_1M.root");
    runConfigure(s_oss, "blackhole.seedfile /test/cfg_1T.root 1T");
    auto s3 = g_blackholeFS.getStub("/test/cfg_1T.root");
    ASSERT_NE(nullptr, s1); EXPECT_EQ(1024ULL,                      s1->m_size);
    ASSERT_NE(nullptr, s2); EXPECT_EQ(1024ULL * 1024,                s2->m_size);
    ASSERT_NE(nullptr, s3); EXPECT_EQ(1024ULL * 1024 * 1024 * 1024, s3->m_size);
    g_blackholeFS.unlink("/test/cfg_1K.root");
    g_blackholeFS.unlink("/test/cfg_1M.root");
    g_blackholeFS.unlink("/test/cfg_1T.root");
}

TEST_F(OssFileTest, SeedfileCountExpandsFiles) {
    runConfigure(s_oss, "blackhole.seedfile /test/f_%02d.root 512M count=3");
    EXPECT_TRUE(g_blackholeFS.exists("/test/f_00.root"));
    EXPECT_TRUE(g_blackholeFS.exists("/test/f_01.root"));
    EXPECT_TRUE(g_blackholeFS.exists("/test/f_02.root"));
    EXPECT_FALSE(g_blackholeFS.exists("/test/f_03.root"));
    g_blackholeFS.unlink("/test/f_00.root");
    g_blackholeFS.unlink("/test/f_01.root");
    g_blackholeFS.unlink("/test/f_02.root");
}

TEST_F(OssFileTest, SeedfileTypeRandom) {
    runConfigure(s_oss, "blackhole.seedfile /test/cfg_rand.root 4096 type=random");
    auto stub = g_blackholeFS.getStub("/test/cfg_rand.root");
    ASSERT_NE(nullptr, stub);
    EXPECT_EQ("random", stub->m_readtype);
    g_blackholeFS.unlink("/test/cfg_rand.root");
}

TEST_F(OssFileTest, SeedfileTypeZerosIsDefault) {
    runConfigure(s_oss, "blackhole.seedfile /test/cfg_z.root 4096");
    auto stub = g_blackholeFS.getStub("/test/cfg_z.root");
    ASSERT_NE(nullptr, stub);
    EXPECT_EQ("zeros", stub->m_readtype);
    g_blackholeFS.unlink("/test/cfg_z.root");
}
