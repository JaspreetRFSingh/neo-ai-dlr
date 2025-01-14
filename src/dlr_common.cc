#include "dlr_common.h"
#include <dmlc/filesystem.h>


using namespace dlr;

std::string dlr::GetBasename(const std::string& path) {
#ifdef _WIN32
  /* remove any trailing backward or forward slashes
     (UNIX does this automatically) */
  std::string path_;
  std::string::size_type tmp = path.find_last_of("/\\");
  if (tmp == path.length() - 1) {
    size_t i = tmp;
    while ((path[i] == '/' || path[i] == '\\') && i >= 0) {
      --i;
    }
    path_ = path.substr(0, i + 1);
  } else {
    path_ = path;
  }
  std::vector<char> fname(path_.length() + 1);
  std::vector<char> ext(path_.length() + 1);
  _splitpath_s(path_.c_str(), NULL, 0, NULL, 0,
    &fname[0], path_.length() + 1, &ext[0], path_.length() + 1);
  return std::string(&fname[0]) + std::string(&ext[0]);
#else
  char* path_ = strdup(path.c_str());
  char* base = basename(path_);
  std::string ret(base);
  free(path_);
  return ret;
#endif
}

void dlr::ListDir(const std::string& dirname, std::vector<std::string>& paths) {
  dmlc::io::URI uri(dirname.c_str());
  dmlc::io::FileSystem* fs = dmlc::io::FileSystem::GetInstance(uri);
  std::vector<dmlc::io::FileInfo> file_list;
  fs->ListDirectory(uri, &file_list);
  for (dmlc::io::FileInfo info : file_list) {
    if (info.type != dmlc::io::FileType::kDirectory) {
      paths.push_back(info.path.name);
    }
  }
}

DLRBackend dlr::GetBackend(const std::string& dirname) {
  // Support the case where user provides full path to tflite file.
  if (EndsWith(dirname, ".tflite")) {
    return DLRBackend::kTFLITE;
  }
  // Scan Directory content to guess the backend.
  std::vector<std::string> paths;
  dlr::ListDir(dirname, paths);
  for (auto filename: paths) {
    if (EndsWith(filename, ".params")) {
      return DLRBackend::kTVM;
    } else if (EndsWith(filename, ".tflite")) {
      return DLRBackend::kTFLITE;
    }
  }
  return DLRBackend::kTREELITE;
}