#ifndef __BLACKHOLEFS_OSS_HH__
#define __BLACKHOLEFS_OSS_HH__

#include <string>
#include <XrdOss/XrdOss.hh>
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"

#include <map>
#include <thread>
#include <mutex>

#include <sys/stat.h>
#include <sys/errno.h>
#include <fcntl.h>


struct Stub {
  bool m_isOpen;
  bool m_isOpenWrite;  
  int m_flags;
  int m_mode;
  ssize_t m_size;
  struct stat m_stat; 
  int m_fd;
};

class BlackholeFS {
  public:
    bool exists(const std::string& fname);

    Stub * getStub(const std::string& fname);

    int unlink(const std::string& fname);

    int open(const std::string& fname, int flags, int mode);

    void close(const std::string& fname);


  private:
    std::map<std::string, Stub*>  m_files; 
    unsigned long long m_fd_last = 0;
    std::mutex m_mutexFD;  // Declare a mutex

};

#endif 
