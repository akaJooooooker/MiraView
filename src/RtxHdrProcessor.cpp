#include "RtxHdrProcessor.h"

#include <dxgi1_6.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace {
constexpr UINT NvidiaVendorId = 0x10DE;

ComPtr<IDXGIAdapter1> FindNvidiaAdapter() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return {};

    ComPtr<IDXGIFactory6> factory6;
    factory.As(&factory6);
    if (factory6) {
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory6->EnumAdapterByGpuPreference(
                    index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)) &&
                description.VendorId == NvidiaVendorId &&
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) return adapter;
        }
    }

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)) &&
            description.VendorId == NvidiaVendorId &&
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) return adapter;
    }
    return {};
}
}

RtxHdrProcessor::~RtxHdrProcessor() {
    Shutdown();
}

bool RtxHdrProcessor::Initialize(std::wstring& error) {
    error.clear();
    if (hdrFeature_) return true;

    adapter_ = FindNvidiaAdapter();
    if (!adapter_) {
        error = L"找不到 NVIDIA RTX 顯示卡。 / No NVIDIA RTX adapter was found.";
        return false;
    }
    DXGI_ADAPTER_DESC1 adapterDescription{};
    adapter_->GetDesc1(&adapterDescription);
    adapterName_ = adapterDescription.Description;

    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(
        adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &device_, &selected, &context_);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(
            adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels + 1, 1,
            D3D11_SDK_VERSION, &device_, &selected, &context_);
    }
    if (FAILED(hr)) {
        error = L"無法建立 NVIDIA D3D11 裝置。 / Could not create the NVIDIA D3D11 device.";
        Shutdown();
        return false;
    }
    if (SUCCEEDED(context_.As(&multithread_))) multithread_->SetMultithreadProtected(TRUE);

    wchar_t temporaryPath[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(temporaryPath)), temporaryPath);
    const NVSDK_NGX_Result initResult = NVSDK_NGX_D3D11_Init(0, temporaryPath, device_.Get());
    if (NVSDK_NGX_FAILED(initResult)) {
        error = NgxError(L"TrueHDR NGX init", initResult);
        Shutdown();
        return false;
    }
    ngxInitialized_ = true;

    const NVSDK_NGX_Result capabilityResult =
        NVSDK_NGX_D3D11_GetCapabilityParameters(&parameters_);
    if (NVSDK_NGX_FAILED(capabilityResult) || !parameters_) {
        error = NgxError(L"TrueHDR capabilities", capabilityResult);
        Shutdown();
        return false;
    }

    int available = 0;
    const NVSDK_NGX_Result availabilityResult =
        parameters_->Get(NVSDK_NGX_Parameter_TrueHDR_Available, &available);
    if (NVSDK_NGX_FAILED(availabilityResult) || available == 0) {
        std::wostringstream stream;
        stream << L"RTX Video HDR unavailable: capability_result=0x" << std::hex
               << static_cast<unsigned long long>(availabilityResult)
               << L", available=" << std::dec << available << L". "
               << L"Update the NVIDIA driver and verify nvngx_truehdr.dll.";
        error = stream.str();
        Shutdown();
        return false;
    }

    NVSDK_NGX_Feature_Create_Params create{};
    if (multithread_) multithread_->Enter();
    const NVSDK_NGX_Result createResult = NGX_D3D11_CREATE_TRUEHDR_EXT(
        context_.Get(), &hdrFeature_, parameters_, &create);
    if (multithread_) multithread_->Leave();
    if (NVSDK_NGX_FAILED(createResult) || !hdrFeature_) {
        error = NgxError(L"TrueHDR create", createResult);
        Shutdown();
        return false;
    }

    int vsrAvailable = 0;
    if (NVSDK_NGX_SUCCEED(
            parameters_->Get(NVSDK_NGX_Parameter_VSR_Available, &vsrAvailable)) &&
        vsrAvailable != 0) {
        NVSDK_NGX_Feature_Create_Params vsrCreate{};
        if (multithread_) multithread_->Enter();
        const NVSDK_NGX_Result vsrCreateResult = NGX_D3D11_CREATE_VSR_EXT(
            context_.Get(), &vsrFeature_, parameters_, &vsrCreate);
        if (multithread_) multithread_->Leave();
        if (NVSDK_NGX_FAILED(vsrCreateResult)) vsrFeature_ = nullptr;
    }
    return true;
}

bool RtxHdrProcessor::Process(
    const ImageData& source, const std::uint32_t targetWidth,
    const std::uint32_t targetHeight, const RECT& outputRectangle,
    const RtxHdrSettings& settings, ComPtr<ID3D11Texture2D>& output,
    std::wstring& error, const bool enableVsr, bool* usedVsr) {
    error.clear();
    output.Reset();
    if (usedVsr) *usedVsr = false;
    if (!Initialize(error)) return false;
    if (source.width == 0 || source.height == 0 || source.pixels.empty() ||
        targetWidth == 0 || targetHeight == 0 ||
        targetWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        targetHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        error = L"TrueHDR 輸入或輸出尺寸無效。 / Invalid TrueHDR dimensions.";
        return false;
    }
    if (outputRectangle.left < 0 || outputRectangle.top < 0 ||
        outputRectangle.right <= outputRectangle.left ||
        outputRectangle.bottom <= outputRectangle.top ||
        outputRectangle.right > static_cast<LONG>(targetWidth) ||
        outputRectangle.bottom > static_cast<LONG>(targetHeight)) {
        error = L"TrueHDR 顯示範圍無效。 / Invalid TrueHDR output rectangle.";
        return false;
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
    const D3D11_SUBRESOURCE_DATA inputData{
        source.pixels.data(), source.stride, static_cast<UINT>(source.pixels.size())};
    ComPtr<ID3D11Texture2D> input;
    HRESULT hr = device_->CreateTexture2D(&inputDescription, &inputData, &input);
    if (FAILED(hr)) {
        error = L"無法建立 TrueHDR 輸入 texture。 / Could not create the TrueHDR input texture.";
        return false;
    }

    ComPtr<ID3D11Texture2D> hdrInput = input;
    std::uint32_t hdrInputWidth = source.width;
    std::uint32_t hdrInputHeight = source.height;
    const auto displayWidth = static_cast<std::uint32_t>(outputRectangle.right - outputRectangle.left);
    const auto displayHeight = static_cast<std::uint32_t>(outputRectangle.bottom - outputRectangle.top);
    const bool needsUpscale = displayWidth > source.width && displayHeight > source.height;
    if (enableVsr && needsUpscale) {
        if (!vsrFeature_) {
            error = L"RTX VSR 無法使用；請確認 nvngx_vsr.dll 與 NVIDIA 驅動程式。 / "
                    L"RTX VSR is unavailable; verify nvngx_vsr.dll and the NVIDIA driver.";
            return false;
        }

        D3D11_TEXTURE2D_DESC vsrDescription{};
        vsrDescription.Width = displayWidth;
        vsrDescription.Height = displayHeight;
        vsrDescription.MipLevels = 1;
        vsrDescription.ArraySize = 1;
        vsrDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        vsrDescription.SampleDesc.Count = 1;
        vsrDescription.Usage = D3D11_USAGE_DEFAULT;
        vsrDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> vsrOutput;
        hr = device_->CreateTexture2D(&vsrDescription, nullptr, &vsrOutput);
        if (FAILED(hr)) {
            error = L"無法建立整合式 VSR 輸出 texture。 / "
                    L"Could not create the integrated VSR output texture.";
            return false;
        }

        NVSDK_NGX_D3D11_VSR_Eval_Params vsrEvaluate{};
        vsrEvaluate.pInput = input.Get();
        vsrEvaluate.pOutput = vsrOutput.Get();
        vsrEvaluate.InputSubrectBase = {0, 0};
        vsrEvaluate.InputSubrectSize = {source.width, source.height};
        vsrEvaluate.OutputSubrectBase = {0, 0};
        vsrEvaluate.OutputSubrectSize = {displayWidth, displayHeight};
        vsrEvaluate.QualityLevel = NVSDK_NGX_VSR_Quality_Ultra;
        if (multithread_) multithread_->Enter();
        const NVSDK_NGX_Result vsrResult = NGX_D3D11_EVALUATE_VSR_EXT(
            context_.Get(), vsrFeature_, parameters_, &vsrEvaluate);
        if (multithread_) multithread_->Leave();
        if (NVSDK_NGX_FAILED(vsrResult)) {
            error = NgxError(L"VSR evaluate", vsrResult);
            return false;
        }
        hdrInput = std::move(vsrOutput);
        hdrInputWidth = displayWidth;
        hdrInputHeight = displayHeight;
        if (usedVsr) *usedVsr = true;
    }

    D3D11_TEXTURE2D_DESC outputDescription{};
    outputDescription.Width = targetWidth;
    outputDescription.Height = targetHeight;
    outputDescription.MipLevels = 1;
    outputDescription.ArraySize = 1;
    outputDescription.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    outputDescription.SampleDesc.Count = 1;
    outputDescription.Usage = D3D11_USAGE_DEFAULT;
    outputDescription.BindFlags = D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    hr = device_->CreateTexture2D(&outputDescription, nullptr, &output);
    if (FAILED(hr)) {
        error = L"無法建立 10-bit TrueHDR 輸出 texture。 / "
                L"Could not create the 10-bit TrueHDR output texture.";
        return false;
    }

    ComPtr<ID3D11RenderTargetView> outputView;
    hr = device_->CreateRenderTargetView(output.Get(), nullptr, &outputView);
    if (FAILED(hr)) {
        error = L"無法建立 TrueHDR 輸出檢視。 / Could not create the TrueHDR output view.";
        output.Reset();
        return false;
    }
    constexpr float black[]{0.0F, 0.0F, 0.0F, 1.0F};
    context_->ClearRenderTargetView(outputView.Get(), black);

    NVSDK_NGX_D3D11_TRUEHDR_Eval_Params evaluate{};
    evaluate.pInput = hdrInput.Get();
    evaluate.pOutput = output.Get();
    evaluate.InputSubrectTL = {0, 0};
    evaluate.InputSubrectBR = {hdrInputWidth, hdrInputHeight};
    evaluate.OutputSubrectTL = {
        static_cast<unsigned int>(outputRectangle.left),
        static_cast<unsigned int>(outputRectangle.top)};
    evaluate.OutputSubrectBR = {
        static_cast<unsigned int>(outputRectangle.right),
        static_cast<unsigned int>(outputRectangle.bottom)};
    evaluate.Contrast = std::clamp(settings.contrast, 0U, 200U);
    evaluate.Saturation = std::clamp(settings.saturation, 0U, 200U);
    evaluate.MiddleGray = std::clamp(settings.middleGray, 10U, 100U);
    evaluate.MaxLuminance = std::clamp(settings.maxLuminance, 400U, 2000U);

    if (multithread_) multithread_->Enter();
    const NVSDK_NGX_Result result = NGX_D3D11_EVALUATE_TRUEHDR_EXT(
        context_.Get(), hdrFeature_, parameters_, &evaluate);
    if (multithread_) multithread_->Leave();
    if (NVSDK_NGX_FAILED(result)) {
        error = NgxError(L"TrueHDR evaluate", result);
        output.Reset();
        return false;
    }
    return true;
}

void RtxHdrProcessor::Shutdown() noexcept {
    if (vsrFeature_) {
        NVSDK_NGX_D3D11_ReleaseFeature(vsrFeature_);
        vsrFeature_ = nullptr;
    }
    if (hdrFeature_) {
        NVSDK_NGX_D3D11_ReleaseFeature(hdrFeature_);
        hdrFeature_ = nullptr;
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
    adapter_.Reset();
}

std::wstring RtxHdrProcessor::NgxError(
    const wchar_t* operation, const NVSDK_NGX_Result result) {
    std::wostringstream stream;
    stream << operation << L" 失敗 / failed (NGX 0x" << std::hex
           << static_cast<unsigned long long>(result) << L").";
    return stream.str();
}
