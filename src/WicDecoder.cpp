#include "WicDecoder.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <new>

using Microsoft::WRL::ComPtr;

namespace {
WICBitmapTransformOptions ReadOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader))) return WICBitmapTransformRotate0;

    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = reader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value);
    if (FAILED(result)) {
        PropVariantClear(&value);
        return WICBitmapTransformRotate0;
    }

    unsigned short orientation = 1;
    if (value.vt == VT_UI2) orientation = value.uiVal;
    else if (value.vt == VT_UI4) orientation = static_cast<unsigned short>(value.ulVal);
    PropVariantClear(&value);

    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

std::wstring HResultMessage(const HRESULT result) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = count > 0 && buffer ? std::wstring(buffer, count) : L"未知的解碼錯誤";
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message;
}
}

std::shared_ptr<ImageData> WicDecoder::Decode(const std::wstring& path, std::wstring& error) {
    error.clear();
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(comResult);

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        result = CoCreateInstance(
            CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(result)) {
        result = factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);

    ComPtr<IWICBitmapSource> source = frame;
    ComPtr<IWICBitmapFlipRotator> rotator;
    if (SUCCEEDED(result)) {
        const auto orientation = ReadOrientation(frame.Get());
        if (orientation != WICBitmapTransformRotate0) {
            result = factory->CreateBitmapFlipRotator(&rotator);
            if (SUCCEEDED(result)) {
                result = rotator->Initialize(frame.Get(), orientation);
                source = rotator;
            }
        }
    }

    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) {
        result = converter->Initialize(
            source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(result)) result = converter->GetSize(&width, &height);

    const std::uint64_t stride64 = static_cast<std::uint64_t>(width) * 4ULL;
    const std::uint64_t size64 = stride64 * static_cast<std::uint64_t>(height);
    if (SUCCEEDED(result) && (width == 0 || height == 0 ||
        stride64 > std::numeric_limits<UINT>::max() || size64 > std::numeric_limits<UINT>::max())) {
        result = WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    }

    std::shared_ptr<ImageData> image;
    if (SUCCEEDED(result)) {
        try {
            image = std::make_shared<ImageData>();
            image->path = path;
            image->width = width;
            image->height = height;
            image->stride = static_cast<std::uint32_t>(stride64);
            image->pixels.resize(static_cast<std::size_t>(size64));
            result = converter->CopyPixels(
                nullptr, image->stride, static_cast<UINT>(image->pixels.size()),
                reinterpret_cast<BYTE*>(image->pixels.data()));
        } catch (const std::bad_alloc&) {
            result = E_OUTOFMEMORY;
        }
    }

    if (FAILED(result)) {
        image.reset();
        error = HResultMessage(result);
    }
    if (uninitialize) CoUninitialize();
    return image;
}

