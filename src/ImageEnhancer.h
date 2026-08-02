#pragma once

#include "ImageData.h"

#include <memory>
#include <string>

class ImageEnhancer {
public:
    virtual ~ImageEnhancer() = default;
    [[nodiscard]] virtual bool IsAvailable() const noexcept = 0;
    [[nodiscard]] virtual std::wstring Name() const = 0;
    [[nodiscard]] virtual std::wstring Status() const = 0;
    virtual std::shared_ptr<ImageData> Enhance(
        const ImageData& source, std::uint32_t targetWidth, std::uint32_t targetHeight,
        std::wstring& error) = 0;
};

