#include "BlackholeFS.hh"

extern XrdSysError XrdBlackholeEroute;

    bool BlackholeFS::exists(const std::string& fname) {
      std::unique_lock<std::mutex> lock(m_mutexFD);
      if (m_files.find(fname) == m_files.end()) {
        return false;
      } else {
        return true; 
      }
    }

    Stub * BlackholeFS::getStub(const std::string& fname) {
      std::unique_lock<std::mutex> lock(m_mutexFD);
      auto citr = m_files.find(fname); 
      if (citr == m_files.end()) return nullptr;
      return citr->second; 
    }

    int BlackholeFS::unlink(const std::string& fname) {
      std::unique_lock<std::mutex> lock(m_mutexFD);
      auto citr = m_files.find(fname); 
      // no file found
      if (citr == m_files.end()) return -ENOENT;
      // remove the file
      m_files.erase(citr);
      return 0;
    }

    int BlackholeFS::open(const std::string& fname, int flags, int mode) {

      Stub * stub = getStub(fname);
      bool fileExists = stub == nullptr ? false : true; 
      XrdBlackholeEroute.Say("BH::Open ", fname.c_str());

      if ((flags&O_ACCMODE) == O_RDONLY) {
        // read only
        // can't open a non-existing file ... 
        if (!fileExists) return -ENOENT;
        std::unique_lock<std::mutex> lock(m_mutexFD);
        unsigned long long fd = ++m_fd_last;
        stub->m_fd = fd; 
        stub->m_isOpen = true;
        stub->m_flags = flags;
        stub->m_mode = mode;
        return fd;
      }
      // access mode is has write:
      if (fileExists) {
        if (flags & O_TRUNC) {
          int rc = unlink(fname);
          // if an error (excluding not exists error, return the error)
          if (rc < 0 && (rc != -ENOENT)) return rc; 
        } else {
          if (flags & O_EXCL) return -EACCES; // perm denied
          // if here then file exists, but no access method is provided
          // to be able to open the file
          return -EEXIST; // otherwise return just file exists
        }
      } //exists 

      // At this point, we know either the target file didn't exist, 
      // or the unlink above removed it

      std::unique_lock<std::mutex> lock(m_mutexFD);
      unsigned long long tmp = ++m_fd_last; 
      stub = new Stub;
      m_files.insert ( std::pair<std::string, Stub*>(fname, stub) );
      stub->m_isOpen = true;
      stub->m_isOpenWrite = true;
      stub->m_fd = tmp;
      stub->m_size = 0;
      stub->m_flags = flags;
      stub->m_mode = mode;  
      return tmp;
    }

    void BlackholeFS::close(const std::string& fname) {
      Stub * stub = getStub(fname);
      bool fileExists = stub == nullptr ? false : true; 
      if (!fileExists) { 
          // XrdBlackholeEroute.Say("Closing file, but it doesn't exist");
          return;
      }
      // possible race condidtion with initial get (i.e. if another op happens)
      std::unique_lock<std::mutex> lock(m_mutexFD);
      stub->m_isOpen =false; 
      stub->m_isOpenWrite = false;
    } // close


void BlackholeFS::create_defaults(const std::string & path) {
    // lock as updating the fd value
    std::unique_lock<std::mutex> lock(m_mutexFD);
      unsigned long long tmp = ++m_fd_last; 
      auto stub = new Stub;
      stub->m_isOpen = false;
      stub->m_isOpenWrite = false;
      stub->m_fd = tmp;
      stub->m_flags = 0;
      stub->m_mode = 0;     
      stub->m_special = true;
      stub->m_readtype = "zeros";
      
    stub->m_size = 1024*1024;
    stub->m_stat.st_size = 1024*1024;
      m_files.insert ( std::pair<std::string, Stub*>(path+"/testfile_zeros_1MiB", stub) );
    std::clog << "Creating: " << path+"/testfile_zeros_1MiB" << std::endl;

    stub = new Stub;
      stub->m_isOpen = false;
      stub->m_isOpenWrite = false;
      stub->m_fd = tmp;
      stub->m_flags = 0;
      stub->m_mode = 0;     
      stub->m_special = true;
      stub->m_readtype = "zeros";
      
    stub->m_size = 1024*1024*1024;
    stub->m_stat.st_size = 1024*1024*1024;
      m_files.insert ( std::pair<std::string, Stub*>(path+"/testfile_zeros_1GiB", stub) );
    std::clog << "Creating: " << path+"/testfile_zeros_1GiB" << std::endl;


    stub = new Stub;
      stub->m_isOpen = false;
      stub->m_isOpenWrite = false;
      stub->m_fd = tmp;
      stub->m_flags = 0;
      stub->m_mode = 0;     
      stub->m_special = true;
      stub->m_readtype = "zeros";
      
    stub->m_size = 10737418240;
    stub->m_stat.st_size = 10737418240;
      m_files.insert ( std::pair<std::string, Stub*>(path+"/testfile_zeros_10GiB", stub) );
    std::clog << "Creating: " << path+"/testfile_zeros_10GiB" << std::endl;


} 

