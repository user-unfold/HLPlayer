#include "RenderBridge.h"
#include <spdlog/spdlog.h>

namespace hlplayer {
namespace render {

RenderBridge::RenderBridge() {
    spdlog::info("HLPlayer::RenderBridge constructed");
}

RenderBridge::~RenderBridge() {
    spdlog::info("HLPlayer::RenderBridge destructed");
}

void RenderBridge::initialize() {
    spdlog::info("HLPlayer::RenderBridge initialized");
}

void RenderBridge::render() {
    spdlog::info("HLPlayer::RenderBridge rendering");
}

} // namespace render
} // namespace hlplayer
