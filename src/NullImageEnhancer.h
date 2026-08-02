#pragma once

#include "ImageEnhancer.h"

class NullImageEnhancer final : public ImageEnhancer {
public:
    [[nodiscard]] bool IsAvailable() const noexcept override { return false; }
    [[nodiscard]] std::wstring Name() const override { return L"RTX（尚未安裝 SDK 後端）"; }
    [[nodiscard]] std::wstring Status() const override;
    std::shared_ptr<ImageData> Enhance(
        const ImageData& source, std::uint32_t targetWidth, std::uint32_t targetHeight,
        std::wstring& error) override;
};

