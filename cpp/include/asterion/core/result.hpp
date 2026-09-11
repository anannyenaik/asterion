#pragma once

#include <stdexcept>
#include <utility>
#include <variant>

namespace asterion {

struct Unit {};

template <typename T, typename E> class Result {
public:
  static Result ok(T value) { return Result(true, std::move(value)); }
  static Result err(E error) { return Result(false, std::move(error)); }

  [[nodiscard]] bool is_ok() const noexcept { return ok_; }
  [[nodiscard]] bool is_err() const noexcept { return !ok_; }

  [[nodiscard]] const T& value() const {
    if (!ok_) {
      throw std::logic_error("attempted to read value from error Result");
    }
    return std::get<T>(storage_);
  }

  [[nodiscard]] T& value() {
    if (!ok_) {
      throw std::logic_error("attempted to read value from error Result");
    }
    return std::get<T>(storage_);
  }

  [[nodiscard]] const E& error() const {
    if (ok_) {
      throw std::logic_error("attempted to read error from ok Result");
    }
    return std::get<E>(storage_);
  }

private:
  explicit Result(bool ok, T value) : ok_(ok), storage_(std::move(value)) {}
  explicit Result(bool ok, E error) : ok_(ok), storage_(std::move(error)) {}

  bool ok_;
  std::variant<T, E> storage_;
};

} // namespace asterion
