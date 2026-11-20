#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Local Darwin linux-user launcher with FUSE-free type-2 AppImage support."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def image_argument(args):
    for i, value in enumerate(args):
        try:
            with open(value, "rb") as stream:
                header = stream.read(20)
        except (OSError, ValueError):
            continue
        if header[:4] == b"\x7fELF":
            if header[8:11] == b"AI\x02":
                return i, Path(value).resolve()
            return None
    return None


def guest_architecture(args):
    for value in args:
        try:
            with open(value, "rb") as stream:
                header = stream.read(20)
        except (OSError, ValueError):
            continue
        if header[:4] == b"\x7fELF":
            if header[4:6] == b"\x01\x01" and header[18:20] == b"\x03\x00":
                return "i386"
            return "x86_64"
    return "x86_64"


def extract(binary, prefix, image, cache):
    digest = hashlib.sha256()
    with image.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    cache.mkdir(parents=True, exist_ok=True)
    directory = cache / digest.hexdigest()
    with (cache / (digest.hexdigest() + ".lock")).open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if (directory / ".linonmac-extracted").exists():
            return directory
        with tempfile.TemporaryDirectory(
                prefix=".extract-", dir=cache) as scratch:
            scratch = Path(scratch)
            logfile = scratch / "extraction.log"
            with logfile.open("wb") as log:
                status = subprocess.run(
                    [str(binary), *prefix, str(image), "--appimage-extract"],
                    cwd=scratch, stdout=log, stderr=subprocess.STDOUT,
                    check=False,
                ).returncode
            app = scratch / "squashfs-root"
            if status or not (app / "AppRun").exists():
                raise RuntimeError("AppImage extraction failed:\n" +
                                   logfile.read_text(errors="replace")[-4000:])
            (app / ".linonmac-extracted").write_text(json.dumps({
                "image": str(image), "sha256": digest.hexdigest()
            }) + "\n")
            if directory.exists():
                raise RuntimeError(
                    "Incomplete AppImage cache: " + str(directory))
            app.rename(directory)
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--rootfs", required=True, type=Path)
    parser.add_argument("--binary-i386", type=Path)
    parser.add_argument("--rootfs-i386", type=Path)
    parser.add_argument("--gui-rootfs-i386", type=Path)
    parser.add_argument("--gui-rootfs", type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("args", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    args = options.args
    if args[:1] == ["--"]:
        args = args[1:]
    arch = guest_architecture(args)
    binary = options.binary
    runtime = options.rootfs
    if arch == "i386":
        binary = options.binary_i386 or binary.with_name("qemu-i386")
        runtime = options.rootfs_i386
        if runtime is None:
            raise RuntimeError("Set --rootfs-i386 to a 32-bit Linux runtime")
    binary = Path(os.environ.get("LINONMAC_QEMU", binary)).resolve()
    runtime = Path(os.environ.get("LINONMAC_ROOTFS", runtime)).resolve()
    runtime32 = os.environ.get("QEMU_LD_PREFIX_I386", str(
        runtime if arch == "i386" else options.rootfs_i386 or runtime))
    runtime64 = os.environ.get("QEMU_LD_PREFIX_X86_64", str(
        runtime if arch == "x86_64" else options.rootfs))
    abi_prefix = ["-E", "QEMU_LD_PREFIX_I386=" + runtime32,
                  "-E", "QEMU_LD_PREFIX_X86_64=" + runtime64]
    image = image_argument(args)
    if not image:
        os.execv(binary, [str(binary), "-L", str(runtime), *abi_prefix, "-E",
                         f"PATH={runtime}/usr/bin:{runtime}/bin:/usr/bin:/bin",
                         *args])
    index, image = image
    app_args = args[index + 1:]
    if any(arg.startswith("--appimage-") and arg != "--appimage-extract-and-run"
           for arg in app_args):
        os.execv(binary, [str(binary), "-L", str(runtime), *abi_prefix, *args])
    if not os.access(image, os.X_OK):
        raise RuntimeError(
            f"{image}: Permission denied (execute bit is missing)")
    gui_runtime = (options.gui_rootfs_i386 if arch == "i386"
                   else options.gui_rootfs)
    if "LINONMAC_ROOTFS" not in os.environ and gui_runtime:
        runtime = gui_runtime.resolve()
    loader = ("lib/ld-linux.so.2" if arch == "i386"
              else "lib64/ld-linux-x86-64.so.2")
    if not (runtime / loader).is_file():
        raise RuntimeError("Missing " + arch + " Linux interpreter: " + loader)
    cache = Path(os.environ.get("LINONMAC_CACHE", options.cache)).resolve()
    app = extract(binary, ["-L", str(runtime)], image, cache)
    guest_home = Path(os.environ.get(
        "LINONMAC_HOME", str(cache / "home"))).resolve()
    guest_home.mkdir(parents=True, exist_ok=True)
    env = {
        "HOME": str(guest_home),
        "PATH": f"{runtime}/usr/bin:{runtime}/bin:/usr/bin:/bin",
        "QT_X11_NO_MITSHM": "1",
        "QT_XCB_GL_INTEGRATION": "none",
        "SDL_AUDIODRIVER": os.environ.get("SDL_AUDIODRIVER", "dummy"),
    }
    runtime_key = ("QEMU_LD_PREFIX_I386" if arch == "i386"
                   else "QEMU_LD_PREFIX_X86_64")
    env[runtime_key] = str(runtime)
    if os.environ.get("DISPLAY"):
        env["SDL_VIDEODRIVER"] = os.environ.get("SDL_VIDEODRIVER", "x11")
        env["QT_QPA_PLATFORM"] = os.environ.get("QT_QPA_PLATFORM", "xcb")
    authority = Path(os.environ.get(
        "XAUTHORITY", str(Path.home() / ".Xauthority")))
    if authority.exists():
        env["XAUTHORITY"] = str(authority)
    prefix = ["-L", str(runtime), *abi_prefix]
    for key, value in env.items():
        prefix.extend(["-E", key + "=" + value])
    prefix.extend(args[:index])
    entry = app / "AppRun"
    with entry.open("rb") as stream:
        line = stream.readline(4096)
    command = [str(entry)]
    if line.startswith(b"#!"):
        interpreter = line[2:].decode().strip().split(None, 1)
        if not interpreter:
            raise RuntimeError("AppRun has an empty interpreter line")
        shell = runtime / interpreter[0].lstrip("/")
        if not shell.is_file():
            raise RuntimeError(
                "Missing Linux AppRun interpreter: " + str(shell))
        command = [str(shell), *interpreter[1:], str(entry)]
    app_args = [arg for arg in app_args if arg != "--appimage-extract-and-run"]
    os.execv(binary, [str(binary), *prefix, *command, *app_args])


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        print("run-linux: " + str(error), file=sys.stderr)
        sys.exit(1)
