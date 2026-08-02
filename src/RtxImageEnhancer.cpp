#include "RtxImageEnhancer.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <sstream>

using Microsoft::WRL::ComPtr;

RtxImageEnhancer::RtxImageEnhancer() = default;

RtxImageEnhancer::~RtxImageEnhancer() {
    std::scoped_lock lock(mutex_);
    ShutdownLocked();
}

bool RtxImageEnhancer::IsAvailable() const noexcept {
    std::scoped_lock lock(mutex_);
    return !initializationAttempted_ || initialized_;
}

std::wstring RtxImageEnhancer::Status() const {
    std::scoped_lock lock(mutex_);
    return status_;
}

std::shared_ptr<ImageData> RtxImageEnhancer::Enhance(
    const ImageData& source, const std::uint32_t targetWidth,
    const std::uint32_t targetHeight, std::wstring& error) {
    std::scoped_lock lock(mutex_);
    error.clear();
    if (!InitializeLocked(error)) return {};

    if (source.width == 0 || source.height == 0 || source.pixels.empty() ||
        targetWidth <= source.width || targetHeight <= source.height) {
        error = L"RTX VSR 只在圖片需要放大時執行。";
        return {};
    }
    if (targetWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        targetHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        source.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        source.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        error = L"圖片尺寸超過 D3D11 texture 上限。";
        return {};
    }

    D3D11_TEXTURE2D_DESC inputDescription{};
    inputDescription.Width = source.width;
    inputDescription.Height = source.height;
    inputDescription.MipLevels = 1;
    inputDescription.ArraySize = 1;
    inputDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    inputDescription.SampleDesc.Count = 1;
    inputDescription.Usage = D3D11_USAGE_DEFAULT;
    inputDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    const D3D11_SUBRESOURCE_DATA initialData{
        source.pixels.data(), source.stride, static_cast<UINT>(source.pixels.size())};
    ComPtr<ID3D11Texture2D> input;
    HRESULT hr = device_->CreateTexture2D(&inputDescription, &initialData, &input);
    if (FAILED(hr)) {
        error = L"建立 RTX 輸入 texture 失敗。";
        return {};
    }

    D3D11_TEXTURE2D_DESC outputDescription{};
    outputDescription.Width = targetWidth;
    outputDescription.Height = targetHeight;
    outputDescription.MipLevels = 1;
    outputDescription.ArraySize = 1;
    outputDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    outputDescription.SampleDesc.Count = 1;
    outputDescription.Usage = D3D11_USAGE_DEFAULT;
    outputDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;

    ComPtr<ID3D11Texture2D> output;
    hr = device_->CreateTexture2D(&outputDescription, nullptr, &output);
    if (FAILED(hr)) {
        error = L"建立 RTX 輸出 texture 失敗。";
        return {};
    }

    NVSDK_NGX_D3D11_VSR_Eval_Params evaluate{};
    evaluate.pInput = input.Get();
    evaluate.pOutput = output.Get();
    evaluate.InputSubrectBase = {0, 0};
    evaluate.InputSubrectSize = {source.width, source.height};
    evaluate.OutputSubrectBase = {0, 0};
    evaluate.OutputSubrectSize = {targetWidth, targetHeight};
    evaluate.QualityLevel = NVSDK_NGX_VSR_Quality_Ultra;

    if (multithread_) multithread_->Enter();
    const NVSDK_NGX_Result evaluationResult = NGX_D3D11_EVALUATE_VSR_EXT(
        context_.Get(), feature_, parameters_, &evaluate);
    if (multithread_) multithread_->Leave();
    if (NVSDK_NGX_FAILED(evaluationResult)) {
        error = NgxError(L"VSR evaluate", evaluationResult);
        SetStatusLocked(error);
        return {};
    }

    D3D11_TEXTURE2D_DESC stagingDescription = outputDescription;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    hr = device_->CreateTexture2D(&stagingDescription, nullptr, &staging);
    if (FAILED(hr)) {
        error = L"建立 RTX readback texture 失敗。";
        return {};
    }
    context_->CopyResource(staging.Get(), output.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        error = L"讀回 RTX VSR 結果失敗。";
        return {};
    }

    std::shared_ptr<ImageData> result;
    try {
        result = std::make_shared<ImageData>();
        result->path = source.path;
        result->width = targetWidth;
        result->height = targetHeight;
        result->stride = targetWidth * 4U;
        result->pixels.resize(static_cast<std::size_t>(result->stride) * targetHeight);
        for (std::uint32_t row = 0; row < targetHeight; ++row) {
            const auto* sourceRow = static_cast<const std::byte*>(mapped.pData) +
                static_cast<std::size_t>(row) * mapped.RowPitch;
            auto* targetRow = result->pixels.data() + static_cast<std::size_t>(row) * result->stride;
            std::memcpy(targetRow, sourceRow, result->stride);
        }
    } catch (...) {
        context_->Unmap(staging.Get(), 0);
        error = L"RTX 結果需要的記憶體不足。";
        return {};
    }
    context_->Unmap(staging.Get(), 0);

    SetStatusLocked(L"RTX Video VSR 可用，品質等級：Ultra（4）。");
    return result;
}

bool RtxImageEnhancer::InitializeLocked(std::wstring& error) {
    if (initialized_) return true;
    if (initializationAttempted_) {
        error = status_;
        return false;
    }
    initializationAttempted_ = true;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &device_, &selected, &context_);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, 1,
            D3D11_SDK_VERSION, &device_, &selected, &context_);
    }
    if (FAILED(hr)) {
        error = L"無法建立支援 RTX 的 D3D11 裝置。";
        SetStatusLocked(error);
        return false;
    }
    if (SUCCEEDED(context_.As(&multithread_))) multithread_->SetMultithreadProtected(TRUE);

    wchar_t temporaryPath[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(temporaryPath)), temporaryPath);
    const NVSDK_NGX_Result initResult = NVSDK_NGX_D3D11_Init(0, temporaryPath, device_.Get());
    if (NVSDK_NGX_FAILED(initResult)) {
        error = NgxError(L"NGX init", initResult);
        SetStatusLocked(error);
        ShutdownLocked();
        return false;
    }
    ngxInitialized_ = true;

    const NVSDK_NGX_Result capabilityResult =
        NVSDK_NGX_D3D11_GetCapabilityParameters(&parameters_);
    if (NVSDK_NGX_FAILED(capabilityResult) || !parameters_) {
        error = NgxError(L"NGX capabilities", capabilityResult);
        SetStatusLocked(error);
        ShutdownLocked();
        return false;
    }

    int available = 0;
    const NVSDK_NGX_Result availabilityResult =
        parameters_->Get(NVSDK_NGX_Parameter_VSR_Available, &available);
    if (NVSDK_NGX_FAILED(availabilityResult) || available == 0) {
        error = L"目前 GPU／驅動或 nvngx_vsr.dll 不支援 VSR。";
        SetStatusLocked(error);
        ShutdownLocked();
        return false;
    }

    NVSDK_NGX_Feature_Create_Params create{};
    if (multithread_) multithread_->Enter();
    const NVSDK_NGX_Result createResult = NGX_D3D11_CREATE_VSR_EXT(
        context_.Get(), &feature_, parameters_, &create);
    if (multithread_) multithread_->Leave();
    if (NVSDK_NGX_FAILED(createResult) || !feature_) {
        error = NgxError(L"VSR create", createResult);
        SetStatusLocked(error);
        ShutdownLocked();
        return false;
    }

    initialized_ = true;
    SetStatusLocked(L"RTX Video VSR 已初始化，品質等級：Ultra（4）。");
    return true;
}

void RtxImageEnhancer::ShutdownLocked() noexcept {
    if (feature_) {
        NVSDK_NGX_D3D11_ReleaseFeature(feature_);
        feature_ = nullptr;
    }
    if (ngxInitialized_ && device_) {
        NVSDK_NGX_D3D11_Shutdown1(device_.Get());
        ngxInitialized_ = false;
    }
    if (parameters_) {
        NVSDK_NGX_D3D11_DestroyParameters(parameters_);
        parameters_ = nullptr;
    }
    multithread_.Reset();
    context_.Reset();
    device_.Reset();
    initialized_ = false;
}

void RtxImageEnhancer::SetStatusLocked(std::wstring status) {
    status_ = std::move(status);
}

std::wstring RtxImageEnhancer::NgxError(
    const wchar_t* operation, const NVSDK_NGX_Result result) {
    std::wostringstream stream;
    stream << operation << L" 失敗（NGX 0x" << std::hex
           << static_cast<unsigned long long>(result) << L"）。";
    return stream.str();
}
