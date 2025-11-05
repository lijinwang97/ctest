#include "logger.h"

#ifdef _WIN32
#include <direct.h>
#endif
#include <time.h>

#include <iostream>
#include <sstream>

namespace libmagic {

inline int NowDateToInt() {
  time_t now;
  time(&now);

  // choose thread save version in each platform
  tm p;
#ifdef _WIN32
  localtime_s(&p, &now);
#else
  localtime_r(&now, &p);
#endif  // _WIN32
  int now_date = (1900 + p.tm_year) * 10000 + (p.tm_mon + 1) * 100 + p.tm_mday;
  return now_date;
}

inline int NowTimeToInt() {
  time_t now;
  time(&now);
  // choose thread save version in each platform
  tm p;
#ifdef _WIN32
  localtime_s(&p, &now);
#else
  localtime_r(&now, &p);
#endif  // _WIN32

  int now_int = p.tm_hour * 10000 + p.tm_min * 100 + p.tm_sec;
  return now_int;
}

inline std::string GetCurrentLogPath() {
  const int kMaxPathSize = 512;

  char buff[kMaxPathSize] = {0};
#ifdef _WIN32
  _getcwd(buff, kMaxPathSize);
#else
  getcwd(buff, kMaxPathSize);
#endif
  return std::string(buff);
}

bool Logger::Init(const string& level, const string& path,
                   int port, bool console, bool reopen) {
  // should create the folder if not exist
  const std::string log_dir = path;
  const std::string logger_name_prefix =
      "libmagic_" + std::to_string(port) + "_";

  try {
    if (logger_ && !reopen) {
      // 只更新日志等级
      if (level == "trace") {
        logger_->set_level(spdlog::level::trace);
        logger_->flush_on(spdlog::level::trace);
      } else if (level == "debug") {
        logger_->set_level(spdlog::level::debug);
        logger_->flush_on(spdlog::level::debug);
      } else if (level == "info") {
        logger_->set_level(spdlog::level::info);
        logger_->flush_on(spdlog::level::info);
      } else if (level == "warn") {
        logger_->set_level(spdlog::level::warn);
        logger_->flush_on(spdlog::level::warn);
      } else if (level == "error") {
        logger_->set_level(spdlog::level::err);
        logger_->flush_on(spdlog::level::err);
      }

      std::cout << "Logger config updated without reopening file." << std::endl;
      return true;
    }

    // logger name with timestamp
    int date = NowDateToInt();
    int time = NowTimeToInt();
    const std::string logger_name =
        logger_name_prefix + std::to_string(date) + "_" + std::to_string(time);

    if (console)
      // logger_ = spdlog::stdout_color_st(logger_name); // single thread
      // console output faster
      logger_ = spdlog::stdout_logger_st(
          logger_name);  // single thread console output faster
    else
      // m_logger =
      // spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(logger_name,
      // log_dir + "/" + logger_name + ".log"); // only one log file
      logger_ = spdlog::create_async<spdlog::sinks::rotating_file_sink_mt>(
          logger_name, log_dir + "/" + logger_name + ".log", 100 * 1024 * 1024,
          10);  // multi part log files, with every part 500M, max 10 files

    // custom format
    std::ostringstream oss;
    oss << "%Y-%m-%d %H:%M:%S.%f <tid:%t> [%l] [%s:%#] %v";
    logger_->set_pattern(oss.str());  // with
                                                                    // timestamp,
                                                                    // thread_id,
                                                                    // filename
                                                                    // and line
                                                                    // number

    if (level == "trace") {
      logger_->set_level(spdlog::level::trace);
      logger_->flush_on(spdlog::level::trace);
    } else if (level == "debug") {
      logger_->set_level(spdlog::level::debug);
      logger_->flush_on(spdlog::level::debug);
    } else if (level == "info") {
      logger_->set_level(spdlog::level::info);
      logger_->flush_on(spdlog::level::info);
    } else if (level == "warn") {
      logger_->set_level(spdlog::level::warn);
      logger_->flush_on(spdlog::level::warn);
    } else if (level == "error") {
      logger_->set_level(spdlog::level::err);
      logger_->flush_on(spdlog::level::err);
    }
  } catch (const spdlog::spdlog_ex& ex) {
    std::cout << "Log initialization failed: " << ex.what() << std::endl;
    return false;
  }

  return true;
}

Logger::Logger() {}

Logger::~Logger() {
  spdlog::flush_every(std::chrono::seconds(3));
#ifndef _WIN32
  spdlog::shutdown();
#endif
}

}  // namespace libmagic
