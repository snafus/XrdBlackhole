#include <gtest/gtest.h>

#include <algorithm>
#include <fcntl.h>
#include <string.h>
#include <vector>

#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysLogger.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"
#include "XrdBlackhole/XrdBlackholeOssDir.hh"

// ---------------------------------------------------------------------------
// Test fixture — mirrors OssFileTest: one XrdBlackholeOss shared across the
// suite, logger wired to /dev/null, g_blackholeFS cleaned up after each test.
// ---------------------------------------------------------------------------

class OssDirTest : public ::testing::Test {
public:
    static XrdSysLogger*    s_logger;
    static XrdBlackholeOss* s_oss;

    static void SetUpTestSuite() {
        static int devnull = open("/dev/null", O_WRONLY);
        static XrdSysLogger logger(devnull, 0);
        s_logger = &logger;
        XrdBlackholeEroute.logger(s_logger);
        s_oss = new XrdBlackholeOss(nullptr, XrdBlackholeEroute);
    }

    static void TearDownTestSuite() {
        delete s_oss;
        s_oss = nullptr;
    }

    XrdBlackholeOssDir* d{nullptr};
    XrdOucEnv env;

    void SetUp() override {
        d = new XrdBlackholeOssDir(s_oss);
    }

    void TearDown() override {
        delete d;
        d = nullptr;
        // Clean up any files created during the test.
        for (const auto& p : m_created) g_blackholeFS.unlink(p);
        m_created.clear();
    }

    // Create a file in g_blackholeFS and register it for cleanup.
    void CreateFile(const char* path) {
        g_blackholeFS.open(path, O_WRONLY | O_CREAT, 0644);
        m_created.push_back(path);
    }

    // Drain Readdir into a vector; stops at empty string or error.
    std::vector<std::string> ReadAll(int bufsz = 256) {
        std::vector<std::string> result;
        std::vector<char> buf(bufsz);
        for (;;) {
            int rc = d->Readdir(buf.data(), bufsz);
            EXPECT_EQ(XrdOssOK, rc);
            if (rc != XrdOssOK || buf[0] == '\0') break;
            result.push_back(buf.data());
        }
        return result;
    }

private:
    std::vector<std::string> m_created;
};

XrdSysLogger*    OssDirTest::s_logger{nullptr};
XrdBlackholeOss* OssDirTest::s_oss{nullptr};

// ---------------------------------------------------------------------------
// Opendir
// ---------------------------------------------------------------------------

TEST_F(OssDirTest, OpendirEmptyDirReturnsOssOK) {
    EXPECT_EQ(XrdOssOK, d->Opendir("/test", env));
}

TEST_F(OssDirTest, OpendirNonEmptyDirReturnsOssOK) {
    CreateFile("/test/a.root");
    EXPECT_EQ(XrdOssOK, d->Opendir("/test", env));
}

// ---------------------------------------------------------------------------
// Readdir — basic
// ---------------------------------------------------------------------------

TEST_F(OssDirTest, ReaddirOnEmptyDirReturnsEmptyString) {
    d->Opendir("/test", env);
    char buf[256]{};
    EXPECT_EQ(XrdOssOK, d->Readdir(buf, sizeof(buf)));
    EXPECT_EQ('\0', buf[0]);
}

TEST_F(OssDirTest, ReaddirReturnsSingleEntry) {
    CreateFile("/test/a.root");
    d->Opendir("/test", env);
    auto entries = ReadAll();
    ASSERT_EQ(1u, entries.size());
    EXPECT_EQ("a.root", entries[0]);
}

TEST_F(OssDirTest, ReaddirReturnsAllDirectChildren) {
    CreateFile("/test/a.root");
    CreateFile("/test/b.root");
    CreateFile("/test/c.root");
    d->Opendir("/test", env);
    auto entries = ReadAll();
    ASSERT_EQ(3u, entries.size());
    EXPECT_NE(entries.end(), std::find(entries.begin(), entries.end(), "a.root"));
    EXPECT_NE(entries.end(), std::find(entries.begin(), entries.end(), "b.root"));
    EXPECT_NE(entries.end(), std::find(entries.begin(), entries.end(), "c.root"));
}

TEST_F(OssDirTest, ReaddirExcludesFilesInSubdirectory) {
    CreateFile("/test/a.root");
    CreateFile("/test/sub/b.root");
    d->Opendir("/test", env);
    auto entries = ReadAll();
    ASSERT_EQ(1u, entries.size());
    EXPECT_EQ("a.root", entries[0]);
}

TEST_F(OssDirTest, ReaddirExcludesFilesInOtherDirectory) {
    CreateFile("/test/a.root");
    CreateFile("/other/b.root");
    d->Opendir("/test", env);
    auto entries = ReadAll();
    ASSERT_EQ(1u, entries.size());
    EXPECT_EQ("a.root", entries[0]);
}

// ---------------------------------------------------------------------------
// Readdir — end-of-directory signalling
// ---------------------------------------------------------------------------

TEST_F(OssDirTest, ReaddirSignalsEodWithEmptyString) {
    CreateFile("/test/a.root");
    d->Opendir("/test", env);
    char buf[256]{};
    d->Readdir(buf, sizeof(buf));  // consume "a.root"
    buf[0] = 'X';                  // sentinel
    EXPECT_EQ(XrdOssOK, d->Readdir(buf, sizeof(buf)));
    EXPECT_EQ('\0', buf[0]);
}

TEST_F(OssDirTest, ReaddirIdempotentAfterEod) {
    d->Opendir("/test", env);
    char buf[256]{};
    d->Readdir(buf, sizeof(buf));  // first EOD
    buf[0] = 'X';
    EXPECT_EQ(XrdOssOK, d->Readdir(buf, sizeof(buf)));  // second call
    EXPECT_EQ('\0', buf[0]);
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

TEST_F(OssDirTest, CloseReturnsOssOK) {
    d->Opendir("/test", env);
    EXPECT_EQ(XrdOssOK, d->Close());
}

TEST_F(OssDirTest, ReaddirAfterCloseReturnsEod) {
    CreateFile("/test/a.root");
    d->Opendir("/test", env);
    d->Close();
    char buf[256]{};
    buf[0] = 'X';
    EXPECT_EQ(XrdOssOK, d->Readdir(buf, sizeof(buf)));
    EXPECT_EQ('\0', buf[0]);
}
