#ifndef MEMORY_HH
#define MEMORY_HH

#include <memory>

template<typename T, typename Deleter>
std::unique_ptr<T, Deleter> make_unique(T *p, Deleter d) {
  return std::unique_ptr<T, Deleter>(p, d);
}

#endif
