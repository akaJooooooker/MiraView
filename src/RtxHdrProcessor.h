#pragma once

#include "ImageData.h"

#include <Windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_truehdr.h>
#include <nvsdk_ngx_defs_vsr.h>
#include <nvsdk_ngx_helpers_truehdr.h>
#include <nvsdk_ngx_helpers_vsr.h>

#include <cstdint>
#include <string>

struct RtxHdrSettings {
    unsigned int contrast = 100;
    unsigned int saturation = 100;
    unsigned int middleGray = 50;
    unsigned int maxLuminance = 1000;
};

class RtxHdrProcessor final {
public:
    RtxHdrProcessor() = default;
    ~RtxHdrProcessor();

    RtxHdrProcessor(const RtxHdrProcessor&) = delete;
    RtxHdrProcessor& operator=(const RtxHdrProcessor&) = delete;

    bool Initialize(std::wstring& error);
    bool Process(
        const ImageData& source, std::uint32_t targetWidth, std::uint32_t targetHeight,
        const RECT& outputRectangle, const RtxHdrSettings& settings,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& output, std::wstring& error,
        bool enableVsr = false, bool* usedVsr = nullptr);
    void Shutdown() noexcept;

    [[nodiscard]] ID3D11Device* Device() const noexcept { return device_.Get(); }
    [[nodiscard]] ID3D11DeviceContext* Context() const noexcept { return context_.Get(); }
    [[nodiscard]] const std::wstring& AdapterName() const noexcept { return adapterName_; }
    [[nodiscard]] bool VsrAvailable() const noexcept { return vsrFeature_ != nullptr; }

private:
    static std::wstring NgxError(const wchar_t* operation, NVSDK_NGX_Result result);

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D10Multithread> multithread_;
    NVSDK_NGX_Parameter* parameters_ = nullptr;
    NVSDK_NGX_Handle* hdrFeature_ = nullptr;
    NVSDK_NGX_Handle* vsrFeature_ = nullptr;
    bool ngxInitialized_ = false;
    std::wstring adapterName_;
};
