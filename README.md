# EDK II source & build workspace

A reproducible UEFI firmware development workspace based on **TianoCore EDK II**.

| Item | Pinned configuration |
|---|---|
| Core source | `tianocore/edk2` Git submodule at `edk2/` |
| Reference platforms | `tianocore/edk2-platforms` Git submodule at `edk2-platforms/` |
| Baseline | `edk2-stable202608` |
| Core upstream commit | `2970e5699ba6267f3384ffab20f96647578aebc8` |
| Platforms snapshot | `1c500d13a653cf0f556ecf7ce703c12874247efc` (master snapshot; compatibility is per-platform, **not** a matching stable tag) |
| Platform | `OvmfPkg/OvmfPkgX64.dsc` |
| Architecture | X64 |
| Linux | GCC toolchain tag `GCC` (not legacy `GCC5`) |
| Windows | Visual Studio 2022, toolchain `VS2022` |
| CI | GitHub Actions, Ubuntu 24.04 and Windows 2022 |

## Checkout the full source (including EDK2 third-party submodules)

```bash
git clone --recurse-submodules https://github.com/cdshopping001-coder/Edk2Test.git
cd Edk2Test
git submodule status
git -C edk2 describe --tags --exact-match
```

If the repository was cloned without recursion:

```bash
git submodule update --init --recursive
```

The `edk2/` directory is a **Git submodule**, so the source is cloned locally and the exact upstream revision is recorded here. This is not a separate GitHub fork. Changes to upstream code should be maintained in a personal EDK2 fork and its submodule URL updated, or submitted as patches upstream. Do not commit modifications inside a detached submodule without a branch and a remote that you control.

## When is edk2-platforms required?

- **OVMF X64**: not required. The `OvmfPkg/OvmfPkgX64.dsc` target is in core `edk2`.
- **Vendor / physical board builds**: typically use board DSC/FDF and silicon / feature packages from `edk2-platforms` or a vendor-specific repository. Some require binaries from `tianocore/edk2-non-osi` and/or `IntelFsp/FSP`; neither is included by default.
- The `edk2-platforms` repository tracks edk2 **master**, not necessarily `edk2-stable202608`. We record a fixed upstream snapshot for repeatable checkouts, but have **not** validated any board DSC against this combination. Each hardware board requires validation.

## Vendor-platform workspace: PACKAGES_PATH

The OVMF wrappers deliberately build using only core EDK2. If building a platform with external packages, first initialize the platform source:

```bash
git submodule update --init edk2-platforms
```

For Linux, run from the top-level `Edk2Test` directory:

```bash
export WORKSPACE="$PWD/edk2"
export PACKAGES_PATH="$PWD/edk2-platforms:$PWD/edk2-platforms/Platform/Intel:$PWD/edk2-platforms/Silicon/Intel:$PWD/edk2-platforms/Features/Intel"
cd edk2
source ./edksetup.sh --reconfig
# Build the desired board/package with its documented DSC/FDF and toolchain.
```

Windows **cmd.exe** example, starting in the top-level `Edk2Test` directory:

```cmd
set "PACKAGES_PATH=%CD%\edk2-platforms;%CD%\edk2-platforms\Platform\Intel;%CD%\edk2-platforms\Silicon\Intel;%CD%\edk2-platforms\Features\Intel"
cd edk2
call edksetup.bat Rebuild
REM Build the platform-specific DSC with the required components and toolchain.
```

For a non-Intel board, replace the Intel-specific paths with paths documented by that board. The `PACKAGES_PATH` separator is `:` on Linux and `;` on Windows. See [official edk2-platforms build instructions](https://github.com/tianocore/edk2-platforms).

## Linux build (Ubuntu 24.04)

Prerequisites (one time):

```bash
sudo apt-get update
sudo apt-get install -y build-essential git nasm acpica-tools uuid-dev python3-venv python3-dev swig
```

Run from repository root:

```bash
bash scripts/build-linux.sh DEBUG
# or
bash scripts/build-linux.sh RELEASE
```

The script initializes the EDK2 core submodule and its dependencies, creates `.venv`, installs the OVMF-specific Python requirements, compiles BaseTools, runs edksetup, and builds OVMF X64. The expected firmware image is `edk2/Build/OvmfX64/DEBUG_GCC/FV/OVMF.fd` (or `RELEASE_GCC`).

## Windows build (VS2022)

Install Visual Studio 2022 with **Desktop development with C++**, Windows SDK, Python 3, Git, NASM and iASL. Ensure `nasm.exe` and `iasl.exe` are available on PATH. Example using an elevated shell with Chocolatey:

```powershell
choco install nasm iasl -y
```

Open **x86 Native Tools Command Prompt for VS 2022** (EDK2 BaseTools default builds the IA32 host utilities). The firmware target remains X64. From the repository root:

```cmd
scripts\build-windows.cmd DEBUG
REM or
scripts\build-windows.cmd RELEASE
```

The script checks the compiler, NASM and iASL, installs OVMF-specific Python requirements into `.venv`, sets `NASM_PREFIX` and `IASL_PREFIX`, calls `edksetup.bat Rebuild`, and builds `OvmfPkg/OvmfPkgX64.dsc` with `-t VS2022`.

## GitHub Actions

[Build workflow](.github/workflows/build-ovmf.yml) runs for pushes, pull requests and manual dispatch. Each successful job uploads an `OVMF.fd` firmware image and build report. Check the repository **Actions** tab for actual results; merely having a workflow file does **not** prove a successful build.

## Updating platform snapshots

Core EDK2 and platform repositories are **independent**. Do not automatically update either to an unreviewed branch head.

```bash
git -C edk2-platforms fetch origin
git -C edk2-platforms checkout 1c500d13a653cf0f556ecf7ce703c12874247efc
git add edk2-platforms
git commit -m "chore: pin edk2-platforms snapshot"
```

## Updating the upstream EDK2 baseline

Choose a reviewed tag or commit (do not float to master automatically):

```bash
git -C edk2 fetch --tags origin
git -C edk2 checkout edk2-stable202608
git add edk2
git commit -m "chore: pin EDK2 upstream revision"
```

Before adopting a newer stable release, review upstream breaking changes and CI results.

## References

- [TianoCore EDK II](https://github.com/tianocore/edk2)
- [TianoCore EDK II Platforms](https://github.com/tianocore/edk2-platforms)
- [Intel platform build instructions](https://github.com/tianocore/edk2-platforms/blob/master/Platform/Intel/Readme.md)
- [OVMF build documentation](https://www.tianocore.org/tianocore-wiki.github.io/development/tutorials-howto/how_to_build_ovmf.html)
- [EDK2 toolchain definitions](https://github.com/tianocore/edk2/blob/edk2-stable202608/BaseTools/Conf/tools_def.template)

EDK II uses its upstream licenses; the submodule does not change upstream attribution.

## Python dependency compatibility note

The pinned `edk2-stable202608` requirements include `pylibfdt==1.7.2.post1`, which has had a known SWIG compatibility issue on some environments. OVMF X64 does not require `pylibfdt`. For this baseline only, the build wrappers therefore install `scripts/pip-requirements-ovmf.txt`, which preserves the other upstream pins but excludes `pylibfdt`. Projects requiring FIT/DTB support should install and validate an appropriate `pylibfdt` independently.


## DpX.efi — UEFI Shell Firmware Analyzer (v0.15.0, in development)

DpX is a standalone, unsigned X64 UEFI Shell application in `CommonToolsPkg/Applications/DpX`. The pinned upstream EDK2 submodule is not modified.

Build only DpX (no full OVMF build): `bash scripts/build-dpx-linux.sh RELEASE`, or use `scripts\\build-dpx-windows.cmd RELEASE` with VS2022. Artifacts are available through the dedicated **DpX X64 Standalone Build** GitHub Actions workflow.

Implemented commands include `-version`, `-help`, `-sysinfo`, `-fv-list`, `-fv-files -fv N`, `-fv-space` (live FV2 size N/A without raw bytes), `-rom-info`, `-rom-analyze -input fs0:\\FILE.FV -fv-space` (one raw FV at offset zero), and `-rom-scan -input fs0:\\BIOS.ROM` (enumerate nonoverlapping standard FVs inside a ROM image with verified capacity accounting).

**Validation:** GCC X64 and VS2022 X64 compile; actual QEMU/OVMF Shell integration tests execute DpX.efi and verify synthetic FVs (4096 bytes total, 136 used, 3960 free) and multi-FV ROM offsets 0x1000/0x3000. This is not Dell Citadel or Intel RVP hardware validation.

**Current live capability:** `DpX.efi` without input files (or `-summary`) runs current-system inventory, local firmware ACPI FPDT/FBPT with memory-map validation, FV2 enumeration and SPI/FVB interface discovery. `-perf-live` directly inspects the platform; `-perf-live -pointers` shows pointers only. **Now implemented:** `-fv-detail` reads current-machine FV2 logical FFS/Sections plus FVB2 raw physical FFS metadata, EFI standard compression decompression and Nested FV FFS counts. Parent FV Used/Free is based exclusively on original bytes; PAD and deleted-but-unreclaimed bytes stay Used. **Still unsupported:** arbitrary proprietary GUID-defined compression and restricted SPI raw access; full DP, SPI dump and multi-boot benchmark remain separate work. Unavailable values remain N/A.

See [DpX Plan v1.3](docs/DpX_Development_Plan_v1.3.md), [Multi-FV scope](docs/DpX_M2_MultiFV.md), and [real Shell CI gate](docs/DpX_OVMF_Shell_Test.md).

### DpX DUT-first live workflow (current development baseline)

On the actual test device, boot UEFI Shell and enter **`DpX.efi`** (no parameters). This immediately queries local system information, firmware ACPI FPDT/FBPT when validated, live FV2 inventory and SPI/FVB capabilities. **External ROM or FPDT files are not prerequisites.** Use `-summary` for the same survey; `-perf-live` for live performance details; `-perf-live -pointers` for pointer-only diagnostics.

Physical FV USED/FREE are calculated for verified read-only FVB raw regions; inaccessible FV regions remain N/A. Extended FPDT Start/End pairs are decoded and time-contained in an event tree, but unmatched/proprietary performance records and platform SPI access remain unsupported where no validated backend exists. [DUT-first requirements](docs/DpX_DUT_First_Operation.md). The older offline input commands remain useful for regression and cross-image analysis but are optional.

### DpX 0.10.0 deep FV and decompression

Run `DpX.efi -fv-detail` on the DUT, without exporting ROM. It displays FV format/name GUID when present, each FFS GUID, type, stored byte offset and size (where raw FVB is available), Sections, EFI standard compression decoded size and nested FV/FFS lists. **Physical Used/Free do not change after decompression; nested counts are separate, not added to parent FFS count.** Unknown GUID-defined codecs report unsupported. See [Deep FV details](docs/DpX_FV_Deep_Sections.md), [OVMF golden compression tests](docs/DpX_FV_Deep_Integration_Tests.md), [PR #18](https://github.com/cdshopping001-coder/Edk2Test/pull/18).

### DpX 0.15.0 — Five-stage DUT pipeline (default)

**Primary field command:**

```text
Shell> DpX.efi
```

No ROM, MAP, FPDT, FBPT or input filename is required.

1. **Raw DP records:** discover current-boot ACPI FPDT or supported EDK2 fallback and capture verified FBPT in bounded transient RAM.
2. **FFS inventory:** enumerate all firmware-visible FV2/FFS, and readable FVB FFS/sections including bounded UEFI-standard decompression and nested FV; do not double count logical child sizes.
3. **GUID → module:** full 128-bit FFS GUID mapping to UI names with an on-device cache; ambiguous/unavailable identities remain N/A.
4. **Performance tree:** pair Extended FPDT Start/End by type, APIC, GUID and token, resolve Module names, print inclusive durations with an ASCII time-containment tree. Time nesting **does not prove** a caller/callee relationship, and matching MAP/PDB/RVA is necessary for verified function names.
5. **FV summary:** reuse stage-two physical-FVB verified metrics: Total/Used/Free, FFS, PAD and deleted-but-unreclaimed count. Nested decoded sizes are **not** added to physical Used bytes; inaccessible FVs remain N/A.

The command is read-only: no SPI or NVRAM writes and no automatic reset. Validation includes GCC and VS2022 X64 CI, QEMU/OVMF Shell integration, synthetic nested Callback test (120 ms containing a 40 ms event), and actual OVMF FV capacity accounting. **Real Citadel/RVP hardware acceptance has not been performed.**

See [Five-stage report specification](docs/DpX_Five_Stage_Tree_v0.15.md), [PR #23](https://github.com/cdshopping001-coder/Edk2Test/pull/23) and [the verified CI run](https://github.com/cdshopping001-coder/Edk2Test/actions/runs/39183985413).
