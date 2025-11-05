#include "av_metrics.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
}
#include "audio_afade.h"

using namespace std::chrono;

// 简单的直播间模拟
struct SimRoom {
  std::string id;
  uint32_t audio_fps;        // 每秒音频帧（比如 50 = 20ms 一帧）
  uint32_t video_fps;        // 每秒视频帧（比如 25/30）
  uint64_t audio_pts_ms = 0; // 最近音频 PTS（毫秒）
  uint64_t video_pts_ms = 0; // 最近视频 PTS（毫秒）
};

int testAvMetrics() {
  // 1) 初始化 metrics 暴露端口
  AvMetrics::Instance().Init("0.0.0.0:8099");

  // 2) 造两间直播间：一个 48/24 fps，一个 50/25 fps
  std::vector<SimRoom> rooms = {
      {"roomA", 48, 24, 0, 0},
      {"roomB", 50, 25, 0, 0},
  };

  // 3) 每秒模拟一次上报（帧率=每秒帧数，PTS 每秒 +1000ms）
  auto last = steady_clock::now();
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto now = steady_clock::now();
    auto win_ms = duration_cast<milliseconds>(now - last).count();
    if (win_ms == 0)
      win_ms = 1000;
    last = now;

    for (auto &r : rooms) {
      // 模拟窗口内的“发送帧数”= 目标 fps * (窗口秒)
      // 因为你希望整数 fps，这里直接等同于帧率
      double a_fps = r.audio_fps * (win_ms / 1000.0);
      double v_fps = r.video_fps * (win_ms / 1000.0);

      // 更新时间戳（媒体时间每秒前进 1000ms，可按需替换为真实 PTS）
      r.audio_pts_ms += win_ms;
      r.video_pts_ms += win_ms;

      // === 上报 ===
      AvMetrics::Instance().SetFps(r.id, a_fps, v_fps);
      AvMetrics::Instance().SetPtsMs(r.id, r.audio_pts_ms, r.video_pts_ms);
    }
  }
}

int testAfade() {
  
}

inline std::string packet_to_string(const AVPacket *pkt) {
  return std::string(reinterpret_cast<const char *>(pkt->data), pkt->size);
}

int main() {
  av_log_set_level(AV_LOG_DEBUG);

  const char *input_file = "/data1/lijinwang/ctest/build/input2.mp3";
  const char *output_file = "output.mp3";

  // 打开输入文件
  AVFormatContext *in_fmt = nullptr;
  if (avformat_open_input(&in_fmt, input_file, nullptr, nullptr) < 0) {
    std::cerr << "❌ Failed to open input file\n";
    return -1;
  }
  avformat_find_stream_info(in_fmt, nullptr);

  // 找到音频流
  int audio_stream_index = -1;
  for (unsigned int i = 0; i < in_fmt->nb_streams; i++) {
    if (in_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_stream_index = i;
      break;
    }
  }
  if (audio_stream_index < 0) {
    std::cerr << "❌ No audio stream found\n";
    return -1;
  }

  // 获取音频参数
  AVStream *in_stream = in_fmt->streams[audio_stream_index];
  int sample_rate = in_stream->codecpar->sample_rate;
  int channels = in_stream->codecpar->channels;

  // ✅ 初始化 AudioAfade（前 200 帧淡入）
  AudioAfade afade(sample_rate, channels, AudioAfade::FADE_IN, 200);

  // 初始化输出封装
  AVFormatContext *out_fmt = nullptr;
  avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_file);
  if (!out_fmt) {
    std::cerr << "❌ Could not create output context\n";
    return -1;
  }

  // 新建音频流
  AVStream *out_stream = avformat_new_stream(out_fmt, nullptr);
  avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
  out_stream->codecpar->codec_tag = 0;

  // 打开输出文件
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE) < 0) {
      std::cerr << "❌ Could not open output file\n";
      return -1;
    }
  }

  avformat_write_header(out_fmt, nullptr);

  AVPacket pkt;
  av_init_packet(&pkt);

  int frame_count = 0;
  while (av_read_frame(in_fmt, &pkt) >= 0) {
    if (pkt.stream_index == audio_stream_index) {
      AVPacket out_pkt;
        av_init_packet(&out_pkt);

      if (afade.Process(&pkt, &out_pkt)) {
            // 成功处理后，写入输出文件
            av_interleaved_write_frame(out_fmt, &out_pkt);
        }

      frame_count++;
    }
    av_packet_unref(&pkt);
  }

  av_write_trailer(out_fmt);

  // 资源清理
  avformat_close_input(&in_fmt);
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  std::cout << "✅ 输出完成: " << output_file
            << "（已应用前 200 帧淡入效果）\n";
  return 0;
}
