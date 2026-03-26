#ifndef __BLACKHOLEFS_OSS_HH__
#define __BLACKHOLEFS_OSS_HH__

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <XrdOss/XrdOss.hh>
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"

#include <sys/stat.h>
#include <sys/errno.h>
#include <fcntl.h>


struct Stub {
  bool m_isOpenWrite {false};
  int m_flags;
  int m_mode;
  unsigned long long m_size;
  struct stat m_stat;
  bool m_special {false};
  std::string m_readtype {"zeros"};
  std::map<std::string, std::string> m_checksums; 
};

class BlackholeFS {
  public:
    BlackholeFS() = default;
    ~BlackholeFS() = default;
    bool exists(const std::string& fname);

    std::shared_ptr<Stub> getStub(const std::string& fname);

    int rename(const std::string& from, const std::string& to);

    // Populate entries with the names (not full paths) of direct children of
    // path.  Returns a snapshot taken under the lock; callers iterate freely.
    void readdir(const std::string& path, std::vector<std::string>& entries);

    int unlink(const std::string& fname);

    int open(const std::string& fname, int flags, int mode);

    void close(const std::string& fname);

    void create_defaults(const std::string & path); //! allow for a default set of files for reading ...

    // Create a single pre-seeded stub with the given size and read type.
    // m_special is set to true; the stub is immediately available for reading.
    void seed(const std::string& path, unsigned long long size,
              const std::string& readtype = "zeros");

  private:
    std::map<std::string, std::shared_ptr<Stub>> m_files;
    unsigned long long m_fd_last = 0;
    std::mutex m_mutexFD;


};

#endif 
