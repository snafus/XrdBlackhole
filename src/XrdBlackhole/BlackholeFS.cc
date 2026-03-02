#include "BlackholeFS.hh"

#include <memory>

extern XrdSysError XrdBlackholeEroute;

bool BlackholeFS::exists(const std::string& fname) {
  std::unique_lock<std::mutex> lock(m_mutexFD);
  return m_files.find(fname) != m_files.end();
}

std::shared_ptr<Stub> BlackholeFS::getStub(const std::string& fname) {
  std::unique_lock<std::mutex> lock(m_mutexFD);
  auto citr = m_files.find(fname);
  if (citr == m_files.end()) return nullptr;
  return citr->second;
}

int BlackholeFS::unlink(const std::string& fname) {
  std::unique_lock<std::mutex> lock(m_mutexFD);
  auto citr = m_files.find(fname);
  if (citr == m_files.end()) return -ENOENT;
  m_files.erase(citr);
  return 0;
}

int BlackholeFS::open(const std::string& fname, int flags, int mode) {
  // Hold the lock for the entire operation so there is no window between the
  // existence check and the subsequent map mutation (TOCTOU). Map ops are
  // inlined rather than delegated to getStub()/unlink() to avoid re-entrant
  // acquisition of the non-recursive mutex.
  XrdBlackholeEroute.Say("BH::Open ", fname.c_str());

  std::unique_lock<std::mutex> lock(m_mutexFD);

  auto citr = m_files.find(fname);
  bool fileExists = (citr != m_files.end());
  Stub* stub = fileExists ? citr->second.get() : nullptr;

  if ((flags & O_ACCMODE) == O_RDONLY) {
    if (!fileExists) return -ENOENT;
    unsigned long long fd = ++m_fd_last;
    stub->m_fd    = fd;
    stub->m_flags = flags;
    stub->m_mode  = mode;
    return fd;
  }

  // Access mode includes write.
  if (fileExists) {
    // O_EXCL means "fail if file already exists", regardless of O_TRUNC.
    if (flags & O_EXCL) return -EEXIST;

    if (flags & O_TRUNC) {
      // Discard the existing stub; a fresh one is created below.
      m_files.erase(citr);
    } else {
      // Write open of an existing file without truncation: reuse the stub.
      // Mark it open-for-write so Close() updates m_size correctly.
      unsigned long long fd = ++m_fd_last;
      stub->m_fd          = fd;
      stub->m_isOpenWrite = true;
      stub->m_flags       = flags;
      stub->m_mode        = mode;
      return fd;
    }
  }

  // Either the file never existed or was just unlinked above.
  unsigned long long tmp = ++m_fd_last;
  auto stub_owned = std::make_shared<Stub>();
  stub = stub_owned.get();
  stub->m_isOpenWrite = true;
  stub->m_fd = tmp;
  stub->m_size = 0;
  stub->m_flags = flags;
  stub->m_mode = mode;
  // XRootD treats a file as "offline" when both st_dev and st_ino are zero.
  // Use the assigned fd as a stable, unique inode number.
  stub->m_stat.st_dev = 1;
  stub->m_stat.st_ino = tmp;
  m_files.insert({fname, stub_owned});
  return tmp;
}

void BlackholeFS::close(const std::string& fname) {
  // Hold the lock for the entire operation (same TOCTOU rationale as open()).
  std::unique_lock<std::mutex> lock(m_mutexFD);
  auto citr = m_files.find(fname);
  if (citr == m_files.end()) return;
  citr->second->m_isOpenWrite = false;
}

void BlackholeFS::create_defaults(const std::string & path) {
  std::unique_lock<std::mutex> lock(m_mutexFD);

  struct FileSpec { const char* suffix; unsigned long long size; };
  static const FileSpec specs[] = {
    { "/testfile_zeros_1MiB",   1ULL * 1024 * 1024 },
    { "/testfile_zeros_1GiB",   1ULL * 1024 * 1024 * 1024 },
    { "/testfile_zeros_10GiB", 10ULL * 1024 * 1024 * 1024 },
  };

  for (const auto& spec : specs) {
    unsigned long long tmp = ++m_fd_last;
    auto stub = std::make_shared<Stub>();
    stub->m_fd = tmp;
    stub->m_flags = 0;
    stub->m_mode = 0;
    stub->m_special = true;
    stub->m_readtype = "zeros";
    stub->m_size = spec.size;
    stub->m_stat.st_size = static_cast<off_t>(spec.size);
    // XRootD treats a file as "offline" when both st_dev and st_ino are zero.
    stub->m_stat.st_dev = 1;
    stub->m_stat.st_ino = tmp;
    std::string fullpath = path + spec.suffix;
    m_files.insert({fullpath, stub});
    XrdBlackholeEroute.Say("blackhole: create_defaults: ", fullpath.c_str());
  }
}
