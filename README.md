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
