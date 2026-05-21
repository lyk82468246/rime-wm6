//
// rime/dict/mapped_file.cc -- WinCE-port mirror of upstream mapped_file.cc.
//
// Upstream uses boost::interprocess::file_mapping and std::filesystem; both
// are unavailable on MSVC 9.0 / WinCE 6. We replace MappedFileImpl with a
// direct Win32 CE wrapper around CreateFileW / CreateFileMappingW /
// MapViewOfFile. The public surface of MappedFile is unchanged.
//
// MVP scope is READ-ONLY: Create / Resize / OpenReadWrite / Flush /
// CreateString / CopyString are defined so downstream code links, but they
// are tested-only-as-stubs on-device. The first real read-write user will
// be the user_dict, which is post-MVP.
//
// `MappedFileImpl::Flush()` returns true unconditionally; for read-only
// views there is nothing to flush. When the read-write path comes back,
// replace with FlushViewOfFile + FlushFileBuffers.
//
#include <windows.h>
#include <rime/dict/mapped_file.h>

namespace rime {

class MappedFileImpl {
 public:
  enum OpenMode { kOpenReadOnly, kOpenReadWrite };

  MappedFileImpl(const path& file_path, OpenMode mode)
      : file_(INVALID_HANDLE_VALUE),
        mapping_(NULL),
        view_(NULL),
        size_(0),
        ok_(false) {
    DWORD access  = (mode == kOpenReadOnly) ? GENERIC_READ
                                            : (GENERIC_READ | GENERIC_WRITE);
    DWORD share   = FILE_SHARE_READ;
    DWORD prot    = (mode == kOpenReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
    DWORD vaccess = (mode == kOpenReadOnly) ? FILE_MAP_READ
                                            : FILE_MAP_ALL_ACCESS;

    std::wstring wpath = file_path.wstring();
    file_ = CreateFileW(wpath.c_str(), access, share, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file_ == INVALID_HANDLE_VALUE)
      return;

    // WinCE has no GetFileSizeEx in the WM6 SDK; GetFileSize(hi=NULL) is
    // good enough for our .bin files (single-digit MB).
    DWORD lo = GetFileSize(file_, NULL);
    if (lo == INVALID_FILE_SIZE) {
      CloseHandle(file_);
      file_ = INVALID_HANDLE_VALUE;
      return;
    }
    size_ = lo;

    mapping_ = CreateFileMappingW(file_, NULL, prot, 0, 0, NULL);
    if (!mapping_) {
      CloseHandle(file_);
      file_ = INVALID_HANDLE_VALUE;
      return;
    }

    view_ = MapViewOfFile(mapping_, vaccess, 0, 0, 0);
    if (!view_) {
      CloseHandle(mapping_);
      mapping_ = NULL;
      CloseHandle(file_);
      file_ = INVALID_HANDLE_VALUE;
      return;
    }

    ok_ = true;
  }

  ~MappedFileImpl() {
    if (view_) UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(mapping_);
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
  }

  bool ok() const { return ok_; }
  bool Flush() { return true; }
  void* get_address() const { return view_; }
  size_t get_size() const { return size_; }

 private:
  MappedFileImpl(const MappedFileImpl&);
  MappedFileImpl& operator=(const MappedFileImpl&);

  HANDLE file_;
  HANDLE mapping_;
  void*  view_;
  size_t size_;
  bool   ok_;
};

MappedFile::MappedFile(const path& file_path)
    : file_path_(file_path), size_(0) {}

MappedFile::~MappedFile() {
  if (file_) {
    file_.reset();
  }
}

bool MappedFile::Create(size_t capacity) {
  // MVP: read-only. The first read-write consumer (user_dict) is post-MVP;
  // restore the SetFilePointer + SetEndOfFile + CreateFileMappingW(rw)
  // sequence then. For now just refuse.
  (void)capacity;
  return false;
}

bool MappedFile::OpenReadOnly() {
  if (!Exists()) {
    LOG(ERROR) << "attempt to open non-existent file '"
               << file_path_.string() << "'.";
    return false;
  }
  MappedFileImpl* impl =
      new MappedFileImpl(file_path_, MappedFileImpl::kOpenReadOnly);
  if (!impl->ok()) {
    delete impl;
    return false;
  }
  file_.reset(impl);
  size_ = file_->get_size();
  return true;
}

bool MappedFile::OpenReadWrite() {
  // MVP: read-only. See Create() comment.
  return false;
}

void MappedFile::Close() {
  if (file_) {
    file_.reset();
    size_ = 0;
  }
}

bool MappedFile::Exists() const {
  return wince::exists(file_path_);
}

bool MappedFile::IsOpen() const {
  return !!file_;
}

bool MappedFile::Flush() {
  if (!file_)
    return false;
  return file_->Flush();
}

bool MappedFile::ShrinkToFit() {
  // MVP: read-only.
  return false;
}

bool MappedFile::Remove() {
  if (IsOpen())
    Close();
  std::wstring wpath = file_path_.wstring();
  return DeleteFileW(wpath.c_str()) != 0;
}

bool MappedFile::Resize(size_t capacity) {
  // MVP: read-only. The .bin files are precompiled on desktop.
  (void)capacity;
  return false;
}

String* MappedFile::CreateString(const string& str) {
  String* ret = Allocate<String>();
  if (ret && !str.empty()) {
    CopyString(str, ret);
  }
  return ret;
}

bool MappedFile::CopyString(const string& src, String* dest) {
  if (!dest)
    return false;
  size_t size = src.length() + 1;
  char* ptr = Allocate<char>(size);
  if (!ptr)
    return false;
  std::strncpy(ptr, src.c_str(), size);
  dest->data = ptr;
  return true;
}

size_t MappedFile::capacity() const {
  return file_->get_size();
}

char* MappedFile::address() const {
  return reinterpret_cast<char*>(file_->get_address());
}

}  // namespace rime
