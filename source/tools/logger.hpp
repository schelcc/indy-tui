#pragma once
#include <array>
#include <mutex>
#include <print>
#include <source_location>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Tools {

class Log {
public:
  enum Level {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    DEBUG2 = 5
  } _level;

private:
  Log() = default;

  static Log &Get() {
    static Log logger{};
    return logger;
  }

  static constexpr std::array<std::string_view, 6> LEVEL_STR{
      "NONE", "ERROR", "WARN", "INFO", "DEBUG", "DEBUG-EX"};

  void log(Level kind, std::string_view msg, std::string_view source = "") {
    if (kind <= _level) {
      char delim = source.length() > 0 ? ':' : '\0';
      std::println("[{}{}{}] {}", LEVEL_STR[kind], delim, source, msg);
    }
  }

public:
  // Delete copy ctors
  Log(const Log &) = delete;
  Log &operator=(const Log &) = delete;

  // Delete move ctors
  Log(Log &&) = delete;
  Log &operator=(Log &&) = delete;

  static void SetLevel(Level level) { Get()._level = level; }

  static void Info(std::string_view msg, std::string_view source = "") {
    Get().log(INFO, msg, source);
  }
  static void Warn(std::string_view msg, std::string_view source = "") {
    Get().log(WARN, msg, source);
  }
  static void Debug(std::string_view msg, std::string_view source = "") {
    Get().log(DEBUG, msg, source);
  }
  static void Debug2(std::string_view msg, std::string_view source = "") {
    Get().log(DEBUG2, msg, source);
  }
  static void Error(std::string_view msg, std::string_view source = "") {
    Get().log(ERROR, msg, source);
  }
};

template <typename T>
concept Lockable = requires(T t) {
  t.lock();
  t.unlock();
};

template <Lockable Head, Lockable... Tail>
void recursed_unlock(Head &head, Tail &...tail) {
  head.unlock();
  if constexpr (sizeof...(Tail) > 0) {
    recursed_unlock(tail...);
  }
}

template <Lockable Head, Lockable... Tail>
void recursed_raw_lock(Head &head, Tail &...tail) {
  head.lock();
  if constexpr (sizeof...(Tail) > 0) {
    recursed_unlock(tail...);
  }
}

template <Lockable... Muts> struct LoggedScopedLock {
  LoggedScopedLock(std::string_view source_ref, Muts &...muts)
      : _mutexes(std::tie(muts...)), _source_ref(source_ref) {

#ifndef NDEBUG
    Log::Debug2(std::format("Block for locks at {}", _source_ref),
                "SCOPE-LOCK");
#endif

    if constexpr (sizeof...(Muts) == 1) {
      recursed_raw_lock(muts...);
    } else {
      std::lock(muts...);
    }

#ifndef NDEBUG
    Log::Debug2(std::format("Locks acquired at {}", _source_ref), "SCOPE-LOCK");
#endif
  }

  ~LoggedScopedLock() {
#ifndef NDEBUG
    Log::Debug2(std::format("Releasing locks at {}", _source_ref),
                "SCOPE-LOCK");
#endif

    std::apply(unlock, _mutexes);
  }

private:
  static void unlock(Muts &...muts) { recursed_unlock(muts...); }

  std::tuple<Muts &...> _mutexes;
  std::string_view _source_ref;
};

}; // namespace Tools
