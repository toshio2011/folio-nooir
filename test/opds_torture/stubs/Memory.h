#pragma once

#include <memory>
#include <new>
#include <utility>

template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) noexcept {
  try {
    return std::make_unique<T>(std::forward<Args>(args)...);
  } catch (...) {
    return nullptr;
  }
}
