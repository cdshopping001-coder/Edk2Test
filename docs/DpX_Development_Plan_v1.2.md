# DpX.efi 開發與交付計畫（Development & Delivery Plan）

> 文件版本：1.2（FV Total/Used/Free 為必要交付；取代 v1.1 作為開發基線）｜建立日：2026-10-19  
> 專案：`cdshopping001-coder/Edk2Test`｜需求追蹤：[Issue #3](https://github.com/cdshopping001-coder/Edk2Test/issues/3)、[FV Issue #5](https://github.com/cdshopping001-coder/Edk2Test/issues/5)  
> 狀態：**開發規格與驗收計畫；非已完成的程式功能**

## 1. 產品定位與交付目標

**DpX.efi = UEFI Shell Firmware Performance Profiler + Reliable System Inventory + Multi-Boot Benchmark Collector + Read-Only Firmware Volume Analyzer。**

基於 TianoCore Dp 2.5（`edk2-stable202608`）的可驗證原始碼，提供可在公開 EDK2/OVMF 上開發、建置及測試的獨立 UEFI Application；實機資料收集應以讀取為主，預設不修改任何 BIOS Setup、NVRAM、BootOrder 或韌體。

主要使用情境：

1. 單次開機：將 FPDT 與 FBPT 取得、驗證、完整保留，列出 Phase、Top N、異常記錄、時間空檔。
2. 多次開機：輸入 N 次，在每次**不同的開機**擷取一份 Snapshot，累積到有耐故障能力的 Session，產生分布統計。
3. Platform / BIOS 比較：例如 Intel RVP、不同 BIOS 或同平台的 DRB/DPF，保留可比性條件與缺失資訊。
4. 離線分析：匯出可交由 Windows Host Tool 分析的 lossless raw data 與 JSON/CSV；搭配同版 MAP / Build Report 實現 GUID/RVA 對應。
5. **FV 分析（必要容量統計）**：對每個 FV 列出 TOTAL/USED/FREE SIZE、Usage%、計算來源與狀態；UEFI Raw FV 不可讀時允許 N/A/原因，但正式交付必須含離線 Host FV/ROM 容量計算工具。由 FV2 列出 FFS/Section，與 FPDT Module GUID 關聯。詳細規格：[DpX_FV_Analyzer_Spec_v1.1.md](DpX_FV_Analyzer_Spec_v1.1.md)。

**非目標（v1.0）**：逆向不存在的 Performance Records、只靠 FPDT 重建精確 Call Stack、擷取同次 Shell 開機後尚未執行的 Windows OS Loader 時間、控制電源硬斷/真正 G3 Cycle、修改平台 BIOS、判定私有 Intel/Dell Feature 或聲稱所有 FPDT 時間能無條件相加。

## 2. 已確認的開發基線

| 項目 | 基線 / 限制 |
|---|---|
| GitHub | [Edk2Test](https://github.com/cdshopping001-coder/Edk2Test)，`main` |
| edk2 | `edk2-stable202608`，Git commit `2970e5699ba6267f3384ffab20f96647578aebc8` |
| edk2-platforms | 獨立固定 Git snapshot，僅特定平台需要 |
| 原版 DP | `edk2/ShellPkg/DynamicCommand/DpDynamicCommand/`，Dp 2.5 |
| Toolchain | Windows VS2022 / Linux GCC；X64 優先 |
| OVMF 驗證 | [GitHub Actions Build #10](https://github.com/cdshopping001-coder/Edk2Test/actions/runs/39148178616)：雙平台成功 |
| DpX 功能 | 尚未實作，需另建 `CommonToolsPkg` 與 CI |

將原版 DP 當作**行為參考與 regression baseline**。如複製原始碼進 DpX，保留上游著作權、SPDX 與來源記錄；不直接修改 Git submodule。維持原版 `Dp.efi` 和新 `DpX.efi` 可並存。

## 3. 版本交付邊界

| 版本 / Gate | 必備交付 | 不包含 |
|---|---|---|
| M0 規格與測試資料 | 文件、命令契約、JSON schema、golden FPDT fixtures、測試設計 | EFI 可執行檔 |
| M1 / DpX 0.1 | 獨立 INF/DSC、基本 Help、原版 DP 功能基線、X64 CI Build、OVMF 啟動測試 | 多次重開機 |
| M2 / DpX 0.2 | Memory-safe FPDT/FBPT Parser、Raw Export、`-validate`、資料來源/可信度標記 | 自動重開機 |
| M3 / DpX 0.3 | `-sysinfo`、單次 Snapshot、JSON/CSV、敏感資料遮蔽、基本 Top N/Phase | Session |
| M4 / DpX 0.4 | 手動跨開機 Session、N 次保存、耐故障 Manifest、統計與 Session Compare | 自動 Reset |
| M5 / DpX 0.5 | Opt-in Auto Reset、`startup.nsh` 使用範例、Loop Guard、異常停止 | 物理斷電控制 |
| M6 / DpX 1.0 | 測試矩陣全通過、實機驗收、回歸資料、操作手冊、Release、校驗碼 | 未來 Windows Host GUI |
| P1（獨立路線） | Windows Host Timeline、兩版 BIOS 深度差異、MAP/RVA Resolve | 不阻擋 v1.0 |

前一 Gate 未通過，不得宣稱後續 Gate 已完成。M0 是文件交付；M1 起才產生實際 `DpX.efi`。

## 4. 儲存庫與程式架構

```text
Edk2Test/
├─ edk2/                              # upstream pinned Git submodule (不直接修改)
├─ edk2-platforms/                    # upstream pinned Git submodule
├─ CommonToolsPkg/
│  ├─ CommonToolsPkg.dec
│  ├─ CommonToolsPkg.dsc              # 獨立 X64 Build Target
│  ├─ Applications/DpX/
│  │  ├─ DpX.inf
│  │  ├─ DpX.c                        # Shell entry, command dispatch
│  │  ├─ DpX.uni                      # HII help/string
│  │  ├─ Cli/
│  │  ├─ Capture/                     # FPDT / FBPT / optional extensions
│  │  ├─ Parser/                      # bounded record parser
│  │  ├─ PerfAnalysis/               # phase/top/gap/pairing
│  │  ├─ Inventory/                   # UEFI/SMBIOS/ACPI/CPUID/PCI/TCG2
│  │  ├─ FvAnalyzer/                  # FV2/FVB enumeration, FFS/section, GUID correlation
│  │  ├─ Session/                     # state machine / recovery
│  │  ├─ Storage/                     # file IO + checksums
│  │  └─ Export/                      # raw/JSON/CSV
│  └─ Include/
├─ Tools/DpXHost/                     # P1: Windows離線分析、ROM/FV深度解析、MAP符號對應
├─ Tests/DpX/
│  ├─ Fixtures/                       # 人工合成 + 已脫敏樣本
│  ├─ Parser/
│  ├─ Pairing/
│  ├─ Session/
│  ├─ FirmwareVolume/
│  ├─ Statistics/
│  └─ Golden/
├─ scripts/
│  ├─ build-dpx-windows.cmd
│  ├─ build-dpx-linux.sh
│  └─ run-dpx-ovmf.*
├─ docs/
│  ├─ DpX_Development_Plan_v1.0.md
│  ├─ DpX_User_Guide.md
│  ├─ DpX_FV_Analyzer_Spec_v1.1.md
│  ├─ DpX_Data_Format.md
│  └─ DpX_Test_Plan.md
└─ .github/workflows/
   ├─ build-ovmf.yml                 # 已存在，獨立保留
   └─ build-dpx.yml                  # 新建，單獨驗證
```

以 `WORKSPACE=.../Edk2Test/edk2`，`PACKAGES_PATH=.../Edk2Test` 設定外部 Package 搜尋路徑。CLI、Parser、Inventory、Storage/Session 必須模組分離；Host 可重用資料格式與測試向量，不強求在 UEFI 與 Python 間共享所有 C 原始碼。

Build：以 `CommonToolsPkg.dsc` 定義 Shell/UEFI LibraryClasses；優先驗證獨立 `DpX.inf`，避免每次全量重建 OVMF。OVMF 啟動測試作為另外的整合 Gate。最終交付 X64 UEFI PE/COFF `DpX.efi`，保留符號檔與 MAP（若工具鏈產生）。

## 5. 功能規格

### 5.1 FPDT Capture & Integrity（FR-FPDT）

- **FR-F01**：優先由 ACPI FPDT 獲取表格，保留 FPDT ACPI Table 與指向的 FBPT 原始 bytes，獨立匯出（不能只保存含 pointer 的 FPDT 而遺失 FBPT）。如走 EDK2 Extended Firmware Performance configuration table fallback，必須明記來源。
- **FR-F02**：讀取 FPDT/FBPT header 後先驗證 Signature、Revision、Length、checksum（若格式要求）、記憶體範圍、上限與 Record Length；任何指標越界 / 長度 0 / 整數溢位均不得繼續解參考。
- **FR-F03**：未知但長度合法的 Record Type 應以原始 bytes 保留、標記 `unsupported`，不可因未知值丟掉整個 Session；損毀資料則回報錯誤，拒絕冒險解析。
- **FR-F04**：同時保留 FPDT Raw Event 及 Derived Measurement。Start/End 不得覆蓋原始紀錄；以 Progress ID / GUID / Token / Context 配對，支援巢狀、重複與交錯情況，標記 unmatched、ambiguous。
- **FR-F05**：清楚區分 FPDT 定義的**奈秒時間戳**、TimerLib 的**原始 ticks/frequency**、BasicBoot 欄位與其他擴充來源的時間域。不可把 FPDT nanoseconds 當作 ticks 再除頻率。
- **FR-F06**：對 ResetEnd、SEC、PEI、DXE、BDS 顯示可用的獨立時間指標；若缺少有效端點，顯示 `N/A`，不以 0 取代。不能不加驗證把巢狀 Duration 直接相加當作 Wall Clock Boot Time。
- **FR-F07**：提供 incomplete count、invalid count、unknown types、FPDT buffer limitation、source availability、實際可量測範圍。
- **FR-F08**：DpX 只分析先前的 firmware timestamps；CLI 執行、收集與寫檔造成的 overhead 需另記在 collection diagnostics，不能混入 BIOS boot duration。

### 5.2 可靠系統資訊（FR-SYS）

每筆 Inventory Field 必須包含 `value`、`status`（ok/unsupported/missing/invalid/error）、`source`、`confidence`（observed / uefi-reported / firmware-reported / inferred）、`captured_at`、`note`。`inferred` 不得當作已驗證事實。

| 類別 | 優先來源與可保存資料 | 限制 |
|---|---|---|
| BIOS / UEFI | System Table FirmwareVendor/FirmwareRevision、SMBIOS Type 0 | 型號/版本屬韌體提供值 |
| 平台 / 主機板 | SMBIOS Type 1/2、可用的 Board Identifier | 序號/UUID 預設遮蔽 |
| CPU | CPUID Vendor/Family/Model/Stepping/Features、MP Services 可用 CPU count | CPUID Features 不保證功能已啟用 |
| Memory | UEFI GetMemoryMap 各類型描述，SMBIOS Type 17/19 | Memory Map 不是實體 RAM 容量；SMBIOS 為宣告值 |
| PCI / PCIe | EFI_PCI_IO_PROTOCOL GetLocation + Config Read VID/DID/REV/Class | 僅列舉 UEFI 已可見裝置；不主動掃描所有 bus |
| ACPI | RSDP、XSDT/RSDT、FPDT、FADT 等 header/revision/checksum | 僅 checksum 合法不等於內容完全可信 |
| Boot / Secure Boot | BootCurrent、SecureBoot、SetupMode 等可取得 Variables | 不讀取/匯出私鑰、PK/KEK/db/dbx 原值 |
| TPM | EFI_TCG2_PROTOCOL GetCapability，PCR banks/algorithm 支援 | 不匯出 raw TPM event log 或秘密資料 |
| Timer | Performance Property、TimerLib counter props、FPDT time domain | 不混合不同時間基準 |
| Capture Context | DpX version、EDK2 commit、Shell firmware vs OS boot path、Storage ID | 系統時間由 RTC 提供，不能直接視為可靠絕對時間 |

**敏感資訊**：System Serial、UUID、MAC、SMBIOS Asset Tag、Boot Variables 中可能有識別值等預設不輸出；額外資訊必須 opt-in。`Edk2Test` 是公開儲存庫，真機 Snapshot 不應 commit 或自動上傳到公開 CI。

### 5.3 分析功能（FR-AN）

- `summary`：來源、FPDT 状態、計數、Phase、Timer、完整性警告。
- `top N`：Duration 排序，Count / Total / Mean / Max / Min；可依 Phase、Token、Module GUID、Minimum Duration 過濾。
- `tree`：顯示以時間包含推估的視圖；無真實 parent/call-stack 資料時標註 `inferred hierarchy`。
- `gap`：找出未被**目前**記錄覆蓋的區間；標註「缺少 instrumentation 或等待的候選區間」，不得推論某 EC/USB Command 一定在做什麼。
- `compare`：在 Host 或 EFI summary 層比較 Session；保留相同測量鍵的新增/消失/不確定配對；以時間 Delta 顯示 regression。
- `symbol`：`P:A0C98B77.000039C0` 類型可拆出 phase/data1/RVA；完整 GUID、同版 Build MAP 和二進位映像可用時才做精確 Function Resolve。Data1 撞號時標 `ambiguous`，不猜名稱。

### 5.4 Read-Only Firmware Volume Analyzer（FR-FV）

**優先做 Runtime FV2 邏輯列舉，不把 FV2 Logical Size 當作 Physical FV Used Size。** 完整工程規格請見 [FV Analyzer Specification](DpX_FV_Analyzer_Spec_v1.1.md)。

- **FR-FV01**：UEFI Shell 列舉可見的 `EFI_FIRMWARE_VOLUME2_PROTOCOL`，逐個回報 FV Identity/Attributes/Status、FFS File GUID/Type/Logical Size；GetNextFile 缺失、Access Denied 應保留來源與錯誤碼。
- **FR-FV02（必要）**：每個 FV 都須輸出 TOTAL/USED/FREE/Usage%，逐欄標註來源與可驗證程度。TOTAL 以經過驗證的 Header.FvLength 為準；USED/FREE 僅在完整 Raw FV Layout 可安全讀取、解析及會計驗證後輸出，否則必須 N/A/原因。禁止使用 FV2 Logical File Size Sum 冒充 Physical Used。
- **FR-FV03**：可選擇透過 `EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL` 執行只讀資料擷取；FVB memory mapped address 不是原始 SPI offset，不可猜測實體 Flash 位址。嚴禁任何 Write、Erase、SetAttributes、InstallProtocol。
- **FR-FV04**：列出可辨識的 PE32/TE/PEI/DXE/MM DEPEX、UI、Compression、GUID_DEFINED、Nested FV section；原始壓縮 codec 需來自 raw encoding evidence，`ReadSection` 解碼結果不代表 on-flash 壓縮格式。
- **FR-FV05**：FPDT 完整 Module GUID 與 FV/FFS Name GUID 關聯，標示 exact/candidate/ambiguous/not found；相同 GUID 重複或僅 Data1 不能做唯一歸屬。Duration 與 FFS 只是關聯，不能據此推斷耗時因果。
- **FR-FV06**：Benchmark 支援 `-fv-check off|once|each`，預設 `off`，將 FV 執行時間列為 DpX capture overhead，而不是 Boot Phase Duration；`once` 不能保證後續開機無變化。
- **FR-FV07（v1.0 必備）**：Host 能離線讀取 .fv/.fd/.rom/.bin，對可驗證標準 FV 計算 TOTAL/USED/FREE/Usage%，輸出每個 FV JSON/CSV/TXT 與可驗證 Aggregation；私有 IFWI Region 解碼、完整壓縮差異為 P1，不宣稱支援專有/加密內容。
- **FR-FV08**：解析 malformed、FFS2/FFS3 large file、alignment、erase polarity、deleted FFS、未知 guided/compression、nested parser 深度/大小限制；匿名 golden fixtures 與 fuzz tests 必備。


- **FR-FV09（必要）**：`TOTAL=FV Header.FvLength`，`FREE` 僅含原始 FV 中可驗證、未配置的 erase-state bytes，`USED=TOTAL-FREE`；USED 包含 header、活躍 FFS physical extents、PAD FFS、alignment/reserved space、已刪除但未回收區域。所有空間須唯一分類且 sum == TOTAL；有未知或解析失敗範圍時不得宣稱精確 Used/Free。
- **FR-FV10（必要）**：提供 FV per-volume 報表與多 FV Aggregate。Nested FV 與母 FV 重疊映像不重複加總；部分/無法驗證的 FV 不默默加入總計。`usage_percent` 衍生計算；完整單位 bytes，另顯示 KiB/MiB。
- **FR-FV11（必要）**：`-fv-space` 與 `-fv-summary` 無論是否能讀取 Raw 都固定包含 `total_bytes/used_bytes/free_bytes/usage_percent`，數值為可驗證整數或 null（不是零）；`status/source/reason` 必須逐欄保存。完整範例和驗收請見 FV Analyzer Specification v1.1。

### 5.5 Multi-Boot Session（FR-BENCH）

**一個獨立開機 = 最多一筆成功提交的 benchmark capture。** 每次 Shell 執行 DpX 只取得**本次** FPDT，跨開機累積依靠檔案，不假設 FPDT 跨 boot 保留。

- **FR-B01**：Session 必有 `session_id`（ASCII whitelist、長度限制）、`target_runs`（1~100）、`run_index`、`schema_version`、`DpX_build`、建立與完成狀態。
- **FR-B02**：同一 Session 的 N/選項固定；中途修改需顯式另開 Session，避免不一致。
- **FR-B03**：支援手動模式（每次開機一次 `-bench N`）、查詢/取消/續跑/統計，不需要連續 Auto Reset。
- **FR-B04**：在 Boot Services 期間設立**volatile boot-scope marker** 或等效可驗證機制，阻止同一次開機反覆執行時被算成不同 boot；此 marker 不保存到 NVRAM。若無法可靠設置，不得啟動 unattended benchmark，並須明示限制。
- **FR-B05**：每次輸出 FPDT、FBPT、Inventory、Measurements、Diagnostics，保留所有原始 bytes 與時間欄位，並記錄非敏感 Platform Fingerprint。
- **FR-B06**：驗證寫入、checksum、device path / volume marker；不可把 `fs0:` 名稱視為跨重開機恆定。磁碟不存在、唯讀、容量不足、驗證失敗時**停止，禁止 reset**。
- **FR-B07**：完整檔案寫入、Flush、重新讀回驗證後再持久化提交 Manifest；FAT rename 不保證 crash-atomic，採 versioned manifest + recoverable journal/commit marker。已提交資料不可覆寫。
- **FR-B08**：同一 Session 分為 `CREATED → CAPTURING → COMMITTED → NEXT_BOOT_PENDING → COMPLETED`，另有 `FAILED / ABORTED`。重啟時依 durable records 恢復；任何不確定狀態採 fail-safe 停止。
- **FR-B09**：自動重開機`僅在` explicit `-reset warm|cold` 後開啟，限額 1~100，最後一次 capture 不再 reset；啟動前確認 Shell 重入方式由使用者事先配置（如 `startup.nsh`）。不修改 BootNext/BootOrder；`EfiResetCold` 不等於物理 AC/G3 Power Cycle。
- **FR-B10**：Reset request 記 `requested_reset=warm|cold`、`observed_reset=unknown`（除非有可核實的獨立證據）；不同 cold/warm/S4/S3/Windows Boot Path 不可默默混在同一組統計。
- **FR-B11**：若 TSC/clock source、BIOS 版本、硬體、啟動模式、量測 PCD、FPDT 覆蓋率等關鍵條件變動，標 `NON_COMPARABLE` 或降可信度，禁止無警告合併。

**資料布局：**

```text
fsX:\DpX\sessions\NVLS_DPF_RC2\
  session.json                      # immutable test parameters and expected fingerprint
  manifest-0001.json               # versioned, checked manifest
  manifest-0002.json
  boot-0001.fpdt                   # raw ACPI FPDT, includes pointer record
  boot-0001.fbpt                   # dereferenced host boot performance table
  boot-0001.json                   # complete parsed records + warnings
  boot-0001.inventory.json
  boot-0002.*
  summary.json
  summary.csv
  report.txt
```

`fpdt` 與 `fbpt` 必須成對儲存；若是 fallback Extended Performance Source，另存來源與完整原始 bytes，不偽裝為正常 FPDT table。

**寫入狀態機**：先檢查可用空間/Session → capture → 在暫存檔寫完整資料 → Flush/close → readback 長度與 SHA-256/CRC 驗證（僅供完整性檢查，非真偽認證）→ 建立 commit record → 新 manifest generation → 只有**全部成功**後才可排定下一次 reboot。掉電後以最後完整 generation 和可驗證 run files 還原，不重複計數。

### 5.6 統計定義（FR-STAT）

- 以每次開機**同一個可比測量項**計算 N、mean、median、min/max、sample standard deviation（N=1 標 N/A）、P90/P95。
- 預設 P90/P95 採 **nearest-rank**（排序後 ceil(p*N) 的索引，從 1 起）；明確寫入 schema、報表與文件；N 太小（尤其 N<20）需提醒 percentile 不穩定。
- Incomplete/zero/missing/unsupported 分開處理：有效 0 不是 missing；不得偷偷排除 outlier，僅提供另欄 `outlier_flag` 與方法。
- 同一 Token 重複出現可統計 Count 與 Sum Duration，但**不得以重疊區間累加值當作 boot wall-clock**。
- Benchmark headline 必須標註其確切邊界（例 `ResetEnd→BDS-end` 或 `PEI-duration`）。如同一套 BIOS 無法提供共同有效端點，該指標顯示 `N/A`，不創造「總 POST Time」。
- `Shell capture time` 不能等於 `Shell entry time`；互動式人工輸入的 delay 不可視為開機耗時。

## 6. CLI 契約（v1 提案）

與官方 DP 的 `-A/-R/-s/-t/-n/-c/-i/-v` 既有語意不衝突。新參數採完整名稱，出錯時輸出 Shell-compatible 非 0 return code。

```text
DpX.efi -help
DpX.efi -summary
DpX.efi -validate
DpX.efi -fv-list
DpX.efi -fv-space       # per FV: Total/Used/Free/Usage + N/A/reason
DpX.efi -fv-files -fv 0
DpX.efi -fv-summary -json fs0:\DpX\fv-summary.json
DpX.efi -fv-raw -fv 0 -out fs0:\DpX\fv-0000.raw
DpX.efi -top 20 -min-ms 50 -fv-correlate
DpX.efi -top 30 -min-ms 50 -phase DXE
DpX.efi -tree -phase PEI
DpX.efi -gap-ms 20
DpX.efi -sysinfo
DpX.efi -snapshot -out fs0:\DpX
DpX.efi -bench 20 -session NVLS_DPF_RC2 -out fs0:\DpX
DpX.efi -bench 20 -session NVLS_DPF_RC2 -reset warm -fv-check once -out fs0:\DpX
DpX.efi -bench-status -session NVLS_DPF_RC2 -out fs0:\DpX
DpX.efi -bench-summary -session NVLS_DPF_RC2 -out fs0:\DpX
DpX.efi -bench-abort -session NVLS_DPF_RC2 -out fs0:\DpX
```

`-bench N`：若無該 Session，建立並擷取 #1；若已存在且未完成，核對參數並取得本次新的開機 Snapshot。`-bench-summary` 只讀既有資料，不觸發任何 reset。`-snapshot` 亦不自動 reboot。

CSV / JSON / RAW 輸出時檔名要 deterministic、不覆寫已完成紀錄。Versioned JSON Schema：`schema_version`、`tool_version`、`source_info`、`time_domains`、`raw_files`、`records`、`measurements`、`inventory`、`warnings`、`comparability`、`stats_method`。uint64 timestamps 在 JSON 採**十進位字串**避免 JS Number 精度流失；另提供衍生 nanoseconds/us 作為字串或受驗證的可安全數值。

## 7. 詳細工作分解與各 Gate 驗收

### M0 — 規格鎖定
- 產出：Spec、Data Format、CLI contract、Threat/Privacy Model、State Diagram、Golden Fixtures。
- Case：正常 FPDT、無 FPDT、Header 壞、Record Length 0、unknown type、截斷、overlap、巢狀、同名記錄、兩組 timestamp domains、空白資料。
- **通過條件**：規格可供獨立工程師實作；所有 N/A/partial/invalid 都有定義。

### M1 — 獨立 UEFI Application 與工具鏈
- 產出：`CommonToolsPkg.dec/dsc`、`DpX.inf`、HII Help、Windows/Linux CI、`DpX.efi`、Build Report、可執行的 OVMF Shell 測試。
- 保留 GPL/BSD 授權與原版 DP attribution；原版與 DpX 輸出可作 regression diff。
- **通過條件**：Windows VS2022 與 Linux GCC X64 CI 綠燈；OVMF Shell 執行 `-help`/`-version`/`-summary` 不 crash。

### M2 — 安全 Parser 與 Raw Snapshot
- 產出：FPDT/FBPT reader、bounds-check parser、validation flags、paired/unpaired records、raw binary 與 JSON；FV Header/FFS/Section 的 bounded parsing library 與合成檔測試。
- **通過條件**：所有 malformed fixture 都安全退出且清楚報錯，原始 bytes 完整可復原；gating tests 無 silent truncation。

### M3 — System Inventory + 單次 Analyzer
- 產出：`-sysinfo`、`-top`、`-summary`、`-tree`（註明推測）、`-gap-ms`、CSV；完整欄位來源與隱私遮蔽。加入 `-fv-list`、`-fv-files`、`-fv-space`、`-fv-summary`：容量欄位固定存在，Raw 不可驗證時 Used/Free 顯示 N/A 及原因。
- **通過條件**：OVMF 與至少 1 個實體平台皆可以輸出；某 Protocol 缺席不造成整體失敗；沒有敏感預設欄位。

### M4 — Manual N-Boot Benchmark
- 產出：Session Manager、Manifest recovery、每次 boot 獨立 raw records、`-bench-status`、`-bench-summary`、mean/median/stddev/P95；選擇性 `-fv-correlate`、`-fv-check once|each` 並另計擷取成本。
- **通過條件**：3 次手動重開機 → 精確 3 份唯一 Snapshot；同 boot 重跑不增計數；破損 manifest/暫存檔可恢復且不覆蓋正確資料。

### M5 — Auto-Reset / Fail-safe
- 產出：`-reset warm|cold`、manual bootstrap `startup.nsh` 範例、Progress/Stop/Abort、Reset failure diagnostics。
- **通過條件**：在 OVMF 可預期的重入機制下跑完整 3 次；第 3 次不 reset；媒體唯讀/滿、改映射、checksum failure、重入失敗均 stop 並可手動處理；不動 BootOrder。

### M6 — Release Quality
- 產出：X64 Release Binary、SHA-256、Symbols/MAP（如有）、Source Tag、Release Notes、User/Developer Guide、Test Matrix、已脫敏可重現測試資料、Known Limitations；必須交付獨立 Raw FV Space Accounting Engine 與可離線輸入 FV/ROM 的 Host Parser，驗證 Total/Used/Free；UEFI Shell 無法取得 Raw 時仍顯示 N/A/原因，不能猜測。
- **通過條件**：全部 mandatory CI／OVMF／實機驗收 pass；版本與來源可重現；附上可在乾淨 Windows 下操作的命令。

**DpXHost v1.0 必須有離線 FV/ROM 空間會計分析**，可精確輸出受支援標準 Raw FV 的 TOTAL/USED/FREE/Usage%。P1 才延伸多 Session、互動 Timeline、HTML、BIOS A/B、進階 IFWI Region/壓縮差異與 MAP/RVA 符號解析；上述進階項目不阻擋 M6。

## 8. CI 與測試矩陣

| 測試 | Windows | Linux | OVMF | 實機 |
|---|---|---|---|---|
| 獨立 X64 Compile、靜態檢查 | 必要 | 必要 | - | - |
| Parser / Pairing / JSON / 統計單元測試 | 必要 | 必要 | - | - |
| Golden Records / malformed / fuzz regression | 必要 | 必要 | 選配 | - |
| Help / Snapshot / missing FPDT / invalid args | - | - | 必要 | 必要 |
| 正常 FPDT、SMBIOS、PCI、TCG2 有無支援 | - | - | 必要 | 必要 |
| 3 次 manual bench + 恢復 / 去重 | - | - | 必要 | 必要 |
| Auto-reset 3 次 + 完成/錯誤停止 | - | - | 必要 | 必要（需受控環境） |
| BIOS A/B 測試可比性判定 | 必要 | 必要 | 以 fixtures | 必要 |
| 完整 OVMF Build 回歸 | 必要 | 必要 | 必要 | - |

**CI 準則**：`build-dpx.yml` 不每次重新建置整顆 OVMF；用 standalone package target 編譯＋Host parser tests，OVMF 開機測試可拆成獨立/排程工作。僅使用公用 synthetic fixtures；CI 不上傳實機敏感 Snapshot。若上游 source/依賴更新，需重新執行完整矩陣。

### 必測負面案例
1. FPDT 不存在、指向不可信記憶體、長度零、越界、Unknown Type、Checksum 錯。
2. Timer frequency 0、不同 timestamp domains、不合理 end < start、Record 僅 start。
3. 同 Token 並行與遞迴、Start/End 不等數、Guid Data1 撞號。
4. SMBIOS/TCG2/PCI Protocol 不存在，BootCurrent 無效。
5. 儲存媒體不存在、唯讀、寫到一半斷電、FS label/裝置映射改變、session corrupt。
6. 同一 boot 重複執行 benchmark、N 改變、已完成 Session 續跑、reset 失敗、使用者 abort。
7. 收集本身時間過長、互動式 Shell 延遲；不得被列為 firmware boot interval。
8. 不同 BIOS/Firmware settings/clock source/測量 Coverage 混用；需拒絕或明示不可比。

## 9. Release 交付清單

**FV 容量 Release Gate（必須）**：對每個 FV 輸出 TOTAL/USED/FREE/Usage% 或逐欄 N/A + reason；同時交付 Host 離線解析程式，至少對合成與公開的完整 Raw FV 算出精確 bytes、通過 PAD/Alignment/Deleted/Nested 測試與 Used+Free=Total 斷言。沒有這些不得宣稱 v1.0 FV 容量功能已交付。


每一個 Release 必交：
- `DpX.efi` X64 Release、SHA-256 校驗碼；Debug Binary 可作 CI artifact。
- 同步的 Git tag、固定 edk2/edk2-platforms commit、建置腳本、Build Report。
- `DpX_User_Guide.md`：前置條件、單次擷取、手動/自動多次量測、復原與 abort、注意事項。
- `DpX_Data_Format.md`：所有 JSON/CSV schema、FPDT/FBPT 來源、時間單位與 FPDT spec 版本映射。
- `DpX_Test_Plan.md` 與測試結果：每項 AC、測試平台/韌體版本、Pass/Fail、問題連結。
- 乾淨的 OVMF Golden Samples（含無 FPDT / 不完整等情況）與 sanitized Sample Report。
- Release Notes：Breaking Changes、已知限制、已驗證平台、未驗證項目、風險聲明。

**不達交付標準的情形**：僅有編譯成功、僅在 OVMF 執行、只有單次 JSON、缺少持久 Session 故障處理、未驗證最後一次 reset 不發生、誤把 User Wait 視為 BIOS 時間、無來源說明就呈現推測硬體資訊。

## 10. 技術風險與控制

| 風險 | 控制 / 取捨 |
|---|---|
| Firmware FPDT 不完整 | partial status + raw preserve + coverage report；不推算遺失事件 |
| ACPI FPDT vs EDK2 extended records 不同 | source-aware decoder、版本表與未知類型保留 |
| Pointer / raw memory 不安全 | 嚴格 bounds、可用 Memory Map / 受控限制；不可盲目 dereference |
| Session file corruption | versioned manifest、checksum、recoverability、Fail Safe |
| `fs0:` 不穩定 | Device Path + volume identity + path validation |
| 同 boot 重複記錄 | boot-scope marker；無法驗證時 disable unattended |
| Reset loop | explicit opt-in + N cap + stop/error state + no BootOrder mutation |
| Benchmark 汙染 | measurement capture after Shell，不混入已記錄前段；記錄工具自身 overhead |
| Cross-platform 不可比 | Fingerprint 與同名 metric boundary；不默默合併 |
| PCI/SMBIOS 隱私 | 最小資訊、Opt-in、敏感欄位遮蔽；公開 GitHub 不放實機 raw samples |
| 自動啟動 Shell 依平台而異 | 手動先驗收；startup.nsh 純文件範例；無重入保證不自動 reset |
| 廠商私有功能 | Generic DpX 不依賴 NDA 代碼；以 Plugin/Host Resolver 延伸 |

## 11. 決策與驗收優先級（建議預設）

1. **主要執行模式**：UEFI Shell 下的獨立 `DpX.efi`，無需重刷 BIOS。
2. **CPU 架構**：先 X64，IA32/AARCH64 非 v1.0 Blocking。
3. **資料存放**：可寫入的 FAT filesystem；每開機一筆，保留原始 FPDT + FBPT。
4. **預設 N**：使用者顯式輸入；建議一般比較 N=20，對 tail latency 建議 N≥30。
5. **預設 Reset**：`none`。手動多次先完成，再發行 opt-in auto mode。
6. **資料可信度**：source/status 必填，Unknown 不推測。
7. **隱私**：敏感識別資訊預設 off；資料絕不自動推送 GitHub。
8. **來源碼策略**：新增獨立 Package，官方 Git submodules 保持 immutable。
9. **交付 Gate**：Windows + Linux build、OVMF 實測、實機驗收 **三個獨立章節**。
10. **Windows Boot**：若需同次 OS Loader / ExitBootServices end-to-end 指標，另開 Windows 收集流程；Shell capture 不冒充此資訊。

## 12. 參考規格與追蹤連結

- [DpX FV Analyzer Detailed Specification](DpX_FV_Analyzer_Spec_v1.1.md)

- [EDK II Dp 原始碼（stable202608）](https://github.com/tianocore/edk2/tree/edk2-stable202608/ShellPkg/DynamicCommand/DpDynamicCommand)
- [EDK II PerformanceLib API](https://github.com/tianocore/edk2/blob/edk2-stable202608/MdePkg/Include/Library/PerformanceLib.h)
- [ACPI 6.6 §5.2.24 Firmware Performance Data Table](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html#firmware-performance-data-table-fpdt)
- [Edk2Test Issue #3](https://github.com/cdshopping001-coder/Edk2Test/issues/3)
- [EDK2 OVMF Build #10（Windows + Linux passed）](https://github.com/cdshopping001-coder/Edk2Test/actions/runs/39148178616)

> 說明：本文是可供開發、Code Review、Test Plan 與最終交付使用的規格基線；此時並未聲稱 DpX 的任何新功能已完成。版本與 CLI 可因受測平台差異按正式 Issue/PR 變更，但每次變更需同步更新規格和測試。
