# MiraView

MiraView 是以「高速漫畫／圖片檢視器，逐步長期取代 FastStone」為目標的原生 Windows 專案。第一版先把最重要的體感打好：開啟任意圖片後，自動索引同資料夾，依自然檔名排序，並在背景預先解碼前後各 8 張圖片。

目前版本：**0.3 高速檢視核心＋RTX Video VSR**。

## 已完成

- 原生 C++20／Win32 應用程式，不需 .NET Runtime。
- WIC 解碼 JPG、PNG、BMP、GIF 第一幀、TIFF、ICO、JPEG XR，以及系統已安裝 codec 所支援的 WebP／HEIC／AVIF 等格式。
- 讀取 EXIF Orientation，自動旋轉 JPEG。
- 開啟一張圖後自動索引同資料夾。
- Windows 邏輯式自然排序：`1, 2, 3, 10`，不會排成 `1, 10, 2, 3`。
- 4 條背景解碼工作執行緒。
- 前後各 8 張的方向感知預讀，下一張優先於上一張。
- 768 MB LRU 記憶體快取；眼前頁面與鄰近頁面固定保留。
- 快速連按翻頁時清除過時的排隊工作，眼前頁面永遠是最高優先。
- Direct2D 顯示、適合視窗、適合寬度、100%、滑鼠拖曳與游標定位縮放。
- 原生 Windows 選單列（檔案、瀏覽、檢視、RTX、說明）、右鍵選單與拖放開圖。
- 專屬 MiraView 應用程式圖示，已嵌入 EXE 與視窗標題列。
- 全螢幕、頁碼／尺寸／縮放資訊列；全螢幕時自動隱藏選單列。
- 滑鼠滾輪可選擇「上一張／下一張」或「放大／縮小」。
- 記住視窗位置、顯示模式、資訊列狀態與滾輪模式。
- 已整合 NVIDIA RTX Video SDK 1.1 的 D3D11 VSR 後端。
- `R` 鍵開關 RTX；只在圖片需要放大時執行，固定使用 Ultra（品質 4）。
- RTX 在獨立背景執行緒處理；翻頁不等 AI，舊頁結果也不會覆蓋新頁。
- RTX 成功套用後，視窗標題會顯示 `[RTX]`。
- VSR 輸出依實際顯示尺寸計算；4K 全螢幕會自動重算到接近 3840×2160，最高單邊 7680 像素。
- 切圖時不顯示「處理中／已套用」浮層，底部資訊列會顯示實際 RTX 輸出解析度。

## 執行

Release 執行檔位於：

```text
out\MiraView.exe
```

可直接啟動後拖入圖片，也可以把圖片路徑當成第一個命令列參數：

```powershell
.\out\MiraView.exe "D:\Manga\001.jpg"
```

## 操作

| 操作 | 功能 |
|---|---|
| `O` | 開啟圖片 |
| `←` / `Page Up` / `Backspace` | 上一張 |
| `→` / `Page Down` / `Space` | 下一張 |
| 滑鼠滾輪 | 依「檢視 → 滑鼠滾輪」設定翻頁或縮放 |
| `Ctrl` + 滾輪、`+` / `-` | 不受上述設定影響，強制以游標／畫面中心縮放 |
| 滑鼠左鍵拖曳 | 平移圖片 |
| `1` | 適合視窗 |
| `2` | 適合寬度 |
| `3` | 原始大小（100%） |
| `Home` / `End` | 第一張／最後一張 |
| `F11` 或雙擊 | 切換全螢幕 |
| `I` | 顯示／隱藏資訊列 |
| `R` | 開啟／關閉 RTX Video VSR |
| 右鍵 | 功能選單 |

## RTX 使用門檻

- 64 位元 Windows 10 或更新版本。
- GeForce RTX 20 系列或更新，或 NVIDIA RTX 1000 系列或更新的 GPU。
- 支援該顯示卡與 RTX Video SDK 的 NVIDIA 驅動程式；本機 SDK 1.1 文件列出的最低版本是 550.58。
- MiraView 必須實際在 NVIDIA RTX GPU 上執行；雙顯卡筆電可在 Windows 圖形設定或 NVIDIA App 將 MiraView 指定為高效能 NVIDIA GPU。
- RTX 增強版旁邊必須有 `nvngx_vsr.dll`；只有一般檢視功能時不需要 RTX 顯示卡。

MiraView 是直接呼叫 RTX Video SDK，而不是借用瀏覽器的驅動程式增強開關。因此達到上述門檻後，即使 NVIDIA 控制面板／NVIDIA App 裡的「影片／視訊增強」沒有開啟，MiraView 的 `R` 鍵 RTX 功能仍可使用。該開關主要控制 NVIDIA 驅動替支援的瀏覽器或播放器所做的增強；硬體、驅動、SDK runtime 與程式使用 NVIDIA GPU 的條件仍然不可少。

官方參考：[RTX Video SDK Getting Started](https://developer.nvidia.com/rtx-video-sdk/getting-started)、[NVIDIA 控制面板影片影像設定說明](https://www.nvidia.com/content/Control-Panel-Help/vLatest/en-us/mergedProjects/Display/Reference_Adjust_Video_Image_Settings.htm)。

## 建置

需求：Visual Studio 2022，並安裝「使用 C++ 的桌面開發」工作負載與 Windows SDK。

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
```

RTX 版還需要把 SDK 1.1 解壓到 `.sdk/rtx-video-1.1.0`，或在設定時指定：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DMIRAVIEW_RTX_SDK_ROOT="D:\SDK\RTX_Video_SDK_v1.1.0"
```

設定成功時 CMake 會顯示 `MiraView RTX backend: enabled`，並自動把 Release 版 `nvngx_vsr.dll` 複製到執行檔旁。沒有 SDK 時仍可建置一般檢視版。

也可用 Visual Studio 直接開啟此資料夾或 `CMakeLists.txt`。

## 目前限制

- GIF／動態 WebP 目前只顯示第一幀。
- WebP、HEIC、AVIF、JPEG XL 是否可開啟取決於 Windows 已安裝的 WIC codec。
- 尚未有縮圖瀏覽器、資料夾樹、CBZ、雙頁模式、檔案管理、ICC／HDR 完整色彩管理。
- 目前 RTX 輸出會讀回 CPU 再交給 Direct2D；下一階段將改為 D3D11 texture 零拷貝顯示。
- 第一次開啟 RTX 需初始化 NGX，實測約數秒；後續圖片較快。
- 目前 VSR 結果只保留在記憶體，尚未建立磁碟快取。

## NVIDIA SDK 告知

MiraView 使用 NVIDIA RTX Video SDK。SDK 與 runtime 受 NVIDIA RTX SDKs License 約束，並非本專案的一般原始碼資產。詳見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

更詳細的設計與路線見 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 和 [docs/RTX_INTEGRATION.md](docs/RTX_INTEGRATION.md)。
