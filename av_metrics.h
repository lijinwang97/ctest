#pragma once
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/gauge.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class AvMetrics {
public:
  static AvMetrics& Instance();
  void Init(const std::string& addr);

  // 直接设置帧率
  void SetFps(const std::string& room_id, double audio_fps, double video_fps);

  // 一次设置两个 PTS（单位毫秒）
  void SetPtsMs(const std::string& room_id, uint64_t audio_pts_ms, uint64_t video_pts_ms);

  void RemoveRoom(const std::string& room_id);
  void Shutdown();

private:
  AvMetrics() = default;
  ~AvMetrics();

  struct StreamMetrics {
    prometheus::Gauge* audio_fps{nullptr};
    prometheus::Gauge* video_fps{nullptr};
    prometheus::Gauge* audio_pts_sec{nullptr};
    prometheus::Gauge* video_pts_sec{nullptr};
  };
  StreamMetrics& GetOrCreate(const std::string& room_id);

  std::unique_ptr<prometheus::Exposer> exposer_;
  std::shared_ptr<prometheus::Registry> registry_;

  prometheus::Family<prometheus::Gauge>* fps_family_{nullptr}; // libpush_fps{room_id,kind}
  prometheus::Family<prometheus::Gauge>* pts_family_{nullptr}; // libpush_last_pts_seconds{room_id,kind}

  std::mutex mu_;
  std::unordered_map<std::string, std::unique_ptr<StreamMetrics>> rooms_;

  bool inited_{false};
};
