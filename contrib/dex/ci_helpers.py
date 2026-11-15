#!/usr/bin/env python3
"""
Copy-Paste-Proof CI Helper Suite for Standalone `Scash Exchange Wallet v0.1.0-beta.1`.
Shares `%APPDATA%\Scash` / `~/.scash` blockchain data while installing side-by-side into
`C:\Program Files\Scash Exchange Wallet\` (`scash-exchange-qt.exe` & `scash-exchange-qtkawaii.exe`).
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1048576), b""):
            h.update(chunk)
    return h.hexdigest()


def cmd_test_htlc() -> None:
    cmd = [
        "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
        "-DSCASH_DEX_STANDALONE_TEST=1",
        "-Isrc/qt",
        "src/qt/exchangehtlc.cpp",
        "src/qt/exchangemulticoin.cpp",
        "contrib/dex/test_exchange_htlc.cpp",
        "-o", "./test_exchange_htlc_bin",
    ]
    subprocess.run(cmd, check=True)
    subprocess.run(["./test_exchange_htlc_bin"], check=True)
    Path("./test_exchange_htlc_bin").unlink(missing_ok=True)


def cmd_commit_and_pr() -> None:
    actor = os.environ.get("GITHUB_ACTOR", "github-actions")
    actor_id = os.environ.get("GITHUB_ACTOR_ID", "41898282")
    base_branch = os.environ.get("BASE_BRANCH", "scash_master")
    target_branch = os.environ.get("TARGET_BRANCH", "exchange-wallet-main")
    open_pr = os.environ.get("OPEN_PR", "false").lower() == "true"

    subprocess.run(["git", "config", "user.name", actor], check=True)
    subprocess.run(["git", "config", "user.email", f"{actor_id}+{actor}@users.noreply.github.com"], check=True)
    subprocess.run(["git", "checkout", "-B", target_branch], check=True)
    subprocess.run([
        "git", "add",
        "CMakeLists.txt", "depends/", "src/boost/", "src/qt/",
        "contrib/dex/", "doc/release-notes/scash/release-notes-exchange-0.1.0.md",
    ], check=True)

    if subprocess.run(["git", "diff", "--cached", "--quiet"]).returncode != 0:
        subprocess.run([
            "git", "commit", "-m",
            "feat(wallet): Scash Exchange Wallet v0.1.0 with built-in BTC (mempool.space) and Arbitrum/Ethereum USDT",
        ], check=True)
    subprocess.run(["git", "push", "--force", "origin", target_branch], check=True)

    if open_pr and target_branch != base_branch:
        subprocess.run([
            "gh", "pr", "create",
            "--base", base_branch,
            "--head", target_branch,
            "--title", "Scash Exchange Wallet v0.1.0: Built-In Non-Custodial SCASH + BTC + Arbitrum/ETH USDT DEX",
            "--body", "Standalone **Scash Exchange Wallet v0.1.0** with built-in native Bitcoin (`bc1q`), Arbitrum One USDT, and Ethereum Mainnet USDT atomic swap wallets.",
        ], check=False)


def cmd_build_linux() -> None:
    nproc = str(os.cpu_count() or 4)
    shutil.rmtree("build", ignore_errors=True)
    subprocess.run([
        "cmake", "-B", "build",
        "--toolchain", "depends/x86_64-pc-linux-gnu/toolchain.cmake",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_GUI=ON", "-DBUILD_TESTS=OFF", "-DBUILD_BENCH=OFF",
    ], check=True)
    subprocess.run(["cmake", "--build", "build", "-j", nproc], check=True)


def cmd_package_linux() -> None:
    bin_dir = Path("out-linux/scash-exchange-wallet-0.1.0/bin")
    bin_dir.mkdir(parents=True, exist_ok=True)

    pairs = [
        ("scash-cli", "bitcoin-cli", "scash-cli"),
        ("scash-qt", "bitcoin-qt", "scash-exchange-qt"),
        ("scash-tx", "bitcoin-tx", "scash-tx"),
        ("scash-util", "bitcoin-util", "scash-util"),
        ("scash-wallet", "bitcoin-wallet", "scash-wallet"),
        ("scashd", "bitcoind", "scashd"),
    ]
    for primary, fallback, dest in pairs:
        src = Path("build/bin") / primary
        if not src.exists():
            src = Path("build/bin") / fallback
        if src.exists():
            subprocess.run(["strip", str(src)], check=False)
            shutil.copy2(src, bin_dir / dest)

    if (bin_dir / "scash-exchange-qt").exists():
        shutil.copy2(bin_dir / "scash-exchange-qt", bin_dir / "scash-exchange-qtkawaii")

    tgz_name = "scash-exchange-wallet-0.1.0-x86_64-pc-linux-gnu.tgz"
    subprocess.run(["tar", "-czf", f"out-linux/{tgz_name}", "-C", "out-linux", "scash-exchange-wallet-0.1.0"], check=True)

    sha_lines = []
    for f in sorted(bin_dir.iterdir(), key=lambda p: p.name):
        if f.is_file():
            sha_lines.append(f"{sha256_file(f)}  {f.name}")
    sha_lines.append(f"{sha256_file(Path('out-linux') / tgz_name)}  {tgz_name}")
    sha_txt = "\n".join(sha_lines) + "\n"
    Path("out-linux/SHA256SUMS-linux.txt").write_text(sha_txt, encoding="utf-8")
    print(sha_txt)


def cmd_build_and_package_windows() -> None:
    nproc = str(os.cpu_count() or 4)
    subprocess.run([
        "cmake", "-B", "build-win",
        "--toolchain", "depends/x86_64-w64-mingw32/toolchain.cmake",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_GUI=ON", "-DBUILD_TX=ON", "-DBUILD_UTIL=ON", "-DBUILD_WALLET_TOOL=ON",
        "-DBUILD_TESTS=OFF", "-DBUILD_BENCH=OFF",
    ], check=True)
    subprocess.run(["cmake", "--build", "build-win", "-j", nproc], check=True)

    win_bin = Path("out-win/scash-exchange-wallet-0.1.0/bin")
    win_bin.mkdir(parents=True, exist_ok=True)
    pairs = [
        ("scashd.exe", "bitcoind.exe", "scashd.exe"),
        ("scash-qt.exe", "bitcoin-qt.exe", "scash-exchange-qt.exe"),
        ("scash-cli.exe", "bitcoin-cli.exe", "scash-cli.exe"),
        ("scash-tx.exe", "bitcoin-tx.exe", "scash-tx.exe"),
        ("scash-util.exe", "bitcoin-util.exe", "scash-util.exe"),
        ("scash-wallet.exe", "bitcoin-wallet.exe", "scash-wallet.exe"),
    ]
    for primary, fallback, dest in pairs:
        src = Path("build-win/bin") / primary
        if not src.exists():
            src = Path("build-win/bin") / fallback
        if src.exists():
            subprocess.run(["x86_64-w64-mingw32-strip", str(src)], check=False)
            shutil.copy2(src, win_bin / dest)

    if (win_bin / "scash-exchange-qt.exe").exists():
        shutil.copy2(win_bin / "scash-exchange-qt.exe", win_bin / "scash-exchange-qtkawaii.exe")

    zip_name = "scash-exchange-wallet-0.1.0-win64-setup.zip"
    setup_name = "scash-exchange-wallet-0.1.0-win64-setup.exe"
    subprocess.run(["zip", "-r", zip_name, "scash-exchange-wallet-0.1.0"], cwd="out-win", check=True)

    daemon_files = [
        f'  File /nonfatal "scash-exchange-wallet-0.1.0\\bin\\{fn}"'
        for fn in ["scashd.exe", "scash-cli.exe", "scash-tx.exe", "scash-util.exe", "scash-wallet.exe"]
        if (win_bin / fn).exists()
    ]
    daemon_block = "\n".join(daemon_files)

    nsi_script = rf"""!include "MUI2.nsh"
Name "Scash Exchange Wallet 0.1.0 (64-bit)"
OutFile "{setup_name}"
InstallDir "$PROGRAMFILES64\Scash Exchange Wallet"
InstallDirRegKey HKCU "SOFTWARE\ScashExchangeWallet" ""
RequestExecutionLevel admin
SetCompressor /SOLID lzma
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Section "Scash Exchange Wallet 0.1.0 (required)"
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "scash-exchange-wallet-0.1.0\bin\*.exe"
  SetOutPath "$INSTDIR\daemon"
{daemon_block}
  WriteRegStr HKCU "SOFTWARE\ScashExchangeWallet" "" $INSTDIR
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Scash Exchange Wallet"
  CreateShortCut "$SMPROGRAMS\Scash Exchange Wallet\Scash Exchange Wallet (64-bit).lnk" "$INSTDIR\scash-exchange-qt.exe"
  CreateShortCut "$SMPROGRAMS\Scash Exchange Wallet\Scash Exchange Kawaii (64-bit).lnk" "$INSTDIR\scash-exchange-qtkawaii.exe"
  CreateShortCut "$SMPROGRAMS\Scash Exchange Wallet\Uninstall Scash Exchange Wallet.lnk" "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\Scash Exchange Wallet.lnk" "$INSTDIR\scash-exchange-qt.exe"
  CreateShortCut "$DESKTOP\Scash Exchange Kawaii.lnk" "$INSTDIR\scash-exchange-qtkawaii.exe"
SectionEnd
Section "Uninstall"
  RMDir /r "$SMPROGRAMS\Scash Exchange Wallet"
  Delete "$DESKTOP\Scash Exchange Wallet.lnk"
  Delete "$DESKTOP\Scash Exchange Kawaii.lnk"
  RMDir /r "$INSTDIR\daemon"
  Delete "$INSTDIR\*.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "SOFTWARE\ScashExchangeWallet"
SectionEnd
"""
    Path("out-win/scash-exchange-installer.nsi").write_text(nsi_script, encoding="utf-8")
    subprocess.run(["makensis", "-V2", "scash-exchange-installer.nsi"], cwd="out-win", check=True)

    dest_setup = Path("out-win") / setup_name
    dest_zip = Path("out-win") / zip_name
    assert dest_setup.exists() and dest_setup.stat().st_size > 100000, "win64-setup.exe missing!"
    assert dest_zip.exists() and dest_zip.stat().st_size > 100000, "win64-setup.zip missing!"

    sha_win = f"{sha256_file(dest_setup)}  {setup_name}\n{sha256_file(dest_zip)}  {zip_name}\n"
    Path("out-win/SHA256SUMS-windows.txt").write_text(sha_win, encoding="utf-8")
    print(sha_win)


def cmd_publish_release() -> None:
    tag = "v0.1.0-beta.1"
    repo = os.environ.get("GITHUB_REPOSITORY", "ScashNetworkCommunity/Scash-Exchange-Wallet")
    target_branch = os.environ.get("TARGET_BRANCH", "scash_master")
    is_draft = os.environ.get("IS_DRAFT", "true").lower() == "true"

    linux_sha = Path("release-dl/scash-exchange-linux-release/SHA256SUMS-linux.txt").read_text(encoding="utf-8").strip()
    win_sha = Path("release-dl/scash-exchange-windows-release/SHA256SUMS-windows.txt").read_text(encoding="utf-8").strip()

    body = (
        "**Scash Exchange Wallet v0.1.0-beta.1** is a standalone, 100% non-custodial multi-coin atomic swap desktop wallet "
        "(`scash-exchange-qt` and `scash-exchange-qtkawaii`) built on Scash Core `2.3.0` (`Bitcoin Core 30.4`). "
        "It shares your existing `%APPDATA%\\Scash` (`~/.scash`) blockchain and wallet directory (run one wallet app at a time) "
        "while embedding native **Bitcoin (`bc1q` via `mempool.space`)**, **Arbitrum One USDT (`~$0.01` gas)**, and "
        "**Ethereum Mainnet USDT** light wallets derived directly from your encrypted Scash wallet.\n\n"
        f"See the release notes in [`doc/release-notes/scash/release-notes-exchange-0.1.0.md`](https://github.com/{repo}/blob/{target_branch}/doc/release-notes/scash/release-notes-exchange-0.1.0.md).\n\n"
        "Users are advised to build from source. Binaries are provided only for convenience.\n\n"
        "Important: Verify hashes using `sha256sum`. If the hashes do not match, do not run the binaries and file a GitHub issue immediately to alert the developers!\n\n"
        "```text\n"
        "Linux (compiled on Ubuntu 22.04)\n"
        f"{linux_sha}\n\n"
        "Windows\n"
        f"{win_sha}\n"
        "```\n"
    )
    notes_file = Path("/tmp/release_body_exchange_0_1_0.md")
    notes_file.write_text(body, encoding="utf-8")

    assets = [
        "release-dl/scash-exchange-windows-release/scash-exchange-wallet-0.1.0-win64-setup.exe",
        "release-dl/scash-exchange-windows-release/scash-exchange-wallet-0.1.0-win64-setup.zip",
        "release-dl/scash-exchange-linux-release/scash-exchange-wallet-0.1.0-x86_64-pc-linux-gnu.tgz",
    ]
    for a in assets:
        p = Path(a)
        assert p.exists() and p.stat().st_size > 100000, f"Missing asset: {a}"

    exists = subprocess.run(["gh", "release", "view", tag], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if exists:
        subprocess.run(["gh", "release", "upload", tag, *assets, "--clobber"], check=True)
        edit_cmd = ["gh", "release", "edit", tag, "--title", "Scash Exchange Wallet 0.1.0-beta.1", "--notes-file", str(notes_file)]
        if not is_draft:
            edit_cmd.append("--draft=false")
        subprocess.run(edit_cmd, check=True)
    else:
        create_cmd = [
            "gh", "release", "create", tag, *assets,
            "--target", target_branch,
            "--title", "Scash Exchange Wallet 0.1.0-beta.1",
            "--notes-file", str(notes_file),
            "--prerelease",
        ]
        if is_draft:
            create_cmd.append("--draft")
        subprocess.run(create_cmd, check=True)


def main() -> int:
    action = sys.argv[1]
    if action == "test-htlc":
        cmd_test_htlc()
    elif action == "commit-and-pr":
        cmd_commit_and_pr()
    elif action == "build-linux":
        cmd_build_linux()
    elif action == "package-linux":
        cmd_package_linux()
    elif action == "build-and-package-windows":
        cmd_build_and_package_windows()
    elif action == "publish-release":
        cmd_publish_release()
    else:
        raise ValueError(f"Unknown action: {action}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
