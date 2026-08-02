#include "FolderModel.h"

#include <Windows.h>
#include <Shlwapi.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <system_error>

namespace {
bool EqualsPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
}

bool FolderModel::Open(const std::filesystem::path& selectedFile) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(selectedFile, ec).lexically_normal();
    if (ec || !std::filesystem::is_regular_file(absolute, ec)) {
        return false;
    }

    directory_ = absolute.parent_path();
    files_.clear();

    std::filesystem::directory_iterator iterator(
        directory_, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        if (iterator->is_regular_file(ec) && IsSupportedImage(iterator->path())) {
            files_.push_back(iterator->path().lexically_normal());
        }
        ec.clear();
    }

    std::ranges::sort(files_, [](const auto& left, const auto& right) {
        const int result = StrCmpLogicalW(left.filename().c_str(), right.filename().c_str());
        if (result != 0) {
            return result < 0;
        }
        return left.native() < right.native();
    });

    const auto selected = std::ranges::find_if(files_, [&](const auto& file) {
        return EqualsPath(file, absolute);
    });
    if (selected == files_.end()) {
        files_.clear();
        return false;
    }

    index_ = static_cast<std::size_t>(std::distance(files_.begin(), selected));
    return true;
}

const std::filesystem::path& FolderModel::Current() const { return files_.at(index_); }
const std::filesystem::path& FolderModel::At(const std::size_t index) const { return files_.at(index); }

bool FolderModel::Move(const int delta) {
    if (files_.empty() || delta == 0) return false;
    const auto count = static_cast<long long>(files_.size());
    const auto current = static_cast<long long>(index_);
    const auto next = std::clamp(current + static_cast<long long>(delta), 0LL, count - 1);
    if (next == current) return false;
    index_ = static_cast<std::size_t>(next);
    return true;
}

bool FolderModel::MoveTo(const std::size_t index) {
    if (index >= files_.size() || index == index_) return false;
    index_ = index;
    return true;
}

bool FolderModel::IsSupportedImage(const std::filesystem::path& path) {
    static constexpr std::array extensions{
        L".jpg", L".jpeg", L".jpe", L".png", L".bmp", L".dib", L".gif",
        L".tif", L".tiff", L".webp", L".heic", L".heif", L".avif",
        L".jxl", L".ico", L".wdp", L".jxr", L".hdp"
    };
    auto extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](const wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return std::ranges::find(extensions, extension) != extensions.end();
}

