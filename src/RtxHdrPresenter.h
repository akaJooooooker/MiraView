#pragma once

#include "ImageData.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>

enum class HdrPreset : int {
    Standard = 0,
    Vivid = 1,
    Gentle = 2
};

class RtxHdrPresenter final {
public:
    static constexpr UINT FrameReadyMessage = WM_APP + 4;

    RtxHdrPresenter() = default;
    ~RtxHdrPresenter();

    RtxHdrPresenter(const RtxHdrPresenter&) = delete;
    RtxHdrPresenter& operator=(const RtxHdrPresenter&) = delete;

    bool Initialize(HWND window, std::wstring& error);
    bool Render(std::shared_ptr<ImageData> image, const RECT& destination, HdrPreset preset,
        bool& usedVsr, std::wstring& error);
    void Invalidate() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] unsigned int MaxLuminance() const noexcept { return maxLuminance_; }
    [[nodiscard]] const std::wstring& MonitorName() const noexcept { return monitorName_; }

private:
    struct RenderCore;
    bool RefreshHdrMonitor(std::wstring& error);
    bool CreateSwapChain(std::wstring& error);
    bool ResizeSwapChain(UINT width, UINT height, std::wstring& error);
    bool ClearAndPresent(std::wstring& error);

    HWND window_ = nullptr;
    HMONITOR monitor_ = nullptr;
    std::shared_ptr<RenderCore> core_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> hdrTexture_;
    std::wstring monitorName_;
    std::wstring requestedPath_;
    std::wstring displayedPath_;
    RECT requestedDestination_{};
    RECT renderedDestination_{};
    HdrPreset requestedPreset_ = HdrPreset::Standard;
    unsigned int maxLuminance_ = 1000;
    UINT requestedClientWidth_ = 0;
    UINT requestedClientHeight_ = 0;
    std::uint64_t requestGeneration_ = 0;
    bool renderedUsedVsr_ = false;
};
