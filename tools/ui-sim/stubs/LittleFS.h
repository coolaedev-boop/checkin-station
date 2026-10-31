#pragma once
// PC stand-in for LittleFS: files live in memory for the length of a run.
#include <Arduino.h>
#include <map>

class File {
 public:
  File() = default;
  File(std::string* data, bool write) : data_(data), write_(write) {}
  explicit operator bool() const { return data_ != nullptr; }
  String readString() { return data_ ? *data_ : String(); }
  size_t print(const String& s) { if (data_ && write_) *data_ += s; return s.size(); }
  void close() { data_ = nullptr; }
 private:
  std::string* data_ = nullptr;
  bool         write_ = false;
};

struct LittleFSStub {
  std::map<std::string, std::string> files;
  bool begin(bool = false) { return true; }
  bool exists(const char* path) { return files.count(path) > 0; }
  File open(const char* path, const char* mode) {
    if (mode[0] == 'w') return File(&(files[path] = ""), true);
    auto it = files.find(path);
    return it == files.end() ? File() : File(&it->second, false);
  }
};
extern LittleFSStub LittleFS;
