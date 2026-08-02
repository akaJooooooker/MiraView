#include "NullImageEnhancer.h"

std::wstring NullImageEnhancer::Status() const {
    return L"此版本未包含 NVIDIA RTX Video SDK 後端；一般圖片檢視仍可正常使用。 / "
           L"This build does not include the NVIDIA RTX Video SDK backend; normal image viewing remains available.";
}

std::shared_ptr<ImageData> NullImageEnhancer::Enhance(
    const ImageData&, std::uint32_t, std::uint32_t, std::wstring& error) {
    error = Status();
    return {};
}

