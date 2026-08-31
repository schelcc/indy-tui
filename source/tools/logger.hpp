#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <tuple>

#include "time.hpp"
#include "tools/queue.hpp"
#include "tools/uuid.hpp"

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
  ThreadSafe::Queue<std::string, 20> _write_queue;

  std::thread _write_thread;

  // std::atomic_bool keep_logging{true};
  std::atomic_flag keep_logging{true};

  std::mutex _log_out_mtx;
  std::string_view _log_out;

  Log() {

    _write_thread = std::thread{[this] {
      auto tmp_name = "tmp_log_" + uuid() + ".log";

      auto write_out = std::fstream(tmp_name);

      if (!write_out.is_open()) {
        write_out.clear();
        write_out.open(tmp_name, std::ios::out);
        write_out.close();
        write_out.open(tmp_name);
      }

      write_out.clear();

      size_t flush_count = 0;

      while (keep_logging.test() || !_write_queue.empty()) {
        auto line = _write_queue.try_dequeue_for(Time::Duration::UIntSec(1));
        if (!line.has_value())
          continue;
        write_out << line.value();
        if (flush_count++ % 50 == 0)
          write_out.flush();
      }

      write_out.close();

      {
        std::scoped_lock lock(_log_out_mtx);
        try {
          std::filesystem::rename(tmp_name, _log_out);
        } catch (std::filesystem::filesystem_error &e) {
          std::cerr << std::format("Writing log to '{}' failed: {}", _log_out,
                                   e.what());
        }
      }
    }};
    pthread_setname_np(_write_thread.native_handle(), "Log writer");
  };

  ~Log() {

    keep_logging.clear();

    _write_thread.join();
  }

  static Log &Get() {
    static Log logger{};
    return logger;
  }

  static constexpr std::array<std::string_view, 6> LEVEL_STR{
      "NONE", "ERROR", "WARN", "INFO", "DEBUG", "DEBUG-EX"};

  void log(Level kind, std::string_view msg, std::string_view source = "") {

    if (kind <= _level) {
      _write_queue.enqueue(std::format("[{}{}{}] {}\n", LEVEL_STR[kind],
                                       source.length() > 0 ? ':' : ' ', source,
                                       msg));
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

  static void SetOut(std::string_view const fname) {

    std::scoped_lock lock(Get()._log_out_mtx);
    Get()._log_out = fname;
  }

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

  // [[nodiscard]] static double GetWriteRate() { return Get()._log_hz.load(); }
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

template <Lockable Mut> struct LoggedSharedLock {
  LoggedSharedLock(std::string_view source_ref, Mut &mut)
      : _mut(mut), _source_ref(source_ref) {
#ifndef NDEBUG
    Log::Debug2(std::format("Block for shared-lock at {}", _source_ref),
                "SHARED-LOCK");
#endif
    _lock = std::shared_lock(_mut);
    _lock.lock();
#ifndef NDEBUG
    Log::Debug2(std::format("Shared-lock acquired at {}", _source_ref),
                "SHARED-LOCK");
#endif
  }

  ~LoggedSharedLock() {
#ifndef NDEBUG
    Log::Debug2(std::format("Release shared lock for {}", _source_ref),
                "SHARED-LOCK");
#endif
    _lock.unlock();
  }

private:
  Mut &_mut;
  std::shared_lock<Mut> _lock;
  std::string_view _source_ref;
};

template <Lockable Mut> struct LoggedUniqueLock {
  LoggedUniqueLock(std::string_view source_ref, Mut &mut)
      : _mut(mut), _source_ref(source_ref) {
#ifndef NDEBUG
    Log::Debug2(std::format("Block for unique-lock at {}", _source_ref),
                "UNIQUE-LOCK");
#endif
    _lock = std::unique_lock(mut);
    _lock.lock;
#ifndef NDEBUG
    Log::Debug2(std::format("Unique-lock acquired at {}", _source_ref),
                "SHARED-LOCK");
#endif
  }

  ~LoggedUniqueLock() {
#ifndef NDEBUG
    Log::Debug2(std::format("Release unique lock for {}", _source_ref),
                "SHARED-LOCK");
#endif
    _lock.unlock();
  }

private:
  Mut &_mut;
  std::unique_lock<Mut> _lock;
  std::string_view _source_ref;
};

}; // namespace Tools
