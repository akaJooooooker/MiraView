#pragma once

#include "ImageEnhancer.h"

#include <d3d11_4.h>
#include <wrl/client.h>

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_vsr.h>
#include <nvsdk_ngx_helpers_vsr.h>

#include <mutex>

class RtxImageEnhancer final : public ImageEnhancer {
public:
    RtxImageEnhancer();
    ~RtxImageEnhancer() override;

    [[nodiscard]] bool IsAvailable() const noexcept override;
    [[nodiscard]] std::wstring Name() const override { return L"NVIDIA RTX Video VSR"; }
    [[nodiscard]] std::wstring Status() const override;
    std::shared_ptr<ImageData> Enhance(
        const ImageData& source, std::uint32_t targetWidth, std::uint32_t targetHeight,
        std::wstring& error) override;

private:
    bool InitializeLocked(std::wstring& error);
    void ShutdownLocked() noexcept;
    void SetStatusLocked(std::wstring status);
    static std::wstring NgxError(const wchar_t* operation, NVSDK_NGX_Result result);

    mutable std::mutex mutex_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D10Multithread> multithread_;
    NVSDK_NGX_Parameter* parameters_ = nullptr;
    NVSDK_NGX_Handle* feature_ = nullptr;
    bool initialized_ = false;
    bool ngxInitialized_ = false;
    bool initializationAttempted_ = false;
    std::wstring status_ = L"RTX Video SDK 1.1 已連結；第一次啟用時初始化 VSR。";
};
