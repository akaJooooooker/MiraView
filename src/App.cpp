#include "App.h"

#include "NullImageEnhancer.h"
#if MIRAVIEW_WITH_RTX
#include "RtxImageEnhancer.h"
#endif

#include <Windowsx.h>
#include <d2d1helper.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t WindowClassName[] = L"MiraView.MainWindow";
constexpr wchar_t ProductName[] = L"MiraView";
constexpr WORD AppIconResource = 101;
constexpr UINT_PTR NoticeTimer = 1;
constexpr UINT_PTR EnhancementResizeTimer = 2;

enum Command : UINT {
    CommandOpen = 100,
    CommandPrevious,
    CommandNext,
    CommandFit,
    CommandFitWidth,
    CommandActual,
    CommandFullscreen,
    CommandToggleInfo,
    CommandRtx,
    CommandWheelNavigate,
    CommandWheelZoom,
    CommandFirst,
    CommandLast,
    CommandAbout,
    CommandExit
};

std::wstring ViewModeName(const ViewMode mode) {
    switch (mode) {
    case ViewMode::FitWidth: return L"適合寬度";
    case ViewMode::ActualSize: return L"原始大小";
    default: return L"適合視窗";
    }
}

D2D1_RECT_F ClientRectF(const HWND window) {
    RECT rectangle{};
    GetClientRect(window, &rectangle);
    return D2D1::RectF(0.0F, 0.0F, static_cast<float>(rectangle.right), static_cast<float>(rectangle.bottom));
}
}

App::App() : cache_(768ULL * 1024ULL * 1024ULL) {
#if MIRAVIEW_WITH_RTX
    enhancer_ = std::make_unique<RtxImageEnhancer>();
#else
    enhancer_ = std::make_unique<NullImageEnhancer>();
#endif
}

App::~App() = default;

int App::Run(const HINSTANCE instance, const std::filesystem::path& initialFile) {
    if (!CreateDeviceIndependentResources() || !CreateMainWindow(instance)) {
        MessageBoxW(nullptr, L"無法初始化 MiraView。", ProductName, MB_OK | MB_ICONERROR);
        return 1;
    }

    LoadSettings();
    UpdateMenuChecks();
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    if (!initialFile.empty()) OpenImage(initialFile);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool App::CreateMainWindow(const HINSTANCE instance) {
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(AppIconResource));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(AppIconResource), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!windowClass.hIconSm) windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    window_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, WindowClassName, ProductName, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820, nullptr, nullptr, instance, this);
    if (!window_) return false;
    mainMenu_ = CreateApplicationMenu();
    if (mainMenu_) SetMenu(window_, mainMenu_);
    DragAcceptFiles(window_, TRUE);
    cache_.SetNotificationWindow(window_);
    enhancementWorker_ = std::make_unique<EnhancementWorker>(*enhancer_, window_);
    return true;
}

HMENU App::CreateApplicationMenu() {
    HMENU root = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    HMENU navigateMenu = CreatePopupMenu();
    viewMenu_ = CreatePopupMenu();
    wheelMenu_ = CreatePopupMenu();
    rtxMenu_ = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();
    if (!root || !fileMenu || !navigateMenu || !viewMenu_ || !wheelMenu_ || !rtxMenu_ || !helpMenu) return root;

    AppendMenuW(fileMenu, MF_STRING, CommandOpen, L"開啟圖片…\tO");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, CommandExit, L"結束\tAlt+F4");

    AppendMenuW(navigateMenu, MF_STRING, CommandPrevious, L"上一張\t←");
    AppendMenuW(navigateMenu, MF_STRING, CommandNext, L"下一張\t→");
    AppendMenuW(navigateMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(navigateMenu, MF_STRING, CommandFirst, L"第一張\tHome");
    AppendMenuW(navigateMenu, MF_STRING, CommandLast, L"最後一張\tEnd");

    AppendMenuW(viewMenu_, MF_STRING, CommandFit, L"適合視窗\t1");
    AppendMenuW(viewMenu_, MF_STRING, CommandFitWidth, L"適合寬度\t2");
    AppendMenuW(viewMenu_, MF_STRING, CommandActual, L"原始大小（100%）\t3");
    AppendMenuW(viewMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu_, MF_STRING, CommandFullscreen, L"全螢幕\tF11");
    AppendMenuW(viewMenu_, MF_STRING, CommandToggleInfo, L"顯示資訊列\tI");
    AppendMenuW(viewMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(wheelMenu_, MF_STRING, CommandWheelNavigate, L"上一張／下一張");
    AppendMenuW(wheelMenu_, MF_STRING, CommandWheelZoom, L"放大／縮小");
    AppendMenuW(viewMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(wheelMenu_), L"滑鼠滾輪");

    AppendMenuW(rtxMenu_, MF_STRING, CommandRtx, L"啟用 RTX Video VSR\tR");
    AppendMenuW(rtxMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(rtxMenu_, MF_STRING | MF_DISABLED, 0, L"品質：Ultra（4）");
    AppendMenuW(rtxMenu_, MF_STRING | MF_DISABLED, 0, L"輸出：依顯示尺寸（最高 7680 px）");

    AppendMenuW(helpMenu, MF_STRING, CommandAbout, L"關於 MiraView / About MiraView…");

    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"檔案(&F)");
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(navigateMenu), L"瀏覽(&N)");
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu_), L"檢視(&V)");
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(rtxMenu_), L"RTX(&R)");
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"說明(&H)");
    UpdateMenuChecks();
    return root;
}

void App::UpdateMenuChecks() {
    if (viewMenu_) {
        CheckMenuRadioItem(viewMenu_, CommandFit, CommandActual,
            viewMode_ == ViewMode::FitWindow ? CommandFit :
            viewMode_ == ViewMode::FitWidth ? CommandFitWidth : CommandActual,
            MF_BYCOMMAND);
        CheckMenuItem(viewMenu_, CommandFullscreen,
            MF_BYCOMMAND | (fullscreen_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(viewMenu_, CommandToggleInfo,
            MF_BYCOMMAND | (showInfo_ ? MF_CHECKED : MF_UNCHECKED));
    }
    if (wheelMenu_) {
        CheckMenuRadioItem(wheelMenu_, CommandWheelNavigate, CommandWheelZoom,
            wheelBehavior_ == WheelBehavior::Navigate ? CommandWheelNavigate : CommandWheelZoom,
            MF_BYCOMMAND);
    }
    if (rtxMenu_) {
        CheckMenuItem(rtxMenu_, CommandRtx,
            MF_BYCOMMAND | (enhancementEnabled_ ? MF_CHECKED : MF_UNCHECKED));
    }
    if (window_ && IsWindow(window_)) DrawMenuBar(window_);
}

bool App::CreateDeviceIndependentResources() {
    if (FAILED(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())))) return false;

    if (FAILED(writeFactory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"zh-TW", &textFormat_))) return false;
    if (FAILED(writeFactory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 17.0F, L"zh-TW", &titleFormat_))) return false;
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

bool App::EnsureRenderTarget() {
    if (renderTarget_) return true;
    RECT rectangle{};
    GetClientRect(window_, &rectangle);
    const auto pixelSize = D2D1::SizeU(
        static_cast<UINT32>(std::max(1L, rectangle.right)),
        static_cast<UINT32>(std::max(1L, rectangle.bottom)));
    if (FAILED(d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(window_, pixelSize), &renderTarget_))) return false;

    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xF4F6F8), &textBrush_);
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xAAB2BD), &mutedBrush_);
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x57A9FF), &accentBrush_);
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x11151A, 0.88F), &overlayBrush_);
    UploadCurrentBitmap();
    return true;
}

void App::DiscardRenderTarget() {
    bitmap_.Reset();
    textBrush_.Reset();
    mutedBrush_.Reset();
    accentBrush_.Reset();
    overlayBrush_.Reset();
    renderTarget_.Reset();
}

void App::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!EnsureRenderTarget()) {
        EndPaint(window_, &paint);
        return;
    }

    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->Clear(D2D1::ColorF(0x0B0E12));
    const D2D1_SIZE_F size = renderTarget_->GetSize();

    if (bitmap_ && currentImage_) {
        const auto destination = ImageRectangle(size);
        renderTarget_->DrawBitmap(
            bitmap_.Get(), destination, 1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            D2D1::RectF(0.0F, 0.0F, bitmap_->GetSize().width, bitmap_->GetSize().height));
    } else {
        const std::wstring primary = loading_ ? L"正在載入圖片…" : L"拖曳圖片到這裡，或按 O 開啟";
        const std::wstring secondary = folder_.Empty()
            ? L"開啟後會自動索引同一資料夾，並在背景預讀前後頁"
            : L"圖片解碼中；預讀完成後翻頁會直接顯示";
        auto primaryRect = D2D1::RectF(40.0F, size.height * 0.5F - 36.0F, size.width - 40.0F, size.height * 0.5F);
        auto secondaryRect = D2D1::RectF(40.0F, size.height * 0.5F + 2.0F, size.width - 40.0F, size.height * 0.5F + 36.0F);
        titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        renderTarget_->DrawTextW(primary.c_str(), static_cast<UINT32>(primary.size()), titleFormat_.Get(), primaryRect, textBrush_.Get());
        renderTarget_->DrawTextW(secondary.c_str(), static_cast<UINT32>(secondary.size()), textFormat_.Get(), secondaryRect, mutedBrush_.Get());
        titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    DrawOverlay(size);
    const HRESULT result = renderTarget_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) DiscardRenderTarget();
    EndPaint(window_, &paint);
}

void App::DrawOverlay(const D2D1_SIZE_F& size) {
    if (showInfo_ && !folder_.Empty()) {
        renderTarget_->FillRectangle(D2D1::RectF(0.0F, 0.0F, size.width, 48.0F), overlayBrush_.Get());
        renderTarget_->FillRectangle(D2D1::RectF(0.0F, size.height - 38.0F, size.width, size.height), overlayBrush_.Get());

        const std::wstring filename = folder_.Current().filename().wstring();
        renderTarget_->DrawTextW(filename.c_str(), static_cast<UINT32>(filename.size()), titleFormat_.Get(),
            D2D1::RectF(18.0F, 12.0F, size.width - 180.0F, 40.0F), textBrush_.Get());

        std::wostringstream counter;
        counter << (folder_.Index() + 1) << L" / " << folder_.Size();
        const auto counterText = counter.str();
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        renderTarget_->DrawTextW(counterText.c_str(), static_cast<UINT32>(counterText.size()), textFormat_.Get(),
            D2D1::RectF(size.width - 170.0F, 15.0F, size.width - 18.0F, 40.0F), accentBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        std::wostringstream details;
        details << ViewModeName(viewMode_) << L"  ·  " << static_cast<int>(std::lround(CurrentScale(size) * 100.0F)) << L"%";
        if (currentImage_) details << L"  ·  " << currentImage_->width << L" × " << currentImage_->height;
        if (enhancementEnabled_) {
            if (enhancementInProgress_) details << L"  ·  RTX 處理中";
            else if (enhancedImage_) details << L"  ·  RTX " << enhancedImage_->width << L" × " << enhancedImage_->height;
            else details << L"  ·  RTX 開啟";
        }
        details << L"  ·  ←/→ 翻頁  Ctrl+滾輪縮放  F11 全螢幕  I 隱藏資訊";
        const auto detailsText = details.str();
        renderTarget_->DrawTextW(detailsText.c_str(), static_cast<UINT32>(detailsText.size()), textFormat_.Get(),
            D2D1::RectF(18.0F, size.height - 29.0F, size.width - 18.0F, size.height - 5.0F), mutedBrush_.Get());
    }

    if (!notice_.empty() && GetTickCount64() < noticeUntil_) {
        const float width = std::min(size.width - 40.0F, 680.0F);
        const float left = (size.width - width) * 0.5F;
        const auto box = D2D1::RectF(left, size.height * 0.72F, left + width, size.height * 0.72F + 52.0F);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(box, 8.0F, 8.0F), overlayBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        renderTarget_->DrawTextW(notice_.c_str(), static_cast<UINT32>(notice_.size()), textFormat_.Get(), box, textBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

void App::ShowOpenDialog() {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    const COMDLG_FILTERSPEC filters[] = {
        {L"圖片", L"*.jpg;*.jpeg;*.jpe;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic;*.heif;*.avif;*.jxl;*.ico;*.jxr"},
        {L"所有檔案", L"*.*"}
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetTitle(L"開啟圖片");
    if (FAILED(dialog->Show(window_))) return;

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) OpenImage(path);
    CoTaskMemFree(path);
}

bool App::OpenImage(const std::filesystem::path& file) {
    if (!folder_.Open(file)) {
        SetNotice(L"找不到圖片，或檔案格式不在支援清單中。", 5000);
        return false;
    }
    currentImage_.reset();
    enhancedImage_.reset();
    ++enhancementGeneration_;
    bitmap_.Reset();
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    loading_ = true;
    ScheduleNeighborhood();
    ApplyCurrentIfReady();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void App::Navigate(const int delta) {
    if (!folder_.Move(delta)) return;
    currentImage_.reset();
    enhancedImage_.reset();
    ++enhancementGeneration_;
    bitmap_.Reset();
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    loading_ = true;
    ScheduleNeighborhood();
    ApplyCurrentIfReady();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::NavigateTo(const std::size_t index) {
    if (!folder_.MoveTo(index)) return;
    currentImage_.reset();
    enhancedImage_.reset();
    ++enhancementGeneration_;
    bitmap_.Reset();
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    loading_ = true;
    ScheduleNeighborhood();
    ApplyCurrentIfReady();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ScheduleNeighborhood() {
    if (folder_.Empty()) return;
    cache_.ClearQueued();
    std::vector<std::wstring> pinned;
    pinned.reserve(17);
    const auto current = static_cast<long long>(folder_.Index());
    const auto count = static_cast<long long>(folder_.Size());
    cache_.Request(folder_.Current().wstring(), 1000);
    pinned.push_back(folder_.Current().wstring());
    for (int distance = 1; distance <= 8; ++distance) {
        const int priority = 900 - distance * 50;
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

void App::ApplyCurrentIfReady() {
    if (folder_.Empty()) return;
    const auto path = folder_.Current().wstring();
    auto image = cache_.TryGet(path);
    if (!image) {
        const auto error = cache_.ErrorFor(path);
        if (!error.empty()) {
            loading_ = false;
            SetNotice(L"解碼失敗：" + error, 6000);
        }
        return;
    }
    if (currentImage_ && currentImage_->path == image->path) return;
    currentImage_ = std::move(image);
    loading_ = false;
    UploadCurrentBitmap();
    if (enhancementEnabled_) RequestEnhancement();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::RequestEnhancement() {
    if (!enhancementEnabled_ || !enhancementWorker_ || !currentImage_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const D2D1_SIZE_F size = D2D1::SizeF(
        static_cast<float>(std::max(1L, client.right)),
        static_cast<float>(std::max(1L, client.bottom)));
    float scale = CurrentScale(size);
    if (scale <= 1.05F) {
        enhancementInProgress_ = false;
        enhancedImage_.reset();
        UploadCurrentBitmap();
        UpdateWindowTitle();
        return;
    }

    constexpr float maximumDimension = 7680.0F;
    float targetWidth = static_cast<float>(currentImage_->width) * scale;
    float targetHeight = static_cast<float>(currentImage_->height) * scale;
    if (std::max(targetWidth, targetHeight) > maximumDimension) {
        const float limitScale = maximumDimension / std::max(targetWidth, targetHeight);
        targetWidth *= limitScale;
        targetHeight *= limitScale;
    }
    const auto width = static_cast<std::uint32_t>(std::max(
        static_cast<float>(currentImage_->width + 1U), std::round(targetWidth)));
    const auto height = static_cast<std::uint32_t>(std::max(
        static_cast<float>(currentImage_->height + 1U), std::round(targetHeight)));
    enhancementInProgress_ = true;
    enhancedImage_.reset();
    UploadCurrentBitmap();
    UpdateWindowTitle();
    const std::uint64_t generation = ++enhancementGeneration_;
    enhancementWorker_->Request(currentImage_, width, height, generation);
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ApplyEnhancementResult() {
    if (!enhancementWorker_) return;
    auto result = enhancementWorker_->TakeResult();
    if (result.generation == 0 || result.generation != enhancementGeneration_ ||
        !enhancementEnabled_ || folder_.Empty()) return;
    enhancementInProgress_ = false;
    if (!result.image) {
        if (!enhancer_->IsAvailable()) {
            enhancementEnabled_ = false;
            UpdateMenuChecks();
        }
        SetNotice(result.error.empty() ? L"RTX VSR 處理失敗。" : result.error, 7000);
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (!currentImage_ || result.image->path != currentImage_->path) return;
    enhancedImage_ = std::move(result.image);
    UploadCurrentBitmap();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ToggleEnhancement() {
    if (enhancementEnabled_) {
        enhancementEnabled_ = false;
        enhancementInProgress_ = false;
        enhancedImage_.reset();
        ++enhancementGeneration_;
        UploadCurrentBitmap();
        UpdateWindowTitle();
        UpdateMenuChecks();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (!enhancer_->IsAvailable()) {
        SetNotice(enhancer_->Status(), 6000);
        return;
    }
    enhancementEnabled_ = true;
    UpdateMenuChecks();
    RequestEnhancement();
}

void App::UploadCurrentBitmap() {
    bitmap_.Reset();
    const auto image = enhancementEnabled_ && enhancedImage_ ? enhancedImage_ : currentImage_;
    if (!renderTarget_ || !image || image->pixels.empty()) return;
    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const HRESULT result = renderTarget_->CreateBitmap(
        D2D1::SizeU(image->width, image->height), image->pixels.data(),
        image->stride, properties, &bitmap_);
    if (FAILED(result)) SetNotice(L"無法將圖片上傳到 Direct2D。", 5000);
}

void App::SetViewMode(const ViewMode mode) {
    viewMode_ = mode;
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    enhancedImage_.reset();
    ++enhancementGeneration_;
    UploadCurrentBitmap();
    if (enhancementEnabled_) RequestEnhancement();
    UpdateMenuChecks();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::SetWheelBehavior(const WheelBehavior behavior) {
    wheelBehavior_ = behavior;
    UpdateMenuChecks();
}

void App::ZoomAt(const float factor, const POINT clientPoint) {
    if (!currentImage_ || factor <= 0.0F) return;
    const D2D1_SIZE_F size = renderTarget_ ? renderTarget_->GetSize() : D2D1::SizeF(
        ClientRectF(window_).right, ClientRectF(window_).bottom);
    const float oldScale = CurrentScale(size);
    const float oldZoom = zoomFactor_;
    zoomFactor_ = std::clamp(zoomFactor_ * factor, 0.02F, 50.0F);
    const float newScale = CurrentScale(size);
    if (oldScale <= 0.0F || std::abs(oldZoom - zoomFactor_) < 0.0001F) return;
    const D2D1_POINT_2F center{size.width * 0.5F, size.height * 0.5F};
    const D2D1_POINT_2F point{static_cast<float>(clientPoint.x), static_cast<float>(clientPoint.y)};
    const D2D1_POINT_2F imagePoint{
        (point.x - center.x - pan_.x) / oldScale,
        (point.y - center.y - pan_.y) / oldScale
    };
    pan_.x = point.x - center.x - imagePoint.x * newScale;
    pan_.y = point.y - center.y - imagePoint.y * newScale;
    InvalidateRect(window_, nullptr, FALSE);
}

float App::BaseScale(const D2D1_SIZE_F& clientSize) const {
    if (!currentImage_ || currentImage_->width == 0 || currentImage_->height == 0) return 1.0F;
    const float widthScale = clientSize.width / static_cast<float>(currentImage_->width);
    const float heightScale = clientSize.height / static_cast<float>(currentImage_->height);
    switch (viewMode_) {
    case ViewMode::FitWidth: return widthScale;
    case ViewMode::ActualSize: return 1.0F;
    default: return std::min(widthScale, heightScale);
    }
}

float App::CurrentScale(const D2D1_SIZE_F& clientSize) const {
    return BaseScale(clientSize) * zoomFactor_;
}

D2D1_RECT_F App::ImageRectangle(const D2D1_SIZE_F& clientSize) const {
    if (!currentImage_) return D2D1::RectF();
    const float scale = CurrentScale(clientSize);
    const float width = static_cast<float>(currentImage_->width) * scale;
    const float height = static_cast<float>(currentImage_->height) * scale;
    const float left = (clientSize.width - width) * 0.5F + pan_.x;
    const float top = (clientSize.height - height) * 0.5F + pan_.y;
    return D2D1::RectF(left, top, left + width, top + height);
}

void App::ToggleFullscreen() {
    if (!fullscreen_) {
        windowStyle_ = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
        windowPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(window_, &windowPlacement_);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_ & ~WS_OVERLAPPEDWINDOW);
        SetMenu(window_, nullptr);
        SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        fullscreen_ = true;
    } else {
        SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_);
        SetMenu(window_, mainMenu_);
        SetWindowPlacement(window_, &windowPlacement_);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        fullscreen_ = false;
    }
    UpdateMenuChecks();
}

void App::ShowContextMenu(const POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CommandOpen, L"開啟圖片…\tO");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandPrevious, L"上一張\t←");
    AppendMenuW(menu, MF_STRING, CommandNext, L"下一張\t→");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (viewMode_ == ViewMode::FitWindow ? MF_CHECKED : 0), CommandFit, L"適合視窗\t1");
    AppendMenuW(menu, MF_STRING | (viewMode_ == ViewMode::FitWidth ? MF_CHECKED : 0), CommandFitWidth, L"適合寬度\t2");
    AppendMenuW(menu, MF_STRING | (viewMode_ == ViewMode::ActualSize ? MF_CHECKED : 0), CommandActual, L"原始大小\t3");
    AppendMenuW(menu, MF_STRING | (fullscreen_ ? MF_CHECKED : 0), CommandFullscreen, L"全螢幕\tF11");
    AppendMenuW(menu, MF_STRING | (showInfo_ ? MF_CHECKED : 0), CommandToggleInfo, L"資訊列\tI");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (enhancementEnabled_ ? MF_CHECKED : 0), CommandRtx, L"RTX 增強\tR");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandExit, L"結束");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, window_, nullptr);
    DestroyMenu(menu);
}

void App::SetNotice(std::wstring text, const DWORD milliseconds) {
    notice_ = std::move(text);
    noticeUntil_ = GetTickCount64() + milliseconds;
    SetTimer(window_, NoticeTimer, std::min<DWORD>(milliseconds, 1000), nullptr);
    InvalidateRect(window_, nullptr, FALSE);
}

void App::UpdateWindowTitle() {
    std::wstring title = ProductName;
    if (!folder_.Empty()) {
        title = folder_.Current().filename().wstring();
        if (enhancementEnabled_ && enhancedImage_) title += L" [RTX]";
        title += L" — " + std::wstring(ProductName);
    }
    SetWindowTextW(window_, title.c_str());
}

std::filesystem::path App::SettingsPath() const {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) return {};
    std::filesystem::path path(localAppData);
    CoTaskMemFree(localAppData);
    return path / L"MiraView" / L"settings.ini";
}

void App::LoadSettings() {
    const auto path = SettingsPath();
    if (path.empty()) return;
    const auto native = path.wstring();
    showInfo_ = GetPrivateProfileIntW(L"view", L"showInfo", 1, native.c_str()) != 0;
    const int mode = GetPrivateProfileIntW(L"view", L"mode", 0, native.c_str());
    if (mode >= 0 && mode <= 2) viewMode_ = static_cast<ViewMode>(mode);
    const int wheel = GetPrivateProfileIntW(L"input", L"wheelBehavior", 0, native.c_str());
    if (wheel >= 0 && wheel <= 1) wheelBehavior_ = static_cast<WheelBehavior>(wheel);
    const int left = GetPrivateProfileIntW(L"window", L"left", INT_MIN, native.c_str());
    const int top = GetPrivateProfileIntW(L"window", L"top", INT_MIN, native.c_str());
    const int width = GetPrivateProfileIntW(L"window", L"width", 0, native.c_str());
    const int height = GetPrivateProfileIntW(L"window", L"height", 0, native.c_str());
    if (left != INT_MIN && top != INT_MIN && width >= 640 && height >= 480) {
        RECT proposed{left, top, left + width, top + height};
        if (MonitorFromRect(&proposed, MONITOR_DEFAULTTONULL)) MoveWindow(window_, left, top, width, height, FALSE);
    }
}

void App::SaveSettings() const {
    const auto path = SettingsPath();
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto native = path.wstring();
    RECT rectangle{};
    if (fullscreen_) rectangle = windowPlacement_.rcNormalPosition;
    else GetWindowRect(window_, &rectangle);
    const auto write = [&](const wchar_t* section, const wchar_t* key, const int value) {
        const auto text = std::to_wstring(value);
        WritePrivateProfileStringW(section, key, text.c_str(), native.c_str());
    };
    write(L"window", L"left", rectangle.left);
    write(L"window", L"top", rectangle.top);
    write(L"window", L"width", rectangle.right - rectangle.left);
    write(L"window", L"height", rectangle.bottom - rectangle.top);
    write(L"view", L"showInfo", showInfo_ ? 1 : 0);
    write(L"view", L"mode", static_cast<int>(viewMode_));
    write(L"input", L"wheelBehavior", static_cast<int>(wheelBehavior_));
}

LRESULT CALLBACK App::WindowProcedure(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    const LRESULT result = app->HandleMessage(message, wParam, lParam);
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return result;
}

LRESULT App::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (renderTarget_) renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        if (enhancementEnabled_ && wParam != SIZE_MINIMIZED && currentImage_) {
            SetTimer(window_, EnhancementResizeTimer, 250, nullptr);
        }
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        const auto* rectangle = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window_, nullptr, rectangle->left, rectangle->top,
            rectangle->right - rectangle->left, rectangle->bottom - rectangle->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case ImageCache::ImageReadyMessage:
        ApplyCurrentIfReady();
        return 0;
    case EnhancementWorker::EnhancementReadyMessage:
        ApplyEnhancementResult();
        return 0;
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0) OpenImage(path);
        DragFinish(drop);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;
    case WM_LBUTTONDOWN:
        dragging_ = true;
        dragOrigin_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        panOrigin_ = pan_;
        SetCapture(window_);
        return 0;
    case WM_MOUSEMOVE:
        if (dragging_) {
            pan_.x = panOrigin_.x + static_cast<float>(GET_X_LPARAM(lParam) - dragOrigin_.x);
            pan_.y = panOrigin_.y + static_cast<float>(GET_Y_LPARAM(lParam) - dragOrigin_.y);
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        dragging_ = false;
        return 0;
    case WM_MOUSEWHEEL: {
        const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        const bool controlZoom = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
        if (wheelBehavior_ == WheelBehavior::Zoom || controlZoom) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window_, &point);
            ZoomAt(std::pow(1.15F, static_cast<float>(steps)), point);
        } else if (steps != 0) {
            Navigate(-steps);
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            RECT rectangle{};
            GetWindowRect(window_, &rectangle);
            point = {(rectangle.left + rectangle.right) / 2, (rectangle.top + rectangle.bottom) / 2};
        }
        ShowContextMenu(point);
        return 0;
    }
    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_LEFT: case VK_PRIOR: case VK_BACK: Navigate(-1); return 0;
        case VK_RIGHT: case VK_NEXT: case VK_SPACE: Navigate(1); return 0;
        case VK_HOME: if (!folder_.Empty()) NavigateTo(0); return 0;
        case VK_END: if (!folder_.Empty()) NavigateTo(folder_.Size() - 1); return 0;
        case VK_F11: ToggleFullscreen(); return 0;
        case VK_ESCAPE: if (fullscreen_) ToggleFullscreen(); return 0;
        case VK_ADD: case VK_OEM_PLUS: {
            RECT rectangle{}; GetClientRect(window_, &rectangle);
            ZoomAt(1.15F, POINT{rectangle.right / 2, rectangle.bottom / 2}); return 0;
        }
        case VK_SUBTRACT: case VK_OEM_MINUS: {
            RECT rectangle{}; GetClientRect(window_, &rectangle);
            ZoomAt(1.0F / 1.15F, POINT{rectangle.right / 2, rectangle.bottom / 2}); return 0;
        }
        case L'O': ShowOpenDialog(); return 0;
        case L'I': showInfo_ = !showInfo_; UpdateMenuChecks(); InvalidateRect(window_, nullptr, FALSE); return 0;
        case L'R': ToggleEnhancement(); return 0;
        case L'1': SetViewMode(ViewMode::FitWindow); return 0;
        case L'2': SetViewMode(ViewMode::FitWidth); return 0;
        case L'3': SetViewMode(ViewMode::ActualSize); return 0;
        case L'0': SetViewMode(ViewMode::FitWindow); return 0;
        default: break;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case CommandOpen: ShowOpenDialog(); break;
        case CommandPrevious: Navigate(-1); break;
        case CommandNext: Navigate(1); break;
        case CommandFirst: if (!folder_.Empty()) NavigateTo(0); break;
        case CommandLast: if (!folder_.Empty()) NavigateTo(folder_.Size() - 1); break;
        case CommandFit: SetViewMode(ViewMode::FitWindow); break;
        case CommandFitWidth: SetViewMode(ViewMode::FitWidth); break;
        case CommandActual: SetViewMode(ViewMode::ActualSize); break;
        case CommandFullscreen: ToggleFullscreen(); break;
        case CommandToggleInfo: showInfo_ = !showInfo_; UpdateMenuChecks(); InvalidateRect(window_, nullptr, FALSE); break;
        case CommandRtx: ToggleEnhancement(); break;
        case CommandWheelNavigate: SetWheelBehavior(WheelBehavior::Navigate); break;
        case CommandWheelZoom: SetWheelBehavior(WheelBehavior::Zoom); break;
        case CommandAbout:
            MessageBoxW(window_,
                L"MiraView 0.3.0\n\n"
                L"【繁體中文】\n"
                L"高速原生 Windows 圖片檢視器，支援 NVIDIA RTX Video VSR。\n"
                L"VSR 會依目前顯示尺寸輸出，最高單邊 7680 像素。\n"
                L"使用 NVIDIA RTX Video SDK 1.1。\n"
                L"本專案不代表受到 NVIDIA 贊助或背書。\n\n"
                L"[English]\n"
                L"Fast native Windows image viewer with NVIDIA RTX Video VSR.\n"
                L"VSR output follows the current display size, up to 7680 pixels per side.\n"
                L"Built with NVIDIA RTX Video SDK 1.1.\n"
                L"This project is not sponsored or endorsed by NVIDIA.",
                L"關於 MiraView / About MiraView", MB_OK | MB_ICONINFORMATION);
            break;
        case CommandExit: DestroyWindow(window_); break;
        default: break;
        }
        return 0;
    case WM_TIMER:
        if (wParam == EnhancementResizeTimer) {
            KillTimer(window_, EnhancementResizeTimer);
            if (enhancementEnabled_ && currentImage_) RequestEnhancement();
            return 0;
        }
        if (wParam == NoticeTimer && GetTickCount64() >= noticeUntil_) {
            KillTimer(window_, NoticeTimer);
            notice_.clear();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        SaveSettings();
        cache_.SetNotificationWindow(nullptr);
        enhancementWorker_.reset();
        if (mainMenu_) {
            SetMenu(window_, nullptr);
            DestroyMenu(mainMenu_);
            mainMenu_ = nullptr;
            viewMenu_ = nullptr;
            wheelMenu_ = nullptr;
            rtxMenu_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
