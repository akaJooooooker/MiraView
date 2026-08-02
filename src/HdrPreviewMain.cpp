#include "FolderModel.h"
#include "ImageCache.h"
#include "RtxHdrProcessor.h"
#include "WicDecoder.h"

#include <Windows.h>
#include <Windowsx.h>
#include <commdlg.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

namespace {
constexpr wchar_t WindowClassName[] = L"MiraView.HdrPreview";
constexpr WORD AppIconResource = 101;
constexpr DXGI_COLOR_SPACE_TYPE HdrColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

const wchar_t* HdrText(
    const bool english, const wchar_t* traditionalChinese, const wchar_t* englishText) {
    return english ? englishText : traditionalChinese;
}

enum PreviewCommand : UINT {
    CommandStandard = 100,
    CommandVivid,
    CommandGentle,
    CommandPrevious,
    CommandNext,
    CommandFirst,
    CommandLast,
    CommandMaximize,
    CommandFullscreen,
    CommandClose
};

struct HdrMonitor {
    HMONITOR monitor = nullptr;
    RECT workArea{};
    unsigned int maxLuminance = 1000;
    std::wstring deviceName;
};

bool FindHdrMonitor(const std::wstring& preferredDevice, HdrMonitor& result) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    HdrMonitor fallback;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC outputDescription{};
            if (FAILED(output->GetDesc(&outputDescription))) continue;
            ComPtr<IDXGIOutput6> output6;
            if (FAILED(output.As(&output6))) continue;
            DXGI_OUTPUT_DESC1 description{};
            if (FAILED(output6->GetDesc1(&description)) ||
                description.ColorSpace != HdrColorSpace) continue;

            MONITORINFO monitorInfo{sizeof(MONITORINFO)};
            if (!GetMonitorInfoW(outputDescription.Monitor, &monitorInfo)) continue;
            HdrMonitor candidate;
            candidate.monitor = outputDescription.Monitor;
            candidate.workArea = monitorInfo.rcWork;
            candidate.deviceName = outputDescription.DeviceName;
            const float reported = description.MaxLuminance;
            candidate.maxLuminance = static_cast<unsigned int>(std::clamp(
                reported > 0.0F ? std::lround(reported) : 1000L, 400L, 2000L));
            if (!preferredDevice.empty() && candidate.deviceName == preferredDevice) {
                result = std::move(candidate);
                return true;
            }
            if (!fallback.monitor) fallback = std::move(candidate);
        }
    }
    if (!fallback.monitor) return false;
    result = std::move(fallback);
    return true;
}

std::filesystem::path SelectImage(const bool english) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(OPENFILENAMEW)};
    dialog.lpstrFilter = english
        ? L"Image files\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp;*.heic;*.avif;*.jxr;*.gif\0All files\0*.*\0"
        : L"圖片檔案\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp;*.heic;*.avif;*.jxr;*.gif\0所有檔案\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path{};
}
}

class HdrPreviewApp final {
public:
    int Run(HINSTANCE instance, const std::filesystem::path& imagePath,
            const std::wstring& preferredMonitor, const bool selfTest,
            const bool english) {
        english_ = english;
        if (!folder_.Open(imagePath)) {
            if (!selfTest) {
                MessageBoxW(nullptr,
                    HdrText(english_, L"無法索引圖片所在資料夾。", L"Could not index the image folder."),
                    L"MiraView RTX VSR + HDR", MB_OK | MB_ICONERROR);
            }
            return 1;
        }
        HdrMonitor monitor;
        if (!FindHdrMonitor(preferredMonitor, monitor)) {
            if (!selfTest) {
                MessageBoxW(nullptr,
                    HdrText(english_,
                        L"找不到已啟用 Windows HDR 的螢幕。\n\n請到「設定 > 系統 > 顯示器 > HDR」開啟 HDR 後再試一次。",
                        L"No display with Windows HDR enabled was found.\n\nEnable HDR under Settings > System > Display > HDR and try again."),
                    HdrText(english_, L"MiraView RTX 視訊增強", L"MiraView RTX Video Enhancement"),
                    MB_OK | MB_ICONWARNING);
            }
            return 2;
        }

        WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(AppIconResource));
        windowClass.hIconSm = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(AppIconResource), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        windowClass.lpszClassName = WindowClassName;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 3;

        const LONG availableWidth = monitor.workArea.right - monitor.workArea.left;
        const LONG availableHeight = monitor.workArea.bottom - monitor.workArea.top;
        const int width = std::min<LONG>(900, std::max<LONG>(640, availableWidth - 160));
        const int height = std::min<LONG>(600, std::max<LONG>(480, availableHeight - 160));
        window_ = CreateWindowExW(
            0, WindowClassName, L"MiraView RTX VSR + HDR", WS_OVERLAPPEDWINDOW,
            monitor.workArea.left + (availableWidth - width) / 2,
            monitor.workArea.top + (availableHeight - height) / 2,
            width, height, nullptr, nullptr, instance, this);
        if (!window_) return 4;
        mainMenu_ = CreateMenuBar();
        if (mainMenu_) SetMenu(window_, mainMenu_);
        cache_.SetNotificationWindow(window_);
        maxLuminance_ = monitor.maxLuminance;
        monitorName_ = monitor.deviceName;

        std::wstring error;
        if (!processor_.Initialize(error) || !CreateSwapChain(error)) {
            if (!selfTest) {
                MessageBoxW(window_, error.c_str(),
                    HdrText(english_, L"MiraView RTX 視訊增強", L"MiraView RTX Video Enhancement"),
                    MB_OK | MB_ICONERROR);
            }
            DestroyWindow(window_);
            return 5;
        }

        if (selfTest) {
            image_ = WicDecoder::Decode(folder_.Current().wstring(), error);
            if (!image_) {
                DestroyWindow(window_);
                return 6;
            }
            UpdateTitle();
            const bool rendered = Render(error);
            DestroyWindow(window_);
            return rendered ? 0 : 7;
        }
        loading_ = true;
        UpdateTitle();
        ScheduleNeighborhood();
        ApplyCurrentIfReady();
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    HMENU CreateMenuBar() {
        HMENU root = CreateMenu();
        navigateMenu_ = CreatePopupMenu();
        presetMenu_ = CreatePopupMenu();
        viewMenu_ = CreatePopupMenu();
        if (!root || !navigateMenu_ || !presetMenu_ || !viewMenu_) return root;
        AppendMenuW(navigateMenu_, MF_STRING, CommandPrevious,
            HdrText(english_, L"上一張\t←", L"Previous\t←"));
        AppendMenuW(navigateMenu_, MF_STRING, CommandNext,
            HdrText(english_, L"下一張\t→", L"Next\t→"));
        AppendMenuW(navigateMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(navigateMenu_, MF_STRING, CommandFirst,
            HdrText(english_, L"第一張\tHome", L"First\tHome"));
        AppendMenuW(navigateMenu_, MF_STRING, CommandLast,
            HdrText(english_, L"最後一張\tEnd", L"Last\tEnd"));
        AppendMenuW(presetMenu_, MF_STRING, CommandStandard,
            HdrText(english_, L"標準\t1", L"Standard\t1"));
        AppendMenuW(presetMenu_, MF_STRING, CommandVivid,
            HdrText(english_, L"鮮明\t2", L"Vivid\t2"));
        AppendMenuW(presetMenu_, MF_STRING, CommandGentle,
            HdrText(english_, L"柔和\t3", L"Gentle\t3"));
        AppendMenuW(viewMenu_, MF_STRING, CommandMaximize,
            HdrText(english_, L"視窗最大化\tM", L"Maximize window\tM"));
        AppendMenuW(viewMenu_, MF_STRING, CommandFullscreen,
            HdrText(english_, L"無邊框全螢幕\tF11", L"Borderless fullscreen\tF11"));
        AppendMenuW(viewMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu_, MF_STRING, CommandClose,
            HdrText(english_, L"關閉\tEsc", L"Close\tEsc"));
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(navigateMenu_),
            HdrText(english_, L"瀏覽", L"Navigate"));
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(presetMenu_),
            HdrText(english_, L"HDR 預設", L"HDR Preset"));
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu_),
            HdrText(english_, L"顯示", L"View"));
        AppendMenuW(root, MF_STRING | MF_DISABLED, 0, L"RTX VSR Ultra → TrueHDR 10-bit");
        UpdateMenuChecks();
        return root;
    }

    void UpdateMenuChecks() {
        if (presetMenu_) {
            CheckMenuRadioItem(presetMenu_, CommandStandard, CommandGentle,
                CommandStandard + preset_, MF_BYCOMMAND);
        }
        if (viewMenu_) {
            CheckMenuItem(viewMenu_, CommandMaximize,
                MF_BYCOMMAND | (IsZoomed(window_) && !fullscreen_ ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(viewMenu_, CommandFullscreen,
                MF_BYCOMMAND | (fullscreen_ ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    void ScheduleNeighborhood() {
        if (folder_.Empty()) return;
        cache_.ClearQueued();
        std::vector<std::wstring> pinned;
        const auto current = static_cast<long long>(folder_.Index());
        const auto count = static_cast<long long>(folder_.Size());
        cache_.Request(folder_.Current().wstring(), 1000);
        pinned.push_back(folder_.Current().wstring());
        for (int distance = 1; distance <= 4; ++distance) {
            const int priority = 900 - distance * 80;
            const long long next = current + distance;
            const long long previous = current - distance;
            if (next < count) {
                cache_.Request(folder_.At(static_cast<std::size_t>(next)).wstring(), priority);
                pinned.push_back(folder_.At(static_cast<std::size_t>(next)).wstring());
            }
            if (previous >= 0) {
                cache_.Request(folder_.At(static_cast<std::size_t>(previous)).wstring(), priority - 1);
                pinned.push_back(folder_.At(static_cast<std::size_t>(previous)).wstring());
            }
        }
        cache_.Pin(pinned);
    }

    void ApplyCurrentIfReady() {
        if (folder_.Empty()) return;
        const auto path = folder_.Current().wstring();
        auto image = cache_.TryGet(path);
        if (!image) {
            const auto error = cache_.ErrorFor(path);
            if (!error.empty()) {
                loading_ = false;
                loadError_ = error;
                UpdateTitle();
            }
            return;
        }
        if (image_ && image_->path == image->path) return;
        image_ = std::move(image);
        loading_ = false;
        loadError_.clear();
        lastUsedVsr_ = false;
        hdrTexture_.Reset();
        UpdateTitle();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void Navigate(const int delta) {
        if (!folder_.Move(delta)) return;
        image_.reset();
        hdrTexture_.Reset();
        loading_ = true;
        loadError_.clear();
        lastUsedVsr_ = false;
        ScheduleNeighborhood();
        ApplyCurrentIfReady();
        UpdateTitle();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void NavigateTo(const std::size_t index) {
        if (!folder_.MoveTo(index)) return;
        image_.reset();
        hdrTexture_.Reset();
        loading_ = true;
        loadError_.clear();
        lastUsedVsr_ = false;
        ScheduleNeighborhood();
        ApplyCurrentIfReady();
        UpdateTitle();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void SetPreset(const int preset) {
        preset_ = std::clamp(preset, 0, 2);
        hdrTexture_.Reset();
        UpdateMenuChecks();
        UpdateTitle();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void ToggleMaximized() {
        if (fullscreen_) ToggleFullscreen();
        ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
        UpdateMenuChecks();
    }

    void ToggleFullscreen() {
        if (!fullscreen_) {
            windowStyle_ = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
            windowPlacement_.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(window_, &windowPlacement_);
            MONITORINFO monitor{sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
            SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_ & ~WS_OVERLAPPEDWINDOW);
            SetMenu(window_, nullptr);
            SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left,
                monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            fullscreen_ = true;
        } else {
            SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_);
            SetMenu(window_, mainMenu_);
            SetWindowPlacement(window_, &windowPlacement_);
            SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                SWP_NOZORDER | SWP_NOOWNERZORDER);
            fullscreen_ = false;
        }
        UpdateMenuChecks();
        hdrTexture_.Reset();
        InvalidateRect(window_, nullptr, FALSE);
    }

    static LRESULT CALLBACK WindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        HdrPreviewApp* app = reinterpret_cast<HdrPreviewApp*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<HdrPreviewApp*>(create->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(window, message, wParam, lParam);
        return app->HandleMessage(message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case ImageCache::ImageReadyMessage:
            ApplyCurrentIfReady();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window_, &paint);
            std::wstring error;
            if (!Render(error) && !error.empty()) {
                EndPaint(window_, &paint);
                MessageBoxW(window_, error.c_str(),
                    HdrText(english_, L"RTX 視訊增強錯誤", L"RTX Video Enhancement error"),
                    MB_OK | MB_ICONERROR);
                return 0;
            }
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                hdrTexture_.Reset();
                InvalidateRect(window_, nullptr, FALSE);
            }
            UpdateMenuChecks();
            return 0;
        case WM_LBUTTONDBLCLK:
            ToggleFullscreen();
            return 0;
        case WM_MOUSEWHEEL: {
            const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            if (steps != 0) Navigate(-steps);
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (fullscreen_) ToggleFullscreen();
                else DestroyWindow(window_);
                return 0;
            }
            if (wParam == VK_F11) {
                ToggleFullscreen();
                return 0;
            }
            if (wParam == L'M') {
                ToggleMaximized();
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_PRIOR || wParam == VK_BACK) {
                Navigate(-1);
                return 0;
            }
            if (wParam == VK_RIGHT || wParam == VK_NEXT || wParam == VK_SPACE) {
                Navigate(1);
                return 0;
            }
            if (wParam == VK_HOME) {
                if (!folder_.Empty()) NavigateTo(0);
                return 0;
            }
            if (wParam == VK_END) {
                if (!folder_.Empty()) NavigateTo(folder_.Size() - 1);
                return 0;
            }
            if (wParam >= L'1' && wParam <= L'3') {
                SetPreset(static_cast<int>(wParam - L'1'));
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case CommandStandard: SetPreset(0); break;
            case CommandVivid: SetPreset(1); break;
            case CommandGentle: SetPreset(2); break;
            case CommandPrevious: Navigate(-1); break;
            case CommandNext: Navigate(1); break;
            case CommandFirst: if (!folder_.Empty()) NavigateTo(0); break;
            case CommandLast: if (!folder_.Empty()) NavigateTo(folder_.Size() - 1); break;
            case CommandMaximize: ToggleMaximized(); break;
            case CommandFullscreen: ToggleFullscreen(); break;
            case CommandClose: DestroyWindow(window_); break;
            default: break;
            }
            return 0;
        case WM_DISPLAYCHANGE:
            hdrTexture_.Reset();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            cache_.SetNotificationWindow(nullptr);
            swapChain_.Reset();
            hdrTexture_.Reset();
            processor_.Shutdown();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    bool CreateSwapChain(std::wstring& error) {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        HRESULT hr = processor_.Device()->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        if (SUCCEEDED(hr)) hr = dxgiDevice->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            error = L"無法取得 HDR DXGI factory。 / Could not obtain the HDR DXGI factory.";
            return false;
        }

        RECT client{};
        GetClientRect(window_, &client);
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = std::max<LONG>(1, client.right);
        description.Height = std::max<LONG>(1, client.bottom);
        description.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> swapChain1;
        hr = factory->CreateSwapChainForHwnd(
            processor_.Device(), window_, &description, nullptr, nullptr, &swapChain1);
        if (SUCCEEDED(hr)) hr = swapChain1.As(&swapChain_);
        if (FAILED(hr)) {
            error = L"無法建立 10-bit HDR swap chain。 / Could not create the 10-bit HDR swap chain.";
            return false;
        }
        factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);

        UINT support = 0;
        hr = swapChain_->CheckColorSpaceSupport(HdrColorSpace, &support);
        if (FAILED(hr) || (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
            FAILED(swapChain_->SetColorSpace1(HdrColorSpace))) {
            error = L"此螢幕或 Windows 顯示模式不接受 HDR10 / Rec.2020。 / "
                    L"This display mode does not accept HDR10 / Rec.2020.";
            swapChain_.Reset();
            return false;
        }
        return true;
    }

    RtxHdrSettings CurrentSettings() const {
        RtxHdrSettings settings;
        settings.maxLuminance = maxLuminance_;
        if (preset_ == 1) {
            settings.contrast = 110;
            settings.saturation = 120;
            settings.middleGray = 55;
        } else if (preset_ == 2) {
            settings.contrast = 92;
            settings.saturation = 95;
            settings.middleGray = 45;
        }
        return settings;
    }

    bool Render(std::wstring& error) {
        if (!swapChain_) return true;
        RECT client{};
        GetClientRect(window_, &client);
        const UINT width = static_cast<UINT>(std::max<LONG>(1, client.right));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, client.bottom));

        DXGI_SWAP_CHAIN_DESC1 description{};
        swapChain_->GetDesc1(&description);
        if (description.Width != width || description.Height != height) {
            hdrTexture_.Reset();
            HRESULT hr = swapChain_->ResizeBuffers(
                2, width, height, DXGI_FORMAT_R10G10B10A2_UNORM, 0);
            if (FAILED(hr)) {
                error = L"調整 HDR swap chain 大小失敗。 / Failed to resize the HDR swap chain.";
                return false;
            }
            if (FAILED(swapChain_->SetColorSpace1(HdrColorSpace))) {
                error = L"重新設定 HDR10 色彩空間失敗。 / Failed to restore the HDR10 color space.";
                return false;
            }
        }

        if (!image_) {
            ComPtr<ID3D11Texture2D> emptyBuffer;
            HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&emptyBuffer));
            ComPtr<ID3D11RenderTargetView> emptyView;
            if (SUCCEEDED(hr)) {
                hr = processor_.Device()->CreateRenderTargetView(
                    emptyBuffer.Get(), nullptr, &emptyView);
            }
            if (FAILED(hr)) {
                error = L"無法清除 HDR 載入畫面。 / Could not clear the HDR loading frame.";
                return false;
            }
            constexpr float black[]{0.0F, 0.0F, 0.0F, 1.0F};
            processor_.Context()->ClearRenderTargetView(emptyView.Get(), black);
            return SUCCEEDED(swapChain_->Present(1, 0));
        }

        if (!hdrTexture_) {
            const double scale = std::min(
                static_cast<double>(width) / image_->width,
                static_cast<double>(height) / image_->height);
            const LONG imageWidth = std::max<LONG>(1, static_cast<LONG>(std::lround(image_->width * scale)));
            const LONG imageHeight = std::max<LONG>(1, static_cast<LONG>(std::lround(image_->height * scale)));
            const RECT outputRectangle{
                (static_cast<LONG>(width) - imageWidth) / 2,
                (static_cast<LONG>(height) - imageHeight) / 2,
                (static_cast<LONG>(width) + imageWidth) / 2,
                (static_cast<LONG>(height) + imageHeight) / 2};
            bool usedVsr = false;
            if (!processor_.Process(
                    *image_, width, height, outputRectangle,
                    CurrentSettings(), hdrTexture_, error, true, &usedVsr)) return false;
            if (usedVsr != lastUsedVsr_) {
                lastUsedVsr_ = usedVsr;
                UpdateTitle();
            }
        }

        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            error = L"無法取得 HDR back buffer。 / Could not obtain the HDR back buffer.";
            return false;
        }
        processor_.Context()->CopyResource(backBuffer.Get(), hdrTexture_.Get());
        hr = swapChain_->Present(1, 0);
        if (FAILED(hr)) {
            error = L"呈現 HDR 畫面失敗。 / Failed to present the HDR frame.";
            return false;
        }
        return true;
    }

    void UpdateTitle() {
        static constexpr const wchar_t* chinesePresetNames[]{L"標準", L"鮮明", L"柔和"};
        static constexpr const wchar_t* englishPresetNames[]{L"Standard", L"Vivid", L"Gentle"};
        std::wstring title;
        if (loading_) title = HdrText(english_, L"正在載入 — ", L"Loading — ");
        else if (!loadError_.empty()) title = HdrText(english_, L"載入失敗 — ", L"Load failed — ");
        else if (lastUsedVsr_) title = L"RTX VSR Ultra → TrueHDR 10-bit — ";
        else title = HdrText(english_,
            L"RTX TrueHDR 10-bit（原圖不需放大）— ",
            L"RTX TrueHDR 10-bit (source does not need upscaling) — ");
        title += english_ ? englishPresetNames[preset_] : chinesePresetNames[preset_];
        title += L" — ";
        title += folder_.Empty() ? L"MiraView" : folder_.Current().filename().wstring();
        if (!folder_.Empty()) {
            title += L" — ";
            title += std::to_wstring(folder_.Index() + 1);
            title += L"/";
            title += std::to_wstring(folder_.Size());
        }
        title += L" — ";
        title += std::to_wstring(maxLuminance_);
        title += L" nits — ";
        title += monitorName_;
        title += HdrText(english_,
            L" — ←/→ 或滾輪翻頁，M 最大化，F11 全螢幕",
            L" — ←/→ or wheel pages, M maximize, F11 fullscreen");
        SetWindowTextW(window_, title.c_str());
    }

    HWND window_ = nullptr;
    HMENU mainMenu_ = nullptr;
    HMENU navigateMenu_ = nullptr;
    HMENU presetMenu_ = nullptr;
    HMENU viewMenu_ = nullptr;
    std::shared_ptr<ImageData> image_;
    FolderModel folder_;
    ImageCache cache_{384ULL * 1024ULL * 1024ULL};
    RtxHdrProcessor processor_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D11Texture2D> hdrTexture_;
    unsigned int maxLuminance_ = 1000;
    std::wstring monitorName_;
    int preset_ = 0;
    bool loading_ = false;
    bool english_ = false;
    std::wstring loadError_;
    bool lastUsedVsr_ = false;
    bool fullscreen_ = false;
    WINDOWPLACEMENT windowPlacement_{sizeof(WINDOWPLACEMENT)};
    DWORD windowStyle_ = 0;
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::filesystem::path imagePath;
    std::wstring preferredMonitor;
    bool selfTest = false;
    bool english = false;
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    for (int index = 1; arguments && index < argumentCount; ++index) {
        if (std::wstring_view(arguments[index]) == L"--monitor" && index + 1 < argumentCount) {
            preferredMonitor = arguments[++index];
        } else if (std::wstring_view(arguments[index]) == L"--self-test") {
            selfTest = true;
        } else if (std::wstring_view(arguments[index]) == L"--lang" && index + 1 < argumentCount) {
            english = std::wstring_view(arguments[++index]) == L"en";
        } else if (imagePath.empty()) {
            imagePath = arguments[index];
        }
    }
    if (arguments) LocalFree(arguments);
    if (imagePath.empty()) imagePath = SelectImage(english);
    if (imagePath.empty()) {
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    HdrPreviewApp app;
    const int result = app.Run(instance, imagePath, preferredMonitor, selfTest, english);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return result;
}
