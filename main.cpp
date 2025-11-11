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
  const char *output_file = "output_my1.aac";

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
  avformat_alloc_output_context2(&out_fmt, nullptr, "adts", output_file);
  if (!out_fmt) {
    LOG_ERROR("❌ Could not create output context");
    return -1;
  }

  // 新建音频流
  AVStream *out_stream = avformat_new_stream(out_fmt, nullptr);
  // avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
  // out_stream->codecpar->codec_tag = 0;

  out_stream->codecpar->codec_id = AV_CODEC_ID_AAC;
  out_stream->codecpar->sample_rate = sample_rate;
  out_stream->codecpar->channels = channels;
  out_stream->codecpar->channel_layout =
      av_get_default_channel_layout(channels);
  out_stream->codecpar->format = AV_SAMPLE_FMT_FLTP;
  out_stream->codecpar->bit_rate = 128000;

  // 打开输出文件
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE) < 0) {
      LOG_ERROR("❌ Could not open output file: {}", output_file);
      return -1;
    }
  }

  avformat_write_header(out_fmt, nullptr);

  int frame_count = 0;
  int test_frame_count = 0;
  bool fading = false;
  std::unique_ptr<AudioAfade> afade;

  AVPacket pkt;
  av_init_packet(&pkt);

  int64_t next_pts = 0;               // 以采样点为单位
  const int samples_per_frame = 1024; // AAC 每帧固定 1024 采样点

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

    if (fading && afade) {
      test_frame_count++;
      AVPacket faded_pkt;
      av_init_packet(&faded_pkt);

      LOG_INFO("🎧 Write before packet: size={}, pts={}, dts={}", pkt.size,
               pkt.pts, pkt.dts);

      if (afade->Process(&pkt, &faded_pkt) && faded_pkt.size > 0) {
        faded_pkt.stream_index = 0;
        faded_pkt.pts = next_pts;
        faded_pkt.dts = next_pts;
        next_pts += samples_per_frame;

        LOG_INFO("🎧 Write faded packet: size={}, pts={}, dts={}",
                 faded_pkt.size, faded_pkt.pts, faded_pkt.dts);

        afade->PrintPacketHex(&faded_pkt);

        uint8_t adts_header[7];
        afade->WriteAdtsHeader(adts_header, faded_pkt.size, 2, sample_rate,
                               channels);
        // 拼接ADTS头 + AAC帧
        int total_size = faded_pkt.size + 7;
        std::vector<uint8_t> full_buf(total_size);
        memcpy(full_buf.data(), adts_header, 7);
        memcpy(full_buf.data() + 7, faded_pkt.data, faded_pkt.size);

        // 构造新的 AVPacket 用于写入
        AVPacket out_pkt;
        av_init_packet(&out_pkt);
        out_pkt.data = full_buf.data();
        out_pkt.size = total_size;
        out_pkt.pts = faded_pkt.pts;
        out_pkt.dts = faded_pkt.dts;
        out_pkt.stream_index = faded_pkt.stream_index;

        afade->PrintPacketHex(&out_pkt);

        int ret = av_interleaved_write_frame(out_fmt, &out_pkt);
        if (ret < 0) {
          char errbuf[128];
          av_strerror(ret, errbuf, sizeof(errbuf));
          LOG_ERROR("Write faded packet failed: {}", errbuf);
        } else {
          LOG_INFO("Wrote ADTS AAC frame ({} bytes) pts={}, dts={}",
                   out_pkt.size, out_pkt.pts, out_pkt.dts);
        }

        av_packet_unref(&faded_pkt);
      }

      // 当淡入200帧后销毁
      if (test_frame_count >= 200) {
        LOG_INFO(" Fade-in finished at frame {}", frame_count);
        fading = false;
        // afade.reset();
      }
    } else {
      pkt.stream_index = 0;
      pkt.pts = next_pts;
      pkt.dts = next_pts;
      next_pts += samples_per_frame;

      LOG_INFO("🎧 Write original packet: size={}, pts={}, dts={}", pkt.size,
               pkt.pts, pkt.dts);

      afade->PrintPacketHex(&pkt);
      int ret = av_interleaved_write_frame(out_fmt, &pkt);
      if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR(" Write common packet failed: {}", errbuf);
      }
    }

    av_packet_unref(&pkt);
  }

  if (afade) {
    afade->FlushEncoder(out_fmt, next_pts);
    afade.reset();
  }

  av_write_trailer(out_fmt);

  // 资源清理
  avformat_close_input(&in_fmt);
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_fmt->pb);

  avformat_free_context(out_fmt);

  LOG_INFO("✅ 输出完成: {}（已应用前 200 帧淡入效果）", output_file);
  return 0;
}
