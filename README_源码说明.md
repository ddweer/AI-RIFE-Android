# LSFG-Android-0.2.2 源码适配包（DLL-free，纯 Android AI 插帧）

本文件夹基于 `LSFG-Android-0.2.1-rife-arm64-debug.apk` 的源码树，做了 **0.2.2 DLL-free 适配**，彻底去掉了对 PC 端 `Lossless.dll` 的依赖，改为在 Android 端直接使用嵌入的 SPIR-V 着色器，适配红米 Note 7 Pro（骁龙675、出厂Android 9/API 28、arm64-v8a、Adreno 612）等中低端设备。

## 核心变更：彻底去掉 DLL 依赖

| 项目 | 0.2.1（有 DLL） | 0.2.2（DLL-free） |
|------|----------------|-------------------|
| 着色器来源 | 从用户选取的 PC 端 `Lossless.dll` 提取 DXBC → SPIR-V | 从 `src/main/assets/shaders/` 直接加载预编译 SPIR-V |
| DLL 选择界面 | `DllPickerScreen.kt`（必选步骤） | 已移除，不再要求用户选 DLL |
| ShaderExtractor | 从 DLL 文件拷贝、PE 解析、DXBC 转译 | 已移除，改为 assets 资源复制 |
| C++ 依赖 | pe-parse、dxbc（PE 解析 + DXBC→SPIR-V） | 已移除，只保留 volk + framegen + 原生渲染循环 |
| JNI | `extractShaders(dllPath, sha, cacheDir)` | 已移除，不再需要 |
| 首次启动 | 用户手动选 DLL 并提取 | 自动从 `assets/shaders/` 复制 SPIR-V 到 `cacheDir/shaders/` |

## 0.2.2 兼容性适配（相对 0.2.1）

| 项目 | 0.2.1 | 0.2.2 适配版 | 目的 |
|------|-------|-------------|------|
| 版本号 | 0.1.2 | **0.2.2** | 标识适配版本 |
| versionCode | 1 | **2** | 升级标识 |
| **minSdk** | 29 (Android 10) | **21 (Android 5.0)** | 红米 Note 7 Pro 出厂为 Android 9 (API 28)，原 minSdk 29 会直接禁止安装 |
| targetSdk | 35 (Android 15) | **34** | 降低对旧系统的行为限制 |
| compileSdk | 35 | **34** | 与 targetSdk 对齐 |
| NDK 平台 | android-29 | **android-21** | C++ 原生库面向更低系统版本编译 |
| **ABI** | arm64-v8a + x86_64 | **仅 arm64-v8a** | 红米 Note 7 Pro 为骁龙675 (arm64-v8a)，去掉 x86_64 减小 APK |
| **Vulkan feature** | required="true" | **required="false"** | 先允许安装，运行时再检测 Vulkan，不可用时界面提示 |

## 目录结构

```
LSFG-Android-0.2.1-source/
├── README_源码说明.md          （本文件）
├── LICENSE
├── README.md
├── .gitignore
├── .gitmodules
├── LSFG-Android-Application/   # Android 应用工程
│   ├── build.gradle.kts        (minSdk=21, targetSdk=34, versionName=0.2.2)
│   ├── settings.gradle.kts
│   ├── gradle.properties
│   ├── gradlew / gradlew.bat
│   ├── gradle/
│   └── app/
│       ├── build.gradle.kts    (compileSdk=34, ndkPlatform=android-21, ABI=arm64-v8a only)
│       └── src/main/
│           ├── AndroidManifest.xml       (Vulkan required="false")
│           ├── assets/
│           │   └── shaders/              # 放入预编译 SPIR-V 文件（见下文）
│           │       ├── README.txt
│           │       ├── 255.spv ... 302.spv   (FP32 DXBC set)
│           │       ├── fp16/ 304.spv ... 351.spv   (FP16, optional)
│           │       └── fp32/ 353.spv ... 400.spv   (FP32 precompiled, optional)
│           ├── java/com/lsfg/android/  # Kotlin 源码（已移除所有 DLL 引用）
│           ├── cpp/                    # C++ JNI/渲染源码（已移除 pe-parse/dxbc）
│           ├── res/                    # 资源（strings.xml 已移除 DLL 字符串）
│           └── aidl/com/lsfg/android/shizuku/
├── lsfg-vk-android/            # Vulkan LS-FG 原生库（C++，保留）
├── staging_edit/               # 汉化暂存（参考用，未合并）
└── staging_rife/               # RIFE 原生代码暂存（参考用，未合并）
```

## 编译要求

- Android Studio（建议最新稳定版）
- Android SDK：compileSdk 34，build-tools 对应版本
- NDK：r27.0.12077973（`app/build.gradle.kts` 中 `ndkVersion`）
- CMake：3.22.1
- ABI：仅构建 **arm64-v8a**

编译命令示例（在 `LSFG-Android-Application` 目录下）：

```
.\gradlew.bat assembleDebug
```

或在 Android Studio 中直接打开 `LSFG-Android-Application` 文件夹作为项目。

## 关于着色器（SPIR-V）文件

`src/main/assets/shaders/` 目录目前为空（仅含 `README.txt` 说明）。你需要：

1. 将你的帧生成着色器编译为 SPIR-V 格式（`.spv`）。
2. 按资源 ID 命名：`255.spv`、`256.spv`、`...`、`302.spv`（基础 FP32 集）。
3. 可选：`shaders/fp16/304.spv`..`351.spv`（FP16 变体），`shaders/fp32/353.spv`..`400.spv`（FP32 预编译变体）。
4. 放入对应子目录后重新打包 APK。

首次启动时，APP 会自动将 `assets/shaders/` 下的文件复制到 `context.cacheDir/shaders/`，原生渲染循环从此目录加载着色器。若目录为空，捕获/直通（passthrough）模式仍可用，但启动帧生成会话会报 "Shader not found"。

## 红米 Note 7 Pro 编译/安装建议

1. 用 Android Studio 打开 `LSFG-Android-Application` 文件夹，直接 `assembleDebug`。
2. 将 APK 传至红米 Note 7 Pro，开启“未知来源安装”后安装。
3. 首次运行若提示 Vulkan 不可用，说明该机 ROM 的 Vulkan 驱动有限制；此时仍可使用 MediaProjection 捕获路径（画面捕获不依赖 Vulkan，但 AI 插帧需要 Vulkan 计算着色器）。

## 注意

- 已从副本中排除所有 `.git` 目录，保持为纯源码树。如需完整 Git 历史，请参考原路径 `工作区/汉化插帧项目/work/LSFG-Android-src`。
- `staging_edit/` 和 `staging_rife/` 中的汉化与 RIFE 原生代码**尚未合并**进主工程，仅作为参考保留。如需合并，请手动将对应文件复制进 `LSFG-Android-Application/app/src/main/`。
- 本副本仅供学习/修改参考，请遵守原项目 LICENSE 及相关开源协议。
