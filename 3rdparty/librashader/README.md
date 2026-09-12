# librashader C header

`include/librashader.h` is the C API header of [librashader](https://github.com/SnowflakePowered/librashader)
(`librashader-capi`), copied verbatim from tag `librashader-v0.12.0` (C ABI 2, API 5).
The header is MIT licensed (see `LICENSE`). The library itself (`librashader.dll` /
`librashader.dylib`, MPL-2.0) is not vendored: it is built by the deps scripts under
`.github/workflows/scripts/{windows,macos}/` and loaded at runtime by
`pcsx2/GS/ShaderChain/LibrashaderLoader.cpp`.

To update: bump the tag here, in both deps scripts, and re-check `LIBRASHADER_CURRENT_ABI`
against `LibrashaderLoader.cpp`.
