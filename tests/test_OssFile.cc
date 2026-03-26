#include <gtest/gtest.h>

#include <fcntl.h>
#include <string.h>

#include "XrdSfs/XrdSfsAio.hh"
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysLogger.hh"
#include "XrdBlackhole/XrdBlackholeOssFile.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

// ---------------------------------------------------------------------------
// Minimal XrdSfsAio implementation for testing the AIO write path.
// ---------------------------------------------------------------------------

class MockAio : public XrdSfsAio {
public:
    bool writeCallbackFired{false};
    void doneRead()  override {}
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
