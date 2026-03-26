#ifndef __BLACKHOLEFS_OSS_HH__
#define __BLACKHOLEFS_OSS_HH__

#include <map>
#include <memory>
#include <mutex>
#include <string>

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

    int unlink(const std::string& fname);

    int open(const std::string& fname, int flags, int mode);

    void close(const std::string& fname);

    void create_defaults(const std::string & path); //! allow for a default set of files for reading ... 

  private:
    std::map<std::string, std::shared_ptr<Stub>> m_files;
    unsigned long long m_fd_last = 0;
    std::mutex m_mutexFD;


};

#endif 
