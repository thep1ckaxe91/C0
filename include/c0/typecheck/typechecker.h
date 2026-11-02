#pragma once

#include <memory>

namespace c0 {

class Driver;
class Program;

class TypeChecker {
public:
  explicit TypeChecker(Driver &driver);
  ~TypeChecker();

  TypeChecker(const TypeChecker &) = delete;
  TypeChecker &operator=(const TypeChecker &) = delete;

  bool check(const Program &program);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace c0
