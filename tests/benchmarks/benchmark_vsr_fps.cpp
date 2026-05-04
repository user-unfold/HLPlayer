#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "net.h"
#include "mat.h"
#include "gpu.h"

namespace fs = std::filesystem;

struct Resolution {
    const char* label;
    uint32_t w;
    uint32_t h;
};

static const Resolution kResolutions[] = {
    {"720p",         1280,  720},
    {"1080p",        1920, 1080},
    {"1440p_2K",    2560, 1440},
};

struct ModelConfig {
    const char* name;
    int         scale;
};

static const ModelConfig kModels[] = {
    {"AnimeVideoV3-2x", 2},
    {"AnimeVideoV3-3x", 3},
    {"AnimeVideoV3-4x", 4},
};

static constexpr int kWarmupFrames = 10;
static constexpr int kBenchFrames  = 100;

static std::string findModelsDir() {
    const char* env = std::getenv("HLPLAYER_MODELS_DIR");
    if (env) return env;
    for (const char* c : {"models", "../models", "../../models"})
        if (fs::exists(c)) return c;
    return "D:/HLPlayer/models";
}

static std::vector<uint8_t> makeFrame(int seed, uint32_t w, uint32_t h) {
    std::vector<uint8_t> data(static_cast<size_t>(w) * h * 4);
    std::mt19937 rng(static_cast<unsigned>(seed + 1));
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < data.size(); i += 4) {
        data[i + 0] = static_cast<uint8_t>(dist(rng));
        data[i + 1] = static_cast<uint8_t>(dist(rng));
        data[i + 2] = static_cast<uint8_t>(dist(rng));
        data[i + 3] = 255;
    }
    return data;
}

struct FrameTiming {
    double uploadConvMs;
    double gpuInferMs;
    double downloadMs;
    double totalMs;
};

static FrameTiming runOneFrame(ncnn::Net* net,
                                const std::vector<uint8_t>& rgba,
                                uint32_t w, uint32_t h) {
    size_t px = static_cast<size_t>(w) * h;

    auto t0 = std::chrono::high_resolution_clock::now();

    ncnn::Mat inputMat(static_cast<int>(w), static_cast<int>(h), 3);
    float* r = inputMat.channel(0);
    float* g = inputMat.channel(1);
    float* b = inputMat.channel(2);
    for (size_t i = 0; i < px; ++i) {
        r[i] = rgba[i * 4 + 0] / 255.0f;
        g[i] = rgba[i * 4 + 1] / 255.0f;
        b[i] = rgba[i * 4 + 2] / 255.0f;
    }

    auto t1 = std::chrono::high_resolution_clock::now();

    ncnn::Extractor ex = net->create_extractor();
    ex.set_light_mode(true);
    ex.input("data", inputMat);
    ncnn::Mat output;
    ex.extract("output", output);

    auto t2 = std::chrono::high_resolution_clock::now();

    volatile float forceSync = output.channel(0)[0];
    (void)forceSync;

    auto t3 = std::chrono::high_resolution_clock::now();

    FrameTiming ft;
    ft.uploadConvMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ft.gpuInferMs   = std::chrono::duration<double, std::milli>(t2 - t1).count();
    ft.downloadMs   = std::chrono::duration<double, std::milli>(t3 - t2).count();
    ft.totalMs      = std::chrono::duration<double, std::milli>(t3 - t0).count();
    return ft;
}

static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(v.size() * p / 100.0);
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

static void runModelBench(const ModelConfig& model, const Resolution& res) {
    char path[512];
    snprintf(path, sizeof(path), "%s/realesr-animevideov3-x%d",
             findModelsDir().c_str(), model.scale);

    ncnn::Net net;
    net.opt.use_vulkan_compute = true;
    net.opt.num_threads = 4;
    net.set_vulkan_device(ncnn::get_default_gpu_index());

    if (net.load_param((std::string(path) + ".param").c_str()) != 0 ||
        net.load_model((std::string(path) + ".bin").c_str()) != 0) {
        fprintf(stderr, "  SKIP: model not found at %s\n", path);
        return;
    }

    std::vector<std::vector<uint8_t>> frames(kWarmupFrames + kBenchFrames);
    for (int i = 0; i < kWarmupFrames + kBenchFrames; ++i)
        frames[i] = makeFrame(i, res.w, res.h);

    for (int i = 0; i < kWarmupFrames; ++i)
        runOneFrame(&net, frames[i], res.w, res.h);

    std::vector<double> uploads, gpus, downloads, totals;
    uploads.reserve(kBenchFrames);
    gpus.reserve(kBenchFrames);
    downloads.reserve(kBenchFrames);
    totals.reserve(kBenchFrames);

    auto wallStart = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kBenchFrames; ++i) {
        auto ft = runOneFrame(&net, frames[kWarmupFrames + i], res.w, res.h);
        uploads.push_back(ft.uploadConvMs);
        gpus.push_back(ft.gpuInferMs);
        downloads.push_back(ft.downloadMs);
        totals.push_back(ft.totalMs);
    }

    auto wallEnd = std::chrono::high_resolution_clock::now();
    double wallSec = std::chrono::duration<double>(wallEnd - wallStart).count();
    double fps = kBenchFrames / wallSec;

    printf("\n");
    printf("  %-28s %6s  scale=%dx  out=%dx%d\n",
           model.name, res.label, model.scale,
           res.w * model.scale, res.h * model.scale);
    printf("  ---------------------------------------------------------\n");
    printf("  Effective FPS:           %8.1f  (%d frames / %.2fs)\n",
           fps, kBenchFrames, wallSec);
    printf("  -- Per-frame latency breakdown -------------------------\n");
    printf("  Total           P50=%7.2fms  P95=%7.2fms  P99=%7.2fms\n",
           pct(totals, 50), pct(totals, 95), pct(totals, 99));
    printf("  |-- Upload+Conv  P50=%7.2fms  P95=%7.2fms\n",
           pct(uploads, 50), pct(uploads, 95));
    printf("  |-- GPU Infer     P50=%7.2fms  P95=%7.2fms\n",
           pct(gpus, 50), pct(gpus, 95));
    printf("  |-- Download+Sync P50=%7.2fms  P95=%7.2fms\n",
           pct(downloads, 50), pct(downloads, 95));
    printf("  Max single-frame: %.2fms\n",
           *std::max_element(totals.begin(), totals.end()));

    net.clear();
}

int main() {
    printf("=== HLPlayer NCNN VSR GPU Benchmark ===\n");
    printf("Warmup: %d frames  |  Bench: %d frames  |  Unique content per frame\n",
           kWarmupFrames, kBenchFrames);

    for (const auto& model : kModels)
        for (const auto& res : kResolutions)
            runModelBench(model, res);

    printf("\n=== Done ===\n");
    return 0;
}
