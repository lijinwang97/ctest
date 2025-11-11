#include "audio_afade.h"
#include "logger.h"
#include <algorithm>
#include <iomanip> // std::hex, std::setw, std::setfill
#include <iostream>
#include <sstream> // std::ostringstream

AudioAfade::AudioAfade(int sample_rate, int channels, AVSampleFormat sample_fmt,
                       FadeType type, int total_frames)
    : sample_rate_(sample_rate), channels_(channels), sample_fmt_(sample_fmt),
      type_(type), total_frames_(total_frames) {

  LOG_INFO("AudioAfade Init sample_rate={}, channels={}, total_frames={} "
           "sample_fmt:{} type:{}",
           sample_rate, channels, total_frames,
           av_get_sample_fmt_name(sample_fmt), type);

  const AVCodec *dec = avcodec_find_decoder(AV_CODEC_ID_AAC);
  if (!dec) {
    LOG_ERROR("AudioAfade AAC decoder not found!");
    return;
  }

  dec_ctx_ = avcodec_alloc_context3(dec);
  dec_ctx_->sample_rate = sample_rate_;
  dec_ctx_->channels = channels_;
  dec_ctx_->channel_layout = av_get_default_channel_layout(channels_);
  if (avcodec_open2(dec_ctx_, dec, nullptr) < 0) {
    LOG_ERROR("AudioAfade Failed to open AAC decoder");
    return;
  }

  LOG_INFO("AudioAfade Decoder initialized Decoder info: sample_fmt: {} "
           "sample_rate: {} "
           "channels: {} (layout=0x{:x})",
           av_get_sample_fmt_name(dec_ctx_->sample_fmt), dec_ctx_->sample_rate,
           dec_ctx_->channels, dec_ctx_->channel_layout);

  // 初始化编码器
  const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
  enc_ctx_ = avcodec_alloc_context3(enc);
  enc_ctx_->sample_rate = sample_rate_;
  enc_ctx_->channels = channels_;
  enc_ctx_->channel_layout = av_get_default_channel_layout(channels_);
  enc_ctx_->bit_rate = 128000;
  enc_ctx_->sample_fmt = sample_fmt_;
  if (avcodec_open2(enc_ctx_, enc, nullptr) < 0) {
    LOG_ERROR("AudioAfade Failed to open MP3 encoder");
    return;
  }

  LOG_INFO("AudioAfade Encoder initialized Encoder info: sample_fmt: {} "
           "sample_rate: {} "
           "channels: {} (layout=0x{:x})",
           av_get_sample_fmt_name(enc_ctx_->sample_fmt), enc_ctx_->sample_rate,
           enc_ctx_->channels, enc_ctx_->channel_layout);

  if (dec_ctx_->sample_fmt != enc_ctx_->sample_fmt ||
      dec_ctx_->sample_rate != enc_ctx_->sample_rate ||
      dec_ctx_->channels != enc_ctx_->channels) {
    LOG_ERROR("AudioAfade Warning: decoder and encoder audio formats differ!");
  } else {
    LOG_INFO("AudioAfade Input/Output formats are perfectly matched!");
  }

  // 初始化滤镜
  InitFilterGraph();
}

AudioAfade::~AudioAfade() { Cleanup(); }

void AudioAfade::Cleanup() {
  if (filter_graph_) {
    if (src_ctx_) {
      avfilter_free(src_ctx_);
      src_ctx_ = nullptr;
    }
    if (sink_ctx_) {
      avfilter_free(sink_ctx_);
      sink_ctx_ = nullptr;
    }

    avfilter_graph_free(&filter_graph_);
    filter_graph_ = nullptr;
  }
  if (dec_ctx_) {
    avcodec_free_context(&dec_ctx_);
    dec_ctx_ = nullptr;
  }

  if (enc_ctx_) {
    avcodec_free_context(&enc_ctx_);
    enc_ctx_ = nullptr;
  }

  pts_counter_ = 0;
  total_frames_ = 0;
  sample_rate_ = 0;
  channels_ = 0;
  sample_fmt_ = AV_SAMPLE_FMT_NONE;
  type_ = FADE_NONE;
}

bool AudioAfade::InitFilterGraph() {
  char args[512];
  filter_graph_ = avfilter_graph_alloc();

  // ---- abuffer 输入 ----
  const AVFilter *abuffer = avfilter_get_by_name("abuffer");
  snprintf(
      args, sizeof(args),
      "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
      sample_rate_, sample_rate_, av_get_sample_fmt_name(sample_fmt_),
      (uint64_t)av_get_default_channel_layout(channels_));

  int ret = avfilter_graph_create_filter(&src_ctx_, abuffer, "in", args,
                                         nullptr, filter_graph_);
  if (ret < 0) {
    LOG_ERROR("InitFilterGraph Failed to create abuffer filter");
    return false;
  }
  LOG_INFO("InitFilterGraph abuffer args: {}", args);

  // ---- aformat 自动格式转换 ----
  const AVFilter *aformat = avfilter_get_by_name("aformat");
  AVFilterContext *aformat_ctx = nullptr;

  char aformat_args[128];
  snprintf(aformat_args, sizeof(aformat_args), "sample_fmts=%s",
           av_get_sample_fmt_name(sample_fmt_));

  ret = avfilter_graph_create_filter(&aformat_ctx, aformat, "aformat",
                                     aformat_args, nullptr, filter_graph_);
  if (ret < 0) {
    LOG_ERROR("InitFilterGraph Failed to create aformat filter");
    return false;
  }
  LOG_INFO("InitFilterGraph aformat args = {}", aformat_args);

  // ---- 3️ afade ----
  const AVFilter *afade = avfilter_get_by_name("afade");
  AVFilterContext *fade_ctx = nullptr;
  std::string fade_type = (type_ == FADE_IN) ? "in" : "out";
  double duration_sec = (double)total_frames_ * 1024 / sample_rate_;
  std::string fade_args =
      "t=" + fade_type + ":st=0:d=" + std::to_string(duration_sec);
  ret = avfilter_graph_create_filter(&fade_ctx, afade, "fade",
                                     fade_args.c_str(), nullptr, filter_graph_);
  if (ret < 0) {
    LOG_ERROR("InitFilterGraph Failed to create afade filter");
    return false;
  }
  LOG_INFO("InitFilterGraph afade args = {}", fade_args);

  // ---- 4️ abuffersink ----
  const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
  ret = avfilter_graph_create_filter(&sink_ctx_, abuffersink, "out", nullptr,
                                     nullptr, filter_graph_);
  if (ret < 0) {
    LOG_ERROR("InitFilterGraph Failed to create abuffersink filter");
    return false;
  }
  LOG_INFO("InitFilterGraph abuffersink created");

  // ---- 5️ 链接滤镜链 ----
  avfilter_link(src_ctx_, 0, aformat_ctx, 0);
  avfilter_link(aformat_ctx, 0, fade_ctx, 0);
  avfilter_link(fade_ctx, 0, sink_ctx_, 0);

  // ---- 6️ 配置滤镜图 ----
  ret = avfilter_graph_config(filter_graph_, nullptr);
  if (ret < 0) {
    LOG_ERROR("InitFilterGraph Failed to configure filter graph");
    return false;
  }
  LOG_INFO("InitFilterGraph Filter graph configured successfully");

  return true;
}

bool AudioAfade::Process(AVPacket *src_pkt, AVPacket *dst_pkt) {
  LOG_INFO("Process start src_pkt size={}, pts={}, dts={}", src_pkt->size,
           src_pkt->pts, src_pkt->dts);

  if (avcodec_send_packet(dec_ctx_, src_pkt) < 0) {
    LOG_ERROR("Process Failed to send packet to decoder");
    return false;
  }

  AVFrame *frame = av_frame_alloc();
  while (avcodec_receive_frame(dec_ctx_, frame) == 0) {
    LOG_INFO("Process Decoded frame: pts={}, nb_samples={}", frame->pts,
             frame->nb_samples);

    // 处理解码后的帧（淡入/淡出）
    frame->pts = pts_counter_;
    pts_counter_ += frame->nb_samples;

    SendToFilter(frame);

    // 清理上一次的数据
    av_packet_unref(dst_pkt);
    av_init_packet(dst_pkt);
    dst_pkt->data = nullptr;
    dst_pkt->size = 0;

    // 从滤镜获取数据
    ReceiveFromFilter(*dst_pkt); // 填充 dst_pkt
    LOG_INFO("Process end frame processed, dst_pkt size={} pts={}, dts={}",
             dst_pkt->size, dst_pkt->pts, dst_pkt->dts);

    av_frame_unref(frame);
  }

  av_frame_free(&frame);
  return true;
}

void AudioAfade::FlushEncoder(AVFormatContext *out_fmt, int64_t &next_pts) {
  LOG_INFO("Flushing AAC encoder...");
  int ret = avcodec_send_frame(enc_ctx_, nullptr); // 发送空帧触发 flush
  if (ret < 0) {
    char errbuf[128];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR("Failed to flush encoder: {}", errbuf);
    return;
  }

  AVPacket pkt;
  av_init_packet(&pkt);
  while (avcodec_receive_packet(enc_ctx_, &pkt) >= 0) {
    pkt.stream_index = 0;
    pkt.pts = pkt.dts = next_pts;
    next_pts += 1024;

    LOG_INFO("🎧 Write flush packet: size={}, pts={}, dts={}", pkt.size, pkt.pts,
             pkt.dts);
    av_interleaved_write_frame(out_fmt, &pkt);
    av_packet_unref(&pkt);
  }
}

bool AudioAfade::ProcessRaw(const char *in_buf, int in_len,
                            std::string &out_buf) {
  if (!in_buf || in_len <= 0) {
    LOG_ERROR("ProcessRaw invalid input");
    return false;
  }

  AVPacket src_pkt;
  av_init_packet(&src_pkt);
  src_pkt.data = reinterpret_cast<uint8_t *>(const_cast<char *>(in_buf));
  src_pkt.size = in_len;

  AVPacket dst_pkt;
  av_init_packet(&dst_pkt);

  bool ok = Process(&src_pkt, &dst_pkt);
  if (!ok || dst_pkt.size <= 0) {
    LOG_WARN("ProcessRaw no valid output from Process()");
    av_packet_unref(&dst_pkt);
    return false;
  }

  uint8_t adts_header[7];
  int profile = 2; // AAC LC
  WriteAdtsHeader(adts_header, dst_pkt.size, profile, sample_rate_, channels_);

  // 拼接 ADTS + AAC
  out_buf.resize(7 + dst_pkt.size);
  memcpy((void *)out_buf.data(), adts_header, 7);
  memcpy((void *)(out_buf.data() + 7), dst_pkt.data, dst_pkt.size);

  LOG_INFO("ProcessRaw success: input={} bytes -> output={} bytes Hex dump:{}",
           in_len, out_buf.size(), PrintHexPreview(out_buf, 64));

  av_packet_unref(&dst_pkt);
  return true;
}

bool AudioAfade::SendToFilter(AVFrame *frame) {
  LOG_INFO("SendToFilter... fmt={}, nb_samples={}, "
           "channels={}, sample_rate={}",
           av_get_sample_fmt_name((AVSampleFormat)frame->format),
           frame->nb_samples, frame->channels, frame->sample_rate);

  int ret = av_buffersrc_add_frame(src_ctx_, frame);
  if (ret < 0) {
    char errbuf[128];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR("SendToFilter Failed to send frame to filter ({}): {}", ret,
              errbuf);
    return false;
  }

  return true;
}

bool AudioAfade::ReceiveFromFilter(AVPacket &out_pkt) {
  AVFrame *faded_frame = av_frame_alloc();
  int total_frames = 0;
  int total_packets = 0;
  int ret = 0;

  av_packet_unref(&out_pkt);

  while ((ret = av_buffersink_get_frame(sink_ctx_, faded_frame)) >= 0) {
    int bytes_per_sample =
        av_get_bytes_per_sample((AVSampleFormat)faded_frame->format);
    int frame_bytes =
        faded_frame->nb_samples * faded_frame->channels * bytes_per_sample;
    LOG_INFO("ReceiveFromFilter Got faded frame from filter: nb_samples={}, "
             "format={}, channels={}, pts={} frame_size=:{}",
             faded_frame->nb_samples,
             av_get_sample_fmt_name((AVSampleFormat)faded_frame->format),
             faded_frame->channels, faded_frame->pts, frame_bytes);
    total_frames++;

    ret = avcodec_send_frame(enc_ctx_, faded_frame);
    if (ret < 0) {
      char errbuf[128];
      av_strerror(ret, errbuf, sizeof(errbuf));
      LOG_ERROR("ReceiveFromFilter Failed to send frame to encoder: {}",
                errbuf);
      av_frame_unref(faded_frame);
      continue;
    }

    while (true) {
      AVPacket tmp_pkt;
      av_init_packet(&tmp_pkt);
      ret = avcodec_receive_packet(enc_ctx_, &tmp_pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_packet_unref(&tmp_pkt);
        break;
      } else if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("ReceiveFromFilter avcodec_receive_packet returned error: {}",
                  errbuf);
        av_packet_unref(&tmp_pkt);
        break;
      }

      LOG_INFO("ReceiveFromFilter Encoded packet: size={}, pts={}, dts={}",
               tmp_pkt.size, tmp_pkt.pts, tmp_pkt.dts);

      LOG_INFO(
          "Encoded pkt: size={} stream_index={} codec={} keyframe={} flags={}",
          tmp_pkt.size, tmp_pkt.stream_index,
          avcodec_get_name(enc_ctx_->codec_id), tmp_pkt.flags & AV_PKT_FLAG_KEY,
          tmp_pkt.flags);

      av_packet_unref(&out_pkt);
      av_packet_move_ref(&out_pkt, &tmp_pkt);
      total_packets++;

      av_packet_unref(&tmp_pkt);
    }

    av_frame_unref(faded_frame);
  }

  // 滤镜结束（但编码器可能还有残留帧）
  if (ret == AVERROR_EOF) {
    LOG_INFO("ReceiveFromFilter Filter reached EOF, flushing encoder...");
    avcodec_send_frame(enc_ctx_, nullptr);

    AVPacket tmp_pkt;
    av_init_packet(&tmp_pkt);
    while (avcodec_receive_packet(enc_ctx_, &tmp_pkt) >= 0) {
      LOG_INFO(
          "ReceiveFromFilter Flushed encoded packet: size={}, pts={}, dts={}",
          tmp_pkt.size, tmp_pkt.pts, tmp_pkt.dts);
      av_packet_unref(&out_pkt);
      av_packet_move_ref(&out_pkt, &tmp_pkt);
      total_packets++;
      av_packet_unref(&tmp_pkt);
    }
  } else if (ret != AVERROR(EAGAIN) && ret < 0) {
    char errbuf[128];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR("ReceiveFromFilter Failed to get frame from filter: {}", errbuf);
  }

  av_frame_free(&faded_frame);
  LOG_INFO("Filter output done. Total frames={}, encoded packets=={}",
           total_frames, total_packets);
  return total_packets > 0;
}

void AudioAfade::PrintPacketHex(const AVPacket *pkt, int max_bytes) {
  int print_len = std::min(pkt->size, max_bytes);
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');

  for (int i = 0; i < print_len; i++) {
    oss << std::setw(2) << (int)pkt->data[i] << " ";
  }
  if (pkt->size > max_bytes)
    oss << "...";
  LOG_INFO("Encoded AAC packet (size={}): {}", pkt->size, oss.str());
}

void AudioAfade::WriteAdtsHeader(uint8_t *adts_header, int aac_length,
                                 int profile, int sample_rate, int channels) {
  // 采样率索引表（ISO 14496-3 Table 1.16）
  static const int freq_tbl[13] = {96000, 88200, 64000, 48000, 44100,
                                   32000, 24000, 22050, 16000, 12000,
                                   11025, 8000,  7350};
  int freq_idx = 4; // 默认44100Hz
  for (int i = 0; i < 13; i++) {
    if (freq_tbl[i] == sample_rate) {
      freq_idx = i;
      break;
    }
  }

  int frame_length = aac_length + 7;

  adts_header[0] = 0xFF;
  adts_header[1] = 0xF1; // 1111 1111 1111 0001 (MPEG-4, Layer=0)
  adts_header[2] = ((profile - 1) << 6) | (freq_idx << 2) | (channels >> 2);
  adts_header[3] = ((channels & 3) << 6) | ((frame_length >> 11) & 0x03);
  adts_header[4] = (frame_length >> 3) & 0xFF;
  adts_header[5] = ((frame_length & 7) << 5) | 0x1F;
  adts_header[6] = 0xFC;
}

std::string PrintHexPreview(const std::string &buf,
                                        size_t max_bytes) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');

  size_t print_len = std::min(buf.size(), max_bytes);
  for (size_t i = 0; i < print_len; ++i) {
    // 每个字节转成两位十六进制
    oss << std::setw(2)
        << static_cast<unsigned>(static_cast<unsigned char>(buf[i])) << " ";
  }

  if (buf.size() > max_bytes)
    oss << "...";

  return oss.str();
}