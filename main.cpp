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
#include "logger.h"

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

int testAfade() {}

inline std::string packet_to_string(const AVPacket *pkt) {
  return std::string(reinterpret_cast<const char *>(pkt->data), pkt->size);
}

int initLog() {
  if (!LOGGER_INS->Init("info", "./log", 0, true, true)) {
    return -1;
  }
}

int main() {
  initLog();
  av_log_set_level(AV_LOG_ERROR);

  const char *input_file = "/data1/lijinwang/ctest/build/input.aac";
  // const char *input_file = "/data1/lijinwang/ctest/build/input2.mp3";
  const char *output_file = "output_my.aac";
  const char *fade_output_file = "fade_output.aac";

  // 打开输入文件
  AVFormatContext *in_fmt = nullptr;
  if (avformat_open_input(&in_fmt, input_file, nullptr, nullptr) < 0) {
    LOG_ERROR("❌ Failed to open input file: {}", input_file);
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
    LOG_ERROR("❌ No audio stream found in file: {}", input_file);
    return -1;
  }

  // 获取音频参数
  AVStream *in_stream = in_fmt->streams[audio_stream_index];
  int sample_rate = in_stream->codecpar->sample_rate;
  int channels = in_stream->codecpar->channels;
  AVSampleFormat sample_fmt = (AVSampleFormat)in_stream->codecpar->format;

  LOG_INFO("Input stream: sample_rate={}, channels={}, format=", sample_rate,
           channels, av_get_sample_fmt_name(sample_fmt));

  // ✅ 初始化 AudioAfade（前 200 帧淡入）
  // AudioAfade afade(sample_rate, channels, AudioAfade::FADE_IN, 200);

  // 初始化输出封装
  AVFormatContext *out_fmt = nullptr;
  avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_file);
  if (!out_fmt) {
    LOG_ERROR("❌ Could not create output context");
    return -1;
  }

  // 新建音频流
  AVStream *out_stream = avformat_new_stream(out_fmt, nullptr);
  avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
  out_stream->codecpar->codec_tag = 0;

  // 打开输出文件
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE) < 0) {
      LOG_ERROR("❌ Could not open output file: {}", output_file);
      return -1;
    }
  }

  avformat_write_header(out_fmt, nullptr);

  // ---- 初始化淡入输出文件 ----
  AVFormatContext *fade_out_fmt = nullptr;
  avformat_alloc_output_context2(&fade_out_fmt, nullptr, nullptr,
                                 fade_output_file);
  if (!fade_out_fmt) {
    LOG_ERROR("❌ Could not create fade output context for {}",
              fade_output_file);
    return -1;
  }

  AVStream *fade_out_stream = avformat_new_stream(fade_out_fmt, nullptr);
  avcodec_parameters_copy(fade_out_stream->codecpar, in_stream->codecpar);
  fade_out_stream->codecpar->codec_tag = 0;

  if (!(fade_out_fmt->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&fade_out_fmt->pb, fade_output_file, AVIO_FLAG_WRITE) < 0) {
      LOG_ERROR("❌ Could not open fade output file: {}", fade_output_file);
      return -1;
    }
  }
  avformat_write_header(fade_out_fmt, nullptr);

  int frame_count = 0;
  bool fading = false;
  std::unique_ptr<AudioAfade> afade;

  AVPacket pkt;
  av_init_packet(&pkt);

  while (av_read_frame(in_fmt, &pkt) >= 0) {
    if (pkt.stream_index != audio_stream_index) {
      av_packet_unref(&pkt);
      continue;
    }

    frame_count++;
    if (frame_count == 100) {
      LOG_INFO("🎬 Fade-in triggered at frame {}", frame_count);
      afade = std::make_unique<AudioAfade>(sample_rate, channels, sample_fmt,
                                           AudioAfade::FADE_IN, 200);
      fading = true;
    }

    // if (fading && afade) {
    //   AVPacket faded_pkt;
    //   av_init_packet(&faded_pkt);

    //   LOG_INFO("🎧 Write before packet: size={}, pts={}, dts={}",
    //              pkt.size, pkt.pts, pkt.dts);

    //   if (afade->Process(&pkt, &faded_pkt) && faded_pkt.size > 0) {
    //     faded_pkt.stream_index = 0;

    //     LOG_INFO("🎧 Write faded packet: size={}, pts={}, dts={}",
    //              faded_pkt.size, faded_pkt.pts, faded_pkt.dts);

    //     int ret = av_interleaved_write_frame(fade_out_fmt, &faded_pkt);
    //     if (ret < 0) {
    //       char errbuf[128];
    //       av_strerror(ret, errbuf, sizeof(errbuf));
    //       LOG_ERROR(" Write faded packet failed: {}", errbuf);
    //     }

    //     av_packet_unref(&faded_pkt);
    //   }

    //   // 当淡入200帧后销毁
    //   if (frame_count >= 300) {
    //     LOG_INFO(" Fade-in finished at frame {}", frame_count);
    //     fading = false;
    //     afade.reset();
    //   }
    // }

    if (fading && afade) {
      LOG_INFO("🎧 Write before packet: size={}, pts={}, dts={}", pkt.size,
               pkt.pts, pkt.dts);

      // 1️⃣ 输出缓冲区（ProcessRaw 输出的数据）
      std::string out_buf;

      // 2️⃣ 调用 ProcessRaw —— 输入原始音频字节流
      if (afade->ProcessRaw(reinterpret_cast<const char *>(pkt.data), pkt.size,
                            out_buf) &&
          !out_buf.empty()) {

        // 3️⃣ 把 out_buf 封装回 AVPacket
        AVPacket faded_pkt;
        av_init_packet(&faded_pkt);

        faded_pkt.data =
            reinterpret_cast<uint8_t *>(const_cast<char *>(out_buf.data()));
        faded_pkt.size = static_cast<int>(out_buf.size());
        faded_pkt.stream_index = 0;

        LOG_INFO("🎧 Write faded packet: size={}, pts={}, dts={}",
                 faded_pkt.size, faded_pkt.pts, faded_pkt.dts);

        // 4️⃣ 写入淡入输出文件
        int ret = av_interleaved_write_frame(fade_out_fmt, &faded_pkt);
        if (ret < 0) {
          char errbuf[128];
          av_strerror(ret, errbuf, sizeof(errbuf));
          LOG_ERROR("❌ Write faded packet failed: {}", errbuf);
        }

        av_packet_unref(&faded_pkt);
      }

      // 5️⃣ 超过 200 帧后结束淡入
      if (frame_count >= 300) {
        LOG_INFO("✅ Fade-in finished at frame {}", frame_count);
        fading = false;
        afade.reset();
      }
    } else {
      pkt.stream_index = 0;
      LOG_INFO("🎧 Write original packet: size={}, pts={}, dts={}", pkt.size,
               pkt.pts, pkt.dts);

      av_interleaved_write_frame(out_fmt, &pkt);
    }

    av_packet_unref(&pkt);
  }

  av_write_trailer(out_fmt);
  av_write_trailer(fade_out_fmt);

  // 资源清理
  avformat_close_input(&in_fmt);
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_fmt->pb);

  if (!(fade_out_fmt->oformat->flags & AVFMT_NOFILE))
    avio_closep(&fade_out_fmt->pb);
  avformat_free_context(out_fmt);
  avformat_free_context(fade_out_fmt);

  LOG_INFO("✅ 输出完成: {}（已应用前 200 帧淡入效果）", output_file);
  LOG_INFO("✅ 淡入输出完成: {}", fade_output_file);
  return 0;
}
