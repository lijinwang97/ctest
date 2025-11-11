#pragma once
#include <memory>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

#include <cstdint>
#include <vector>

std::string PrintHexPreview(const std::string &buf, size_t max_bytes = 64);

class AudioAfade {
public:
  enum FadeType { FADE_NONE, FADE_IN, FADE_OUT };

  AudioAfade(int sample_rate, int channels, AVSampleFormat sample_fmt,
             FadeType type, int total_frames);
  ~AudioAfade();

  // 处理一段 AAC 数据（可能包含多帧）
  bool Process(AVPacket *src_pkt, AVPacket *dst_pkt);
  bool ProcessRaw(const char *in_buf, int in_len, std::string &out_buf);
  void FlushEncoder(AVFormatContext *out_fmt, int64_t &next_pts);
  void PrintPacketHex(const AVPacket *pkt, int max_bytes = 64);
  void WriteAdtsHeader(uint8_t *adts_header, int aac_length, int profile,
                       int sample_rate, int channels);

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

  int total_frames_;        // 多少帧淡入或淡出
  int64_t pts_counter_ = 0; // 维护连续时间戳
};
