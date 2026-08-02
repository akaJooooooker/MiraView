#include "NullImageEnhancer.h"

std::wstring NullImageEnhancer::Status() const {
    return L"高速檢視核心已啟用；RTX 後端將在 NVIDIA SDK 驗證階段接入。";
}

std::shared_ptr<ImageData> NullImageEnhancer::Enhance(
    const ImageData&, std::uint32_t, std::uint32_t, std::wstring& error) {
    error = Status();
    return {};
}

