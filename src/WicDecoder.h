#pragma once

#include "ImageData.h"

#include <memory>
#include <string>

class WicDecoder {
public:
    static std::shared_ptr<ImageData> Decode(const std::wstring& path, std::wstring& error);
};

