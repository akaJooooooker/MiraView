#pragma once

#include "ImageEnhancer.h"

#include <Windows.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

class EnhancementWorker {
public:
    static constexpr UINT EnhancementReadyMessage = WM_APP + 2;

    struct Result {
        std::uint64_t generation = 0;
        std::shared_ptr<ImageData> image;
        std::wstring error;
    };

    EnhancementWorker(ImageEnhancer& enhancer, HWND notificationWindow);
    ~EnhancementWorker();

    EnhancementWorker(const EnhancementWorker&) = delete;
    EnhancementWorker& operator=(const EnhancementWorker&) = delete;

    void Request(std::shared_ptr<ImageData> source, std::uint32_t targetWidth,
        std::uint32_t targetHeight, std::uint64_t generation);
    [[nodiscard]] Result TakeResult();

private:
    struct WorkItem {
        std::shared_ptr<ImageData> source;
        std::uint32_t targetWidth = 0;
        std::uint32_t targetHeight = 0;
        std::uint64_t generation = 0;
    };

    void WorkerLoop();

    ImageEnhancer& enhancer_;
    HWND notificationWindow_ = nullptr;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    WorkItem pending_;
    Result result_;
    bool hasPending_ = false;
    bool stopping_ = false;
};

