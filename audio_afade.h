#pragma once
#include <memory>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libswresample/swresample.h> 
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

class AudioAfade {
public:
  enum FadeType { FADE_NONE, FADE_IN, FADE_OUT };

  AudioAfade(int sample_rate, int channels,AVSampleFormat sample_fmt, FadeType type, int total_frames);
  ~AudioAfade();

  // 处理一段 AAC 数据（可能包含多帧）
  bool Process(AVPacket *src_pkt, AVPacket *dst_pkt);

private:
  bool InitFilterGraph();
  bool SendToFilter(AVFrame *frame);
  bool ReceiveFromFilter(AVPacket &out_pkt);
  void Cleanup();

  AVCodecContext *dec_ctx_ = nullptr;
  AVCodecContext *enc_ctx_ = nullptr;

  AVFilterGraph *filter_graph_ = nullptr;
  AVFilterContext *src_ctx_ = nullptr;
  AVFilterContext *sink_ctx_ = nullptr;

  FadeType type_;
  int sample_rate_;
  int channels_;
  AVSampleFormat sample_fmt_;

  int total_frames_; // 多少帧淡入或淡出
  int64_t pts_counter_ = 0; // 维护连续时间戳
};
