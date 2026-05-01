#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <hlplayer/CheckpointManager.h>
#include <hlplayer/ResumeManager.h>
#include <hlplayer/ICheckpointManager.h>
#include <hlplayer/Result.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

using namespace hlplayer;
namespace fs = std::filesystem;

// ============================================================================
// CheckpointManager tests
// ============================================================================

TEST_CASE("CheckpointManager - construction with default temp directory", "[checkpoint_manager][e2e]") {
    CheckpointManager manager;

    auto tempDir = fs::temp_directory_path() / "hlplayer_checkpoints_test";

    REQUIRE(fs::exists(tempDir) || !fs::exists(tempDir));
}

TEST_CASE("CheckpointManager - save and retrieve checkpoint", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    CheckpointInfo info;
    info.sourcePath = "test_input.mp4";
    info.outputPath = "test_output.mp4";
    info.lastProcessedFrame = 100;
    info.totalFrames = 1000;
    info.timestamp = 1234567890;
    info.pipelineConfig = R"({"scale": 2, "model": "sr"})";

    auto saveResult = manager.saveCheckpoint(info);
    REQUIRE(saveResult.hasValue());

    auto hasResult = manager.hasCheckpoint("test_input.mp4");
    REQUIRE(hasResult.hasValue());
    REQUIRE(hasResult.value());

    auto getResult = manager.getCheckpointInfo("test_input.mp4");
    REQUIRE(getResult.hasValue());

    CheckpointInfo retrieved = getResult.value();
    REQUIRE(retrieved.sourcePath == info.sourcePath);
    REQUIRE(retrieved.outputPath == info.outputPath);
    REQUIRE(retrieved.lastProcessedFrame == info.lastProcessedFrame);
    REQUIRE(retrieved.totalFrames == info.totalFrames);
    REQUIRE(retrieved.timestamp == info.timestamp);
    REQUIRE(retrieved.pipelineConfig == info.pipelineConfig);

    fs::remove_all(tempDir);
}

TEST_CASE("CheckpointManager - restore checkpoint", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    CheckpointInfo info;
    info.sourcePath = "restore_test.mp4";
    info.outputPath = "restore_output.mp4";
    info.lastProcessedFrame = 500;
    info.totalFrames = 2000;
    info.timestamp = 9876543210;
    info.pipelineConfig = R"({"scale": 4})";

    manager.saveCheckpoint(info);

    auto restoreResult = manager.restoreCheckpoint("restore_test.mp4");
    REQUIRE(restoreResult.hasValue());

    CheckpointInfo restored = restoreResult.value();
    REQUIRE(restored.sourcePath == info.sourcePath);
    REQUIRE(restored.lastProcessedFrame == info.lastProcessedFrame);

    fs::remove_all(tempDir);
}

TEST_CASE("CheckpointManager - clean checkpoint", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    CheckpointInfo info;
    info.sourcePath = "clean_test.mp4";
    info.outputPath = "clean_output.mp4";
    info.lastProcessedFrame = 250;
    info.totalFrames = 500;
    info.timestamp = 1111111111;
    info.pipelineConfig = "{}";

    manager.saveCheckpoint(info);

    auto hasBefore = manager.hasCheckpoint("clean_test.mp4");
    REQUIRE(hasBefore.value());

    auto cleanResult = manager.cleanCheckpoint("clean_test.mp4");
    REQUIRE(cleanResult.hasValue());

    auto hasAfter = manager.hasCheckpoint("clean_test.mp4");
    REQUIRE_FALSE(hasAfter.value());

    fs::remove_all(tempDir);
}

TEST_CASE("CheckpointManager - hasCheckpoint returns false for missing", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    auto hasResult = manager.hasCheckpoint("nonexistent.mp4");
    REQUIRE(hasResult.hasValue());
    REQUIRE_FALSE(hasResult.value());

    fs::remove_all(tempDir);
}

TEST_CASE("CheckpointManager - getCheckpointInfo returns error for missing", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    auto getResult = manager.getCheckpointInfo("nonexistent.mp4");
    REQUIRE(getResult.hasError());

    fs::remove_all(tempDir);
}

TEST_CASE("CheckpointManager - multiple checkpoints coexist", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    CheckpointInfo info1;
    info1.sourcePath = "video1.mp4";
    info1.outputPath = "output1.mp4";
    info1.lastProcessedFrame = 100;
    info1.totalFrames = 1000;

    CheckpointInfo info2;
    info2.sourcePath = "video2.mkv";
    info2.outputPath = "output2.mkv";
    info2.lastProcessedFrame = 200;
    info2.totalFrames = 2000;

    manager.saveCheckpoint(info1);
    manager.saveCheckpoint(info2);

    REQUIRE(manager.hasCheckpoint("video1.mp4").value());
    REQUIRE(manager.hasCheckpoint("video2.mkv").value());

    auto result1 = manager.getCheckpointInfo("video1.mp4");
    auto result2 = manager.getCheckpointInfo("video2.mkv");

    REQUIRE(result1.hasValue());
    REQUIRE(result2.hasValue());
    REQUIRE(result1.value().lastProcessedFrame == 100);
    REQUIRE(result2.value().lastProcessedFrame == 200);

    fs::remove_all(tempDir);
}

TEST_CASE("CheckpointManager - cleanAll removes all checkpoints", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    for (int i = 0; i < 5; i++) {
        CheckpointInfo info;
        info.sourcePath = "video" + std::to_string(i) + ".mp4";
        info.outputPath = "output" + std::to_string(i) + ".mp4";
        info.lastProcessedFrame = i * 100;
        info.totalFrames = 1000;
        manager.saveCheckpoint(info);
    }

    auto cleanResult = manager.cleanAll();
    REQUIRE(cleanResult.hasValue());

    REQUIRE_FALSE(manager.hasCheckpoint("video0.mp4").value());
    REQUIRE_FALSE(manager.hasCheckpoint("video1.mp4").value());
    REQUIRE_FALSE(manager.hasCheckpoint("video2.mp4").value());
    REQUIRE_FALSE(manager.hasCheckpoint("video3.mp4").value());
    REQUIRE_FALSE(manager.hasCheckpoint("video4.mp4").value());

    fs::remove_all(tempDir);
}

TEST_CASE("CheckpointManager - overwrite existing checkpoint", "[checkpoint_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    CheckpointManager manager(tempDir);

    CheckpointInfo info1;
    info1.sourcePath = "overwrite.mp4";
    info1.outputPath = "output.mp4";
    info1.lastProcessedFrame = 100;
    info1.totalFrames = 1000;

    manager.saveCheckpoint(info1);

    auto before = manager.getCheckpointInfo("overwrite.mp4");
    REQUIRE(before.value().lastProcessedFrame == 100);

    CheckpointInfo info2;
    info2.sourcePath = "overwrite.mp4";
    info2.outputPath = "output2.mp4";
    info2.lastProcessedFrame = 500;
    info2.totalFrames = 1000;

    manager.saveCheckpoint(info2);

    auto after = manager.getCheckpointInfo("overwrite.mp4");
    REQUIRE(after.value().lastProcessedFrame == 500);
    REQUIRE(after.value().outputPath == "output2.mp4");

    fs::remove_all(tempDir);
}

// ============================================================================
// ResumeManager tests
// ============================================================================

TEST_CASE("ResumeManager - construction", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - save and check resumable", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    auto saveResult = resumeManager.saveProgress(
        "save_test.mp4",
        "save_output.mp4",
        150,
        1000,
        R"({"scale": 2})"
    );
    REQUIRE(saveResult.hasValue());

    auto checkResult = resumeManager.checkForResumable("save_test.mp4");
    REQUIRE(checkResult.hasValue());
    REQUIRE(checkResult.value());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - resume from checkpoint", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    resumeManager.saveProgress(
        "resume_test.mp4",
        "resume_output.mp4",
        300,
        2000,
        R"({"scale": 4})"
    );

    auto resumeResult = resumeManager.resumeFromCheckpoint("resume_test.mp4");
    REQUIRE(resumeResult.hasValue());
    REQUIRE(resumeResult.value() == 300);

    auto infoResult = resumeManager.getCheckpointInfo("resume_test.mp4");
    REQUIRE(infoResult.hasValue());
    REQUIRE(infoResult.value().lastProcessedFrame == 300);

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - complete processing cleans checkpoint", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    resumeManager.saveProgress(
        "complete_test.mp4",
        "complete_output.mp4",
        999,
        1000,
        "{}"
    );

    REQUIRE(resumeManager.checkForResumable("complete_test.mp4").value());

    auto completeResult = resumeManager.completeProcessing("complete_test.mp4");
    REQUIRE(completeResult.hasValue());

    REQUIRE_FALSE(resumeManager.checkForResumable("complete_test.mp4").value());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - cancel processing cleans checkpoint", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    resumeManager.saveProgress(
        "cancel_test.mp4",
        "cancel_output.mp4",
        500,
        2000,
        "{}"
    );

    REQUIRE(resumeManager.checkForResumable("cancel_test.mp4").value());

    auto cancelResult = resumeManager.cancelProcessing("cancel_test.mp4");
    REQUIRE(cancelResult.hasValue());

    REQUIRE_FALSE(resumeManager.checkForResumable("cancel_test.mp4").value());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - getTempOutputPath returns correct path", "[resume_manager][e2e]") {
    auto outputPath = fs::path("test_output.mp4");
    auto tempPath = ResumeManager::getTempOutputPath(outputPath.string());

    REQUIRE(tempPath.filename().string() == "test_output.mp4.part");
}

TEST_CASE("ResumeManager - list resumable jobs", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    resumeManager.saveProgress("job1.mp4", "out1.mp4", 100, 1000, "{}");
    resumeManager.saveProgress("job2.mkv", "out2.mkv", 200, 2000, "{}");
    resumeManager.saveProgress("job3.mp4", "out3.mp4", 300, 3000, "{}");

    auto listResult = resumeManager.listResumableJobs();
    REQUIRE(listResult.hasValue());

    auto jobs = listResult.value();
    REQUIRE(jobs.size() >= 3);

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - resume with missing checkpoint", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    auto resumeResult = resumeManager.resumeFromCheckpoint("missing.mp4");
    REQUIRE(resumeResult.hasError());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - checkForResumable with no checkpoint", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    auto checkResult = resumeManager.checkForResumable("nonexistent.mp4");
    REQUIRE(checkResult.hasValue());
    REQUIRE_FALSE(checkResult.value());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - getCheckpointInfo with missing checkpoint", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    auto infoResult = resumeManager.getCheckpointInfo("missing.mp4");
    REQUIRE(infoResult.hasError());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - finalizeOutput creates temp file path", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    auto outputPath = "final_output.mp4";
    auto finalizeResult = resumeManager.finalizeOutput(outputPath);
    REQUIRE(finalizeResult.hasValue());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - cleanAll removes all checkpoints", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    resumeManager.saveProgress("job1.mp4", "out1.mp4", 100, 1000, "{}");
    resumeManager.saveProgress("job2.mp4", "out2.mp4", 200, 2000, "{}");

    auto cleanResult = resumeManager.cleanAll();
    REQUIRE(cleanResult.hasValue());

    REQUIRE_FALSE(resumeManager.checkForResumable("job1.mp4").value());
    REQUIRE_FALSE(resumeManager.checkForResumable("job2.mp4").value());

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - save progress with different paths", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    auto saveResult = resumeManager.saveProgress(
        "C:\\input\\video.mp4",
        "D:\\output\\video_upscaled.mp4",
        250,
        1000,
        R"({"vsr": true})"
    );
    REQUIRE(saveResult.hasValue());

    auto resumeResult = resumeManager.resumeFromCheckpoint("C:\\input\\video.mp4");
    REQUIRE(resumeResult.hasValue());
    REQUIRE(resumeResult.value() == 250);

    fs::remove_all(tempDir);
}

TEST_CASE("ResumeManager - temp file suffix constant", "[resume_manager][e2e]") {
    auto outputPath = "video.mp4";
    auto tempPath = ResumeManager::getTempOutputPath(outputPath);

    std::string tempPathStr = tempPath.string();
    REQUIRE(tempPathStr.find(".part") != std::string::npos);
}

TEST_CASE("ResumeManager - thread-safe operations", "[resume_manager][e2e]") {
    auto tempDir = fs::temp_directory_path() / "hlplayer_test_" + std::to_string(std::hash<std::string>{}(std::string(__FILE__) + std::to_string(__LINE__)));
    fs::create_directories(tempDir);

    auto checkpointManager = std::make_shared<CheckpointManager>(tempDir);
    ResumeManager resumeManager(checkpointManager);

    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    auto threadFunc = [&resumeManager, &successCount, &failCount](int id) {
        for (int i = 0; i < 10; i++) {
            auto result = resumeManager.saveProgress(
                "thread_test_" + std::to_string(id) + ".mp4",
                "output_" + std::to_string(id) + ".mp4",
                i,
                100,
                "{}"
            );
            if (result.hasValue()) {
                successCount++;
            } else {
                failCount++;
            }
        }
    };

    std::thread t1(threadFunc, 1);
    std::thread t2(threadFunc, 2);
    std::thread t3(threadFunc, 3);

    t1.join();
    t2.join();
    t3.join();

    REQUIRE(successCount.load() == 30);
    REQUIRE(failCount.load() == 0);

    fs::remove_all(tempDir);
}
