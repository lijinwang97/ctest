#ifndef UTILS_LOGGER_H_
#define UTILS_LOGGER_H_

#ifdef ANDROID
#include <android/log.h>
#endif

#include <memory>

#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"
using namespace std;

namespace libmagic {

class Logger {
 public:
  static Logger* Instance() {
    static Logger logger;
    return &logger;
  }

  bool Init(const string& level, const string& path, int port,
            bool console = true, bool reopen = true);
  spdlog::logger* logger() const { return logger_.get(); }

 private:
  Logger();
  ~Logger();
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // void* operator new(size_t size) { return nullptr; }

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace libmagic

#define LOGGER_INS (libmagic::Logger::Instance())

#define LOG_TRACE(...) \
  SPDLOG_LOGGER_CALL(LOGGER_INS->logger(), spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) \
  SPDLOG_LOGGER_CALL(LOGGER_INS->logger(), spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...) \
  SPDLOG_LOGGER_CALL(LOGGER_INS->logger(), spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...) \
  SPDLOG_LOGGER_CALL(LOGGER_INS->logger(), spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) \
  SPDLOG_LOGGER_CALL(LOGGER_INS->logger(), spdlog::level::err, __VA_ARGS__)

#ifdef ANDROID
#define LOGD(LOG_TAG, ...) \
  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(LOG_TAG, ...) \
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(LOG_TAG, ...) \
  __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(LOG_TAG, ...) \
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#elif __APPLE__
#define DO_NOTHING(LOG_TAG)
#define LOGD(LOG_TAG, ...) LOG_DEBUG(__VA_ARGS__)
#define LOGI(LOG_TAG, ...) LOG_INFO(__VA_ARGS__)
#define LOGW(LOG_TAG, ...) LOG_WARN(__VA_ARGS__)
#define LOGE(LOG_TAG, ...) LOG_ERROR(__VA_ARGS__)
#else
#define LOGD(LOG_TAG, ...) LOG_DEBUG(__VA_ARGS__)
#define LOGI(LOG_TAG, ...) LOG_INFO(__VA_ARGS__)
#define LOGW(LOG_TAG, ...) LOG_WARN(__VA_ARGS__)
#define LOGE(LOG_TAG, ...) LOG_ERROR(__VA_ARGS__)
#endif

#endif  // UTILS_LOGGER_H_
