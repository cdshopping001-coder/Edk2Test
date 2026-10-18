# EDK II source & build workspace

A reproducible UEFI firmware development workspace based on **TianoCore EDK II**.

| Item | Pinned configuration |
|---|---|
| Source | `tianocore/edk2` Git submodule at `edk2/` |
| Baseline | `edk2-stable202608` |
| Upstream commit | `2970e5699ba6267f3384ffab20f96647578aebc8` |
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

The script initializes submodules, creates `.venv`, installs the **pinned upstream** Python requirements, compiles BaseTools, runs edksetup, and builds OVMF X64. The expected firmware image is `edk2/Build/OvmfX64/DEBUG_GCC/FV/OVMF.fd` (or `RELEASE_GCC`).

## Windows build (VS2022)

Install Visual Studio 2022 with **Desktop development with C++**, Windows SDK, Python 3, Git, NASM and iASL. Ensure `nasm.exe` and `iasl.exe` are available on PATH. Example using an elevated shell with Chocolatey:

```powershell
choco install nasm iasl -y
```

Open **x64 Native Tools Command Prompt for VS 2022**. From the repository root:

```cmd
scripts\build-windows.cmd DEBUG
REM or
scripts\build-windows.cmd RELEASE
```

The script checks the compiler, NASM and iASL, installs upstream Python requirements into `.venv`, sets `NASM_PREFIX` and `IASL_PREFIX`, calls `edksetup.bat Rebuild`, and builds `OvmfPkg/OvmfPkgX64.dsc` with `-t VS2022`.

## GitHub Actions

[Build workflow](.github/workflows/build-ovmf.yml) runs for pushes, pull requests and manual dispatch. Each successful job uploads an `OVMF.fd` firmware image and build report. Check the repository **Actions** tab for actual results; merely having a workflow file does **not** prove a successful build.

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
- [OVMF build documentation](https://www.tianocore.org/tianocore-wiki.github.io/development/tutorials-howto/how_to_build_ovmf.html)
- [EDK2 toolchain definitions](https://github.com/tianocore/edk2/blob/edk2-stable202608/BaseTools/Conf/tools_def.template)

EDK II uses its upstream licenses; the submodule does not change upstream attribution.
