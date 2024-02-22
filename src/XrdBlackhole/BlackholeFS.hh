#ifndef __BLACKHOLEFS_OSS_HH__
#define __BLACKHOLEFS_OSS_HH__

#include <string>
#include <XrdOss/XrdOss.hh>
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"

#include <map>
#include <thread>
#include <mutex>
#include <string>

#include <sys/stat.h>
#include <sys/errno.h>
#include <fcntl.h>


struct Stub {
  bool m_isOpen;
  bool m_isOpenWrite;  
  int m_flags;
  int m_mode;
  unsigned long long m_size;
  struct stat m_stat; 
  int m_fd;
  bool m_special {false};
  std::string m_readtype {"zeros"};
  std::map<std::string, std::string> m_checksums; 
};

class BlackholeFS {
  public:
    BlackholeFS(){};
    ~BlackholeFS() {m_files.clear();}
    bool exists(const std::string& fname);

    Stub * getStub(const std::string& fname);

    int unlink(const std::string& fname);

    int open(const std::string& fname, int flags, int mode);

    void close(const std::string& fname);

    void create_defaults(const std::string & path); //! allow for a default set of files for reading ... 

  private:
    std::map<std::string, Stub*>  m_files; 
    unsigned long long m_fd_last = 0;
    std::mutex m_mutexFD;  // Declare a mutex


};

#endif 
