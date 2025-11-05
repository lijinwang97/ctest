#include "audio_afade.h"
#include <algorithm>
#include <iostream>

AudioAfade::AudioAfade(int sample_rate, int channels, AVSampleFormat sample_fmt,
                       FadeType type, int total_frames)
    : sample_rate_(sample_rate), channels_(channels), sample_fmt_(sample_fmt),
      type_(type), total_frames_(total_frames) {

  std::cout << "🎬 [AudioAfade Init] sample_rate=" << sample_rate
            << ", channels=" << channels << ", total_frames=" << total_frames
            << std::endl;

  // ==== 初始化解码器 ====
  const AVCodec *dec = avcodec_find_decoder(AV_CODEC_ID_MP3);
  dec_ctx_ = avcodec_alloc_context3(dec);
  avcodec_open2(dec_ctx_, dec, nullptr);

  std::cout << "✅ MP3 decoder initialized" << std::endl;
  std::cout << "   🔹 Decoder info:" << std::endl;
  std::cout << "      sample_fmt: "
            << av_get_sample_fmt_name(dec_ctx_->sample_fmt) << std::endl;
  std::cout << "      sample_rate: " << dec_ctx_->sample_rate << std::endl;
  std::cout << "      channels: " << dec_ctx_->channels << " (layout=0x"
            << std::hex << dec_ctx_->channel_layout << std::dec << ")"
            << std::endl;

  // ==== 初始化编码器 ====
  const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_MP3);
  enc_ctx_ = avcodec_alloc_context3(enc);
  enc_ctx_->sample_rate = sample_rate_;
  enc_ctx_->channels = channels_;
  enc_ctx_->channel_layout = av_get_default_channel_layout(channels_);
  enc_ctx_->bit_rate = 128000;
  enc_ctx_->sample_fmt = sample_fmt_;
  if (avcodec_open2(enc_ctx_, enc, nullptr) < 0) {
    std::cerr << "❌ Failed to open MP3 encoder" << std::endl;
    return;
  }

  std::cout << "✅ MP3 encoder initialized" << std::endl;
  std::cout << "   🔹 Encoder info:" << std::endl;
  std::cout << "      sample_fmt: "
            << av_get_sample_fmt_name(enc_ctx_->sample_fmt) << std::endl;
  std::cout << "      sample_rate: " << enc_ctx_->sample_rate << std::endl;
  std::cout << "      channels: " << enc_ctx_->channels << " (layout=0x"
            << std::hex << enc_ctx_->channel_layout << std::dec << ")"
            << std::endl;

  if (dec_ctx_->sample_fmt != enc_ctx_->sample_fmt ||
      dec_ctx_->sample_rate != enc_ctx_->sample_rate ||
      dec_ctx_->channels != enc_ctx_->channels) {
    std::cerr << "⚠️  Warning: decoder and encoder audio formats differ!"
              << std::endl;
  } else {
    std::cout << "✅ Input/Output formats are perfectly matched!" << std::endl;
  }

  // ==== 初始化滤镜 ====
  InitFilterGraph();
}

AudioAfade::~AudioAfade() { Cleanup(); }

void AudioAfade::Cleanup() {
  if (filter_graph_)
    avfilter_graph_free(&filter_graph_);
  if (dec_ctx_)
    avcodec_free_context(&dec_ctx_);
  if (enc_ctx_)
    avcodec_free_context(&enc_ctx_);
}

bool AudioAfade::InitFilterGraph() {
  char args[512];
  filter_graph_ = avfilter_graph_alloc();

  // ---- 1️⃣ abuffer 输入 ----
  const AVFilter *abuffer = avfilter_get_by_name("abuffer");
  snprintf(
      args, sizeof(args),
      "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
      sample_rate_, sample_rate_, av_get_sample_fmt_name(sample_fmt_),
      (uint64_t)av_get_default_channel_layout(channels_));

  int ret = avfilter_graph_create_filter(&src_ctx_, abuffer, "in", args,
                                         nullptr, filter_graph_);
  if (ret < 0) {
    std::cerr << "❌ Failed to create abuffer filter" << std::endl;
    return false;
  }
  std::cout << "🎬 abuffer args: " << args << std::endl;

  // ---- 2️⃣ aformat 自动格式转换 ----
  const AVFilter *aformat = avfilter_get_by_name("aformat");
  AVFilterContext *aformat_ctx = nullptr;

  char aformat_args[128];
  snprintf(aformat_args, sizeof(aformat_args), "sample_fmts=%s",
           av_get_sample_fmt_name(sample_fmt_));

  ret = avfilter_graph_create_filter(&aformat_ctx, aformat, "aformat",
                                     aformat_args, nullptr, filter_graph_);
  if (ret < 0) {
    std::cerr << "❌ Failed to create aformat filter" << std::endl;
    return false;
  }
  std::cout << "🎬 aformat args = " << aformat_args << std::endl;

  // ---- 3️⃣ afade ----
  const AVFilter *afade = avfilter_get_by_name("afade");
  AVFilterContext *fade_ctx = nullptr;
  std::string fade_type = (type_ == FADE_IN) ? "in" : "out";
  double duration_sec = (double)total_frames_ * 1024 / sample_rate_;
  std::string fade_args =
      "t=" + fade_type + ":st=0:d=" + std::to_string(duration_sec);
  ret = avfilter_graph_create_filter(&fade_ctx, afade, "fade",
                                     fade_args.c_str(), nullptr, filter_graph_);
  if (ret < 0) {
    std::cerr << "❌ Failed to create afade filter" << std::endl;
    return false;
  }
  std::cout << "🎬 afade args = " << fade_args << std::endl;

  // ---- 4️⃣ abuffersink ----
  const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
  ret = avfilter_graph_create_filter(&sink_ctx_, abuffersink, "out", nullptr,
                                     nullptr, filter_graph_);
  if (ret < 0) {
    std::cerr << "❌ Failed to create abuffersink filter" << std::endl;
    return false;
  }
  std::cout << "✅ abuffersink created" << std::endl;

  // ---- 5️⃣ 链接滤镜链 ----
  avfilter_link(src_ctx_, 0, aformat_ctx, 0);
  avfilter_link(aformat_ctx, 0, fade_ctx, 0);
  avfilter_link(fade_ctx, 0, sink_ctx_, 0);

  // ---- 6️⃣ 配置滤镜图 ----
  ret = avfilter_graph_config(filter_graph_, nullptr);
  if (ret < 0) {
    std::cerr << "❌ Failed to configure filter graph" << std::endl;
    return false;
  }
  std::cout << "✅ Filter graph configured successfully" << std::endl;

  return true;
}

bool AudioAfade::Process(AVPacket *src_pkt, AVPacket *dst_pkt) {
  if (avcodec_send_packet(dec_ctx_, src_pkt) < 0) {
    std::cerr << "❌ Failed to send packet to decoder" << std::endl;
    return false;
  }

  AVFrame *frame = av_frame_alloc();
  while (avcodec_receive_frame(dec_ctx_, frame) == 0) {
    std::cout << "🔑 Decoded frame: pts=" << frame->pts
              << ", nb_samples=" << frame->nb_samples << std::endl;

    // 处理解码后的帧（淡入/淡出）
    frame->pts = pts_counter_;
    pts_counter_ += frame->nb_samples;

    SendToFilter(frame);

    // 从滤镜获取数据
    std::cout << "⚙️ Receiving frame from filter..." << std::endl;
    ReceiveFromFilter(*dst_pkt); // 填充 dst_pkt
    std::cout << "🔑 Frame processed, dst_pkt size=" << dst_pkt->size
              << std::endl;

    av_frame_unref(frame);
  }

  av_frame_free(&frame);
  return true;
}

bool AudioAfade::SendToFilter(AVFrame *frame) {
  std::cout << "⚙️ Sending frame to filter... "
            << "fmt=" << av_get_sample_fmt_name((AVSampleFormat)frame->format)
            << " nb_samples=" << frame->nb_samples
            << " channels=" << frame->channels
            << " sample_rate=" << frame->sample_rate << std::endl;

  int ret = av_buffersrc_add_frame(src_ctx_, frame);
  if (ret < 0) {
    char errbuf[128];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "❌ Failed to send frame to filter (" << ret << "): " << errbuf
              << std::endl;
    return false;
  }

  return true;
}

bool AudioAfade::ReceiveFromFilter(AVPacket &out_pkt) {
  AVFrame *faded_frame = av_frame_alloc();
  int total_frames = 0;
  int total_packets = 0;
  int ret = 0;

  while ((ret = av_buffersink_get_frame(sink_ctx_, faded_frame)) >= 0) {
    std::cout << "🎵 Got faded frame from filter: "
              << "nb_samples=" << faded_frame->nb_samples << ", format="
              << av_get_sample_fmt_name((AVSampleFormat)faded_frame->format)
              << ", channels=" << faded_frame->channels
              << ", pts=" << faded_frame->pts << std::endl;

    total_frames++;

    // 编码为 AAC（或 MP3）
    ret = avcodec_send_frame(enc_ctx_, faded_frame);
    if (ret < 0) {
      char errbuf[128];
      av_strerror(ret, errbuf, sizeof(errbuf));
      std::cerr << "❌ Failed to send frame to encoder: " << errbuf << std::endl;
      av_frame_unref(faded_frame);
      continue;
    }

    AVPacket tmp_pkt;
    av_init_packet(&tmp_pkt);

    while ((ret = avcodec_receive_packet(enc_ctx_, &tmp_pkt)) >= 0) {
      std::cout << "📦 Encoded packet: size=" << tmp_pkt.size
                << ", pts=" << tmp_pkt.pts << ", dts=" << tmp_pkt.dts
                << std::endl;

      // ✅ 拷贝输出数据
      out_pkt.data = (uint8_t *)av_malloc(tmp_pkt.size);
      memcpy(out_pkt.data, tmp_pkt.data, tmp_pkt.size);
      out_pkt.size = tmp_pkt.size;

      total_packets++;

      av_packet_unref(&tmp_pkt);
    }

    if (ret != AVERROR(EAGAIN) && ret < 0) {
      char errbuf[128];
      av_strerror(ret, errbuf, sizeof(errbuf));
      std::cerr << "⚠️ avcodec_receive_packet returned error: " << errbuf
                << std::endl;
    }

    av_frame_unref(faded_frame);
  }

  if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN) && ret < 0) {
    char errbuf[128];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "❌ Failed to get frame from filter: " << errbuf << std::endl;
  }

  av_frame_free(&faded_frame);

  std::cout << "✅ Filter output done. "
            << "Total frames=" << total_frames
            << ", encoded packets=" << total_packets << std::endl;

  return total_packets > 0;
}
