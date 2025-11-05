#include "av_metrics.h"

#include <iostream>

AvMetrics& AvMetrics::Instance() {
  static AvMetrics inst;
  return inst;
}

void AvMetrics::Init(const std::string& addr) {
  if (inited_) return;
  exposer_ = std::make_unique<prometheus::Exposer>(addr);
  registry_ = std::make_shared<prometheus::Registry>();

  fps_family_ = &prometheus::BuildGauge()
                     .Name("libpush_fps")
                     .Help("Instant frames per second estimated by libpush")
                     .Register(*registry_);

  pts_family_ = &prometheus::BuildGauge()
                     .Name("libpush_last_pts_milliseconds")
                     .Help("Last media presentation timestamp (milliseconds)")
                     .Register(*registry_);

  exposer_->RegisterCollectable(registry_);
  inited_ = true;
}

AvMetrics::StreamMetrics& AvMetrics::GetOrCreate(const std::string& room_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = rooms_.find(room_id);
  if (it != rooms_.end()) return *it->second;

  auto sm = std::make_unique<StreamMetrics>();
  sm->audio_fps = &fps_family_->Add({{"room_id", room_id}, {"kind", "audio"}});
  sm->video_fps = &fps_family_->Add({{"room_id", room_id}, {"kind", "video"}});
  sm->audio_pts_sec = &pts_family_->Add({{"room_id", room_id}, {"kind", "audio"}});
  sm->video_pts_sec = &pts_family_->Add({{"room_id", room_id}, {"kind", "video"}});

  auto& ref = *sm;
  rooms_.emplace(room_id, std::move(sm));
  return ref;
}

void AvMetrics::SetFps(const std::string& room_id, double audio_fps, double video_fps) {
  auto& m = GetOrCreate(room_id);
  m.audio_fps->Set(audio_fps);
  m.video_fps->Set(video_fps);
}

void AvMetrics::SetPtsMs(const std::string& room_id, uint64_t audio_pts_ms, uint64_t video_pts_ms) {
  auto& m = GetOrCreate(room_id);
  m.audio_pts_sec->Set(static_cast<double>(audio_pts_ms));
  m.video_pts_sec->Set(static_cast<double>(video_pts_ms));
}

void AvMetrics::RemoveRoom(const std::string& room_id) {
  std::lock_guard<std::mutex> lk(mu_);
  rooms_.erase(room_id);
}

void AvMetrics::Shutdown() {
}

AvMetrics::~AvMetrics() { Shutdown(); }
