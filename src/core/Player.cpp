#include "Player.h"
#include <spdlog/spdlog.h>

namespace hlplayer {
namespace core {

Player::Player() {
    spdlog::info("HLPlayer::Player constructed");
}

Player::~Player() {
    spdlog::info("HLPlayer::Player destructed");
}

void Player::initialize() {
    spdlog::info("HLPlayer::Player initialized");
}

void Player::shutdown() {
    spdlog::info("HLPlayer::Player shutdown");
}

} // namespace core
} // namespace hlplayer
