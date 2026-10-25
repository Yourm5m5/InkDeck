// Fake SD card backed by a folder on the PC.
#pragma once
#include "Arduino.h"
#include "SPI.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <vector>
std::string sdRoot();
class File {
 public:
  File() {}
  File(const std::string& path, const char* mode) : path_(path) {
    struct stat st;
    std::string real = sdRoot() + path;
    if (stat(real.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      dir_ = true;
      DIR* d = opendir(real.c_str());
      while (dirent* e = readdir(d)) if (e->d_name[0] != '.' || strlen(e->d_name) > 2) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) entries_.push_back(e->d_name);
      closedir(d);
      std::sort(entries_.begin(), entries_.end());
      ok_ = true;
    } else {
      f_ = std::shared_ptr<FILE>(fopen(real.c_str(), mode[0] == 'w' ? "w+b" : "rb"), [](FILE* f) { if (f) fclose(f); });
      ok_ = f_.get() != nullptr;
    }
  }
  explicit operator bool() const { return ok_; }
  const char* name() const { return path_.c_str() + path_.rfind('/') + 1; }
  bool isDirectory() const { return dir_; }
  uint32_t size() { long p = ftell(f_.get()); fseek(f_.get(), 0, SEEK_END); long s = ftell(f_.get()); fseek(f_.get(), p, SEEK_SET); return s; }
  bool seek(uint32_t o) { return fseek(f_.get(), o, SEEK_SET) == 0; }
  int read(uint8_t* d, size_t n) { return fread(d, 1, n, f_.get()); }
  size_t write(const uint8_t* d, size_t n) { return fwrite(d, 1, n, f_.get()); }
  void print(int v) { fprintf(f_.get(), "%d", v); }
  int parseInt() { int v = 0; fseek(f_.get(), 0, SEEK_SET); if (fscanf(f_.get(), "%d", &v) != 1) v = 0; return v; }
  void close() { f_.reset(); ok_ = false; }
  File openNextFile() {
    if (next_ >= entries_.size()) return File();
    std::string p = (path_ == "/" ? "" : path_) + "/" + entries_[next_++];
    return File(p, "r");
  }
 private:
  std::string path_;
  std::shared_ptr<FILE> f_;
  bool ok_ = false, dir_ = false;
  std::vector<std::string> entries_;
  size_t next_ = 0;
};
struct FakeSD {
  bool begin(int, FakeSPI&) { return true; }
  File open(const String& p, const char* m = "r") { return File(p, m); }
  bool exists(const String& p) { return access((sdRoot() + std::string(p)).c_str(), F_OK) == 0; }
  bool remove(const String& p) { return ::unlink((sdRoot() + std::string(p)).c_str()) == 0; }
  bool mkdir(const String& p) { return ::mkdir((sdRoot() + std::string(p)).c_str(), 0755) == 0; }
};
extern FakeSD SD;
