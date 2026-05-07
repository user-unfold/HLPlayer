#include <benchmark/benchmark.h>
#include <hlplayer/HWVideoEncoder.h>
#include <hlplayer/HWEncoderDetector.h>
#include <hlplayer/RecordingFrameQueue.h>
#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace hlplayer {
namespace bench {

static AVFrame* makeYuvFrame(int w, int h, AVPixelFormat fmt) {
    AVFrame* f = av_frame_alloc();
    f->format = fmt;
    f->width  = w;
    f->height = h;
    av_frame_get_buffer(f, 0);
    return f;
}

static AVFrame* makeYuyvFrame(int w, int h) {
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUYV422;
    f->width  = w;
    f->height = h;
    av_frame_get_buffer(f, 0);
    return f;
}

static void freeFrame(AVFrame* f) { av_frame_free(&f); }

// ── Benchmark 1: NVENC single-frame encode latency ────────────────────────
static void BM_NvencEncodeSingle(benchmark::State& state) {
    EncoderInfo info = HWEncoderDetector::detectBest();
    if (!info.available || info.type != HWEncoderType::NVENC) {
        state.SkipWithError("NVENC not available");
        return;
    }

    HWEncodeConfig cfg;
    cfg.width = 1280; cfg.height = 720; cfg.fps = 30;
    cfg.bitrate = 2000000; cfg.gopSize = 60; cfg.maxBFrames = 0;
    cfg.encoderInfo = info;

    HWVideoEncoder enc;
    if (enc.init(cfg).hasError()) { state.SkipWithError("encoder init failed"); return; }

    AVFrame* frame = makeYuvFrame(1280, 720, AV_PIX_FMT_YUV420P);
    frame->pts = 0;

    for (auto _ : state) {
        auto result = enc.encode(frame);
        if (result.hasError()) { state.SkipWithError("encode failed"); break; }
        // Consume output
        for (auto& pkt : result.value()) {
            benchmark::DoNotOptimize(pkt.data.size());
        }
    }

    enc.flush();
    freeFrame(frame);
}
BENCHMARK(BM_NvencEncodeSingle)->Unit(benchmark::kMicrosecond);

// ── Benchmark 2: sws_scale YUYV422→YUV420P 720p conversion ─────────────────
static void BM_SwsYuyvToYuv720p(benchmark::State& state) {
    AVFrame* src = makeYuyvFrame(1280, 720);
    AVFrame* dst = makeYuvFrame(1280, 720, AV_PIX_FMT_YUV420P);

    SwsContext* ctx = sws_getContext(1280, 720, AV_PIX_FMT_YUYV422,
                                      1280, 720, AV_PIX_FMT_YUV420P,
                                      SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!ctx) { state.SkipWithError("sws_getContext failed"); return; }

    for (auto _ : state) {
        sws_scale(ctx, src->data, src->linesize, 0, 720, dst->data, dst->linesize);
    }

    sws_freeContext(ctx);
    freeFrame(src); freeFrame(dst);
}
BENCHMARK(BM_SwsYuyvToYuv720p)->Unit(benchmark::kMicrosecond);

// ── Benchmark 3: sws_scale SWS_BILINEAR vs SWS_FAST_BILINEAR ──────────────
static void BM_SwsScaleCompare(benchmark::State& state) {
    int flag = static_cast<int>(state.range(0)); // 0=FAST_BILINEAR, 1=BILINEAR
    AVFrame* src = makeYuyvFrame(1280, 720);
    AVFrame* dst = makeYuvFrame(1280, 720, AV_PIX_FMT_YUV420P);

    SwsContext* ctx = sws_getContext(1280, 720, AV_PIX_FMT_YUYV422,
                                      1280, 720, AV_PIX_FMT_YUV420P,
                                      flag, nullptr, nullptr, nullptr);
    if (!ctx) { state.SkipWithError("sws_getContext failed"); return; }

    for (auto _ : state) {
        sws_scale(ctx, src->data, src->linesize, 0, 720, dst->data, dst->linesize);
    }

    sws_freeContext(ctx);
    freeFrame(src); freeFrame(dst);
    state.SetLabel(flag == 0 ? "SWS_FAST_BILINEAR" : "SWS_BILINEAR");
}
BENCHMARK(BM_SwsScaleCompare)->Unit(benchmark::kMicrosecond)
    ->Arg(0)->Arg(1);

// ── Benchmark 4: End-to-end pipeline (YUYV→YUV→encode) throughput ────────
static void BM_PipelineThroughput(benchmark::State& state) {
    EncoderInfo info = HWEncoderDetector::detectBest();
    if (!info.available || info.type != HWEncoderType::NVENC) {
        state.SkipWithError("NVENC not available");
        return;
    }

    HWEncodeConfig cfg;
    cfg.width = 1280; cfg.height = 720; cfg.fps = 30;
    cfg.bitrate = 2000000; cfg.gopSize = 60; cfg.maxBFrames = 0;
    cfg.encoderInfo = info;

    HWVideoEncoder enc;
    if (enc.init(cfg).hasError()) { state.SkipWithError("encoder init failed"); return; }

    AVFrame* src = makeYuyvFrame(1280, 720);
    AVFrame* dst = makeYuvFrame(1280, 720, AV_PIX_FMT_YUV420P);
    SwsContext* sws = sws_getContext(1280, 720, AV_PIX_FMT_YUYV422,
                                      1280, 720, AV_PIX_FMT_YUV420P,
                                      SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    int pts = 0;
    int frameCount = 0;
    for (auto _ : state) {
        // Step 1: color convert
        sws_scale(sws, src->data, src->linesize, 0, 720, dst->data, dst->linesize);
        dst->pts = pts++;
        // Step 2: encode
        auto result = enc.encode(dst);
        if (result.hasError()) { state.SkipWithError("encode failed"); break; }
        for (auto& pkt : result.value()) {
            benchmark::DoNotOptimize(pkt.data.size());
        }
        ++frameCount;
    }
    state.SetItemsProcessed(frameCount);

    enc.flush();
    sws_freeContext(sws);
    freeFrame(src); freeFrame(dst);
}
BENCHMARK(BM_PipelineThroughput)->Unit(benchmark::kMicrosecond);

// ── Benchmark 5: FrameQueue push/pop cycle ────────────────────────────────
static void BM_FrameQueueCycle(benchmark::State& state) {
    RecordingFrameQueue q;
    AVFrame* f = makeYuvFrame(1280, 720, AV_PIX_FMT_YUV420P);

    for (auto _ : state) {
        f->pts = 0;
        if (!q.push(f)) { state.SkipWithError("push failed"); break; }
        AVFrame* out = q.pop();
        if (!out) { state.SkipWithError("pop failed"); break; }
        av_frame_free(&out);
    }

    freeFrame(f);
    q.shutdown();
}
BENCHMARK(BM_FrameQueueCycle)->Unit(benchmark::kMicrosecond);

// ── Benchmark 6: NVENC encode 30-frame batch throughput ───────────────────
static void BM_NvencBatch30(benchmark::State& state) {
    EncoderInfo info = HWEncoderDetector::detectBest();
    if (!info.available || info.type != HWEncoderType::NVENC) {
        state.SkipWithError("NVENC not available");
        return;
    }

    HWEncodeConfig cfg;
    cfg.width = 1280; cfg.height = 720; cfg.fps = 30;
    cfg.bitrate = 2000000; cfg.gopSize = 60; cfg.maxBFrames = 0;
    cfg.encoderInfo = info;

    HWVideoEncoder enc;
    if (enc.init(cfg).hasError()) { state.SkipWithError("encoder init failed"); return; }

    AVFrame* frame = makeYuvFrame(1280, 720, AV_PIX_FMT_YUV420P);

    for (auto _ : state) {
        for (int i = 0; i < 30; ++i) {
            frame->pts = i;
            auto result = enc.encode(frame);
            if (result.hasError()) { state.SkipWithError("encode failed"); break; }
        }
    }
    state.SetItemsProcessed(state.iterations() * 30);

    enc.flush();
    freeFrame(frame);
}
BENCHMARK(BM_NvencBatch30)->Unit(benchmark::kMillisecond);

} // namespace bench
} // namespace hlplayer
