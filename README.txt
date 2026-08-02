MiraView 0.3.0
================

繁體中文
--------

簡介
MiraView 是一款原生 64 位元 Windows 圖片檢視器，目標是逐步成為可長期取代
FastStone 的高速看圖工具。程式支援資料夾自然排序、背景預讀、縮放、平移、
全螢幕，以及 NVIDIA RTX Video VSR 圖片增強。

安裝與啟動
1. 將 MiraView-0.3.0-win-x64.zip 完整解壓縮到同一個資料夾。
2. 執行 MiraView.exe。
3. 按 O 開啟圖片、把圖片拖進視窗，或在命令列把圖片路徑傳給 MiraView.exe。
4. RTX 功能需要 nvngx_vsr.dll 與 MiraView.exe 放在同一個資料夾。

Windows SmartScreen 可能因為此版本尚未使用商業程式碼簽章而顯示警告。
請只從本專案的官方 GitHub Release 下載；若確認來源正確，可選擇「其他資訊」
再按「仍要執行」。

支援格式
- 內建：JPG/JPEG、PNG、BMP、GIF 第一幀、TIFF、ICO、JPEG XR。
- WebP、HEIC、AVIF 等格式取決於 Windows 是否已安裝對應的 WIC codec。

操作說明
- O：開啟圖片。
- 左方向鍵 / Page Up / Backspace：上一張。
- 右方向鍵 / Page Down / Space：下一張。
- Home / End：第一張 / 最後一張。
- 1：適合視窗。
- 2：適合寬度。
- 3：原始大小（100%）。
- + / -：放大 / 縮小。
- 滑鼠左鍵拖曳：平移圖片。
- F11 或雙擊：切換全螢幕。
- I：顯示或隱藏資訊列。
- R：開啟或關閉 NVIDIA RTX Video VSR。
- 右鍵：開啟快速功能選單。

滑鼠滾輪模式
從「檢視 -> 滑鼠滾輪」選擇：
- 「上一張／下一張」：滾輪用來翻頁。
- 「放大／縮小」：滾輪以游標位置為中心縮放。
選擇會自動保存。無論目前選擇哪一種，Ctrl + 滾輪都會強制縮放。

一般使用門檻
- 64 位元 Windows 10 或更新版本。
- 可正常執行 Direct2D 與 WIC 的顯示環境。
- 一般圖片檢視功能不需要 NVIDIA RTX 顯示卡。

RTX Video VSR 使用門檻
- GeForce RTX 20 系列或更新，或 NVIDIA RTX 1000 系列或更新的 GPU。
- 支援 RTX Video SDK 的 NVIDIA 驅動程式；SDK 1.1 文件標示最低 550.58。
- MiraView 必須實際使用 NVIDIA RTX GPU。雙顯卡筆電請在 Windows 圖形設定或
  NVIDIA App 將 MiraView.exe 設為「高效能」。
- nvngx_vsr.dll 必須與 MiraView.exe 位於同一資料夾。
- NVIDIA 控制面板或 NVIDIA App 裡的「影片／視訊增強」不必開啟。MiraView
  直接呼叫 RTX Video SDK；硬體、驅動、runtime 與 RTX GPU 條件仍不可少。
- 只有圖片需要放大時才會執行 VSR。輸出依目前顯示尺寸計算，4K 顯示可產生
  接近 3840 x 2160 的輸出，最高單邊 7680 像素。

目前限制
- GIF 與動態 WebP 目前只顯示第一幀。
- 尚未包含縮圖瀏覽器、資料夾樹、CBZ、雙頁模式與完整 ICC/HDR 色彩管理。
- 第一次啟用 RTX 需要初始化 NGX，可能需數秒。

第三方軟體
MiraView 使用 NVIDIA RTX Video SDK 1.1。NVIDIA runtime 仍受隨附的 NVIDIA
RTX SDKs LICENSE 約束，並不改採 MiraView 的授權。詳見 THIRD_PARTY_NOTICES.md
與 LICENSES 資料夾。NVIDIA 及相關商標屬 NVIDIA Corporation；本專案不代表
受到 NVIDIA 贊助或背書。


English
-------

Overview
MiraView is a native 64-bit Windows image viewer designed to grow into a fast,
long-term alternative to FastStone. It provides natural folder sorting,
background prefetching, zoom and pan, fullscreen viewing, and optional NVIDIA
RTX Video VSR image enhancement.

Install and start
1. Extract the entire MiraView-0.3.0-win-x64.zip archive into one folder.
2. Run MiraView.exe.
3. Press O to open an image, drag an image into the window, or pass an image
   path to MiraView.exe on the command line.
4. Keep nvngx_vsr.dll beside MiraView.exe to use RTX enhancement.

Windows SmartScreen may warn because this release does not yet have a commercial
code-signing certificate. Download only from the official GitHub Release for
this project. If the source is correct, choose "More info" and "Run anyway."

Supported formats
- Built in: JPG/JPEG, PNG, BMP, first frame of GIF, TIFF, ICO, and JPEG XR.
- WebP, HEIC, AVIF, and similar formats depend on installed Windows WIC codecs.

Controls
- O: Open an image.
- Left / Page Up / Backspace: Previous image.
- Right / Page Down / Space: Next image.
- Home / End: First / last image.
- 1: Fit window.
- 2: Fit width.
- 3: Actual size (100%).
- + / -: Zoom in / out.
- Left mouse drag: Pan the image.
- F11 or double-click: Toggle fullscreen.
- I: Show or hide the information bar.
- R: Enable or disable NVIDIA RTX Video VSR.
- Right-click: Open the quick action menu.

Mouse wheel mode
Choose one under View -> Mouse Wheel:
- Previous/Next: Use the wheel to navigate images.
- Zoom In/Out: Zoom around the pointer position.
The selection is saved automatically. Ctrl + wheel always zooms in either mode.

General requirements
- 64-bit Windows 10 or later.
- A display environment capable of running Direct2D and WIC.
- Normal image viewing does not require an NVIDIA RTX GPU.

RTX Video VSR requirements
- GeForce RTX 20 series or newer, or NVIDIA RTX 1000 series or newer.
- A compatible NVIDIA driver; the SDK 1.1 documentation lists 550.58 minimum.
- MiraView must run on the NVIDIA RTX GPU. On dual-GPU laptops, select the
  high-performance NVIDIA GPU for MiraView.exe in Windows Graphics settings or
  the NVIDIA App.
- nvngx_vsr.dll must remain beside MiraView.exe.
- The Video Enhancement switch in NVIDIA Control Panel or the NVIDIA App does
  not need to be enabled. MiraView calls the RTX Video SDK directly; compatible
  hardware, driver, runtime, and use of the RTX GPU are still required.
- VSR runs only when an image needs enlargement. Output follows the current
  display size, can approach 3840 x 2160 on a 4K display, and is capped at 7680
  pixels per side.

Current limitations
- GIF and animated WebP currently display only the first frame.
- Thumbnail browser, folder tree, CBZ, two-page mode, and complete ICC/HDR color
  management are not implemented yet.
- The first RTX activation initializes NGX and may take several seconds.

Third-party software
MiraView uses NVIDIA RTX Video SDK 1.1. The NVIDIA runtime remains governed by
the included NVIDIA RTX SDKs LICENSE and is not relicensed under MiraView terms.
See THIRD_PARTY_NOTICES.md and the LICENSES directory. NVIDIA and related marks
belong to NVIDIA Corporation. This project is not sponsored or endorsed by
NVIDIA.

Project: https://github.com/akaJooooooker/MiraView
