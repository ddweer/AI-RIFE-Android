LSFG Android — Bundled SPIR-V Shaders (0.2.2, DLL-free)
=========================================================

This directory holds the precompiled SPIR-V shaders that the LSFG frame-generation
pipeline loads at runtime. No Lossless.dll or PC-side installation is required.

Directory layout:
  shaders/             FP32 DXBC-translated SPIR-V (IDs 255..302)
    255.spv ... 302.spv
  shaders/fp16/        FP16 SPIR-V variants (IDs 304..351) — optional
    304.spv ... 351.spv
  shaders/fp32/        FP32 precompiled SPIR-V (IDs 353..400) — optional
    353.spv ... 400.spv

How to add shaders:
  1. Compile your frame-generation shaders to SPIR-V (e.g. with glslangValidator,
     shaderc, or the dxvk DXBC translator).
  2. Name each file by its numeric resource ID: 255.spv, 256.spv, ..., 302.spv
     for the base FP32 set; 304..351 for FP16; 353..400 for FP32 precompiled.
  3. Place them under the matching subdirectory above.
  4. Rebuild the APK. On first launch the app copies them into
     context.cacheDir/shaders/ and the native render loop loads them from there.

If this directory is empty, the app will still install and run, but starting a
frame-generation session will fail with "Shader not found" until shaders are
bundled. Capture/passthrough (no framegen) still works.

See the main project README_源码说明.md for build instructions.