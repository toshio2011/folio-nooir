#pragma once

#include <string>
#include <vector>

// One StarDict dictionary found under /dictionaries or /.dictionaries: a
// subfolder holding <stem>.idx plus <stem>.dict or <stem>.dict.dz.
struct DictionaryEntry {
  std::string name;  // subfolder name (shown to the user, stored in settings)
  std::string stem;  // index basename without .idx
};

namespace DictionaryRegistry {

// Scan /dictionaries/*/ and /.dictionaries/*/ for dictionaries. Folders with
// multiple index stems are ambiguous and skipped. Result is sorted
// case-insensitively by name.
void discover(std::vector<DictionaryEntry>& out);

// Resolve a persisted folder name. Exact names win; when a legacy 31-byte
// settings/history value is only a prefix, resolve it only if exactly one
// installed dictionary has that prefix. Ambiguous prefixes fail safely.
bool resolveFolderName(const char* folderName, std::string& resolvedNameOut);

// Resolve a folder name to its extensionless base path
// ("/dictionaries/<folder>/<stem>" or "/.dictionaries/<folder>/<stem>").
// Returns false if the folder holds no usable dictionary in either root.
// resolvedNameOut, when supplied, receives the canonical installed folder
// name selected by the resolver.
bool resolveBasePath(const char* folderName, std::string& basePathOut,
                     std::string* resolvedNameOut = nullptr);

}  // namespace DictionaryRegistry
