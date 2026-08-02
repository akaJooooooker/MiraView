#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

class FolderModel {
public:
    bool Open(const std::filesystem::path& selectedFile);

    [[nodiscard]] bool Empty() const noexcept { return files_.empty(); }
    [[nodiscard]] std::size_t Size() const noexcept { return files_.size(); }
    [[nodiscard]] std::size_t Index() const noexcept { return index_; }
    [[nodiscard]] const std::filesystem::path& Current() const;
    [[nodiscard]] const std::filesystem::path& At(std::size_t index) const;
    [[nodiscard]] const std::filesystem::path& Directory() const noexcept { return directory_; }

    bool Move(int delta);
    bool MoveTo(std::size_t index);

    static bool IsSupportedImage(const std::filesystem::path& path);

private:
    std::filesystem::path directory_;
    std::vector<std::filesystem::path> files_;
    std::size_t index_ = 0;
};

