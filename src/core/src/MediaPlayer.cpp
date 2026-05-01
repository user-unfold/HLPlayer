#include <hlplayer/MediaPlayer.h>

#include <spdlog/spdlog.h>

namespace hlplayer {

MediaPlayer::MediaPlayer(std::unique_ptr<FFPlayer> player)
    : player_(std::move(player)) {
    running_.store(true);
    msgThread_ = std::thread(&MediaPlayer::msgLoop, this);
}

MediaPlayer::~MediaPlayer() {
    postMessage(MsgShutdown{});
    if (msgThread_.joinable()) msgThread_.join();
}

void MediaPlayer::open(const std::string& url) {
    postMessage(MsgDoOpen{url});
}

void MediaPlayer::play() {
    postMessage(MsgDoPlay{});
}

void MediaPlayer::pause() {
    postMessage(MsgDoPause{});
}

void MediaPlayer::stop() {
    postMessage(MsgDoStop{});
}

void MediaPlayer::seek(double seconds) {
    pendingSeekPos_.store(seconds);
    bool expected = false;
    if (seekQueued_.compare_exchange_strong(expected, true)) {
        postMessage(MsgDoSeek{seconds});
    }
}

void MediaPlayer::setVolume(double volume) {
    postMessage(MsgDoSetVolume{volume});
}

PlayerState MediaPlayer::state() const {
    return player_ ? player_->getState() : PlayerState_Idle;
}

double MediaPlayer::position() const {
    return player_ ? player_->getPosition() : 0.0;
}

double MediaPlayer::duration() const {
    return player_ ? player_->getDuration() : 0.0;
}

EventBus& MediaPlayer::eventBus() {
    return player_->eventBus();
}

void MediaPlayer::postMessage(PlayerMessage msg) {
    {
        std::lock_guard lock(msgMutex_);
        msgQueue_.push(std::move(msg));
    }
    msgCv_.notify_one();
}

void MediaPlayer::msgLoop() {
    spdlog::info("MediaPlayer msgLoop started");
    while (running_.load()) {
        PlayerMessage msg;
        {
            std::unique_lock lock(msgMutex_);
            msgCv_.wait(lock, [this] { return !msgQueue_.empty() || !running_.load(); });
            if (!running_.load() && msgQueue_.empty()) break;
            if (msgQueue_.empty()) continue;
            msg = std::move(msgQueue_.front());
            msgQueue_.pop();
        }

        std::visit([this](auto&& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, MsgDoOpen>) {
                player_->open(m.url);
            } else if constexpr (std::is_same_v<T, MsgDoPlay>) {
                player_->play();
            } else if constexpr (std::is_same_v<T, MsgDoPause>) {
                player_->pause();
            } else if constexpr (std::is_same_v<T, MsgDoStop>) {
                player_->stop();
            } else if constexpr (std::is_same_v<T, MsgDoSeek>) {
                double latest = pendingSeekPos_.load();
                seekQueued_.store(false);
                player_->seek(latest);
            } else if constexpr (std::is_same_v<T, MsgDoSetVolume>) {
                player_->setVolume(m.volume);
            } else if constexpr (std::is_same_v<T, MsgShutdown>) {
                running_.store(false);
                player_->stop();
            }
        }, msg);
    }
    spdlog::info("MediaPlayer msgLoop stopped");
}

} // namespace hlplayer
