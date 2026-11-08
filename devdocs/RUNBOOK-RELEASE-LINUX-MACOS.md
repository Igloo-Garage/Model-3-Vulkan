# Linux / macOS 发布构建与打包 runbook

> 适用：`v0.9.9.6` 及以后。Windows 包走 `build.bat -package`，不在本文范围。
> 上次发布记录：`devdocs/CHANGELOG.md` 的 2026-10-31 `v0.9.8.6l` / `v0.9.8.6x` 小节；
> macOS 实机细节见 `MacOS/MacOS_performance.md`。

## 0. 谁在哪里编

| 平台 | 构建位置 | 产物 |
| --- | --- | --- |
| Linux x86_64 | **LAN 笔记本的 Ubuntu 24.04 环境**（远程 SSH，g++-14） | `Model3-Vulkan.v<ver>.for.Linux.tar.gz` |
| macOS Intel | **同一台 LAN 笔记本的 macOS 13.7.8 Intel 环境**（远程 Screen Sharing） | `Model3-Vulkan.v<ver>.for.macOS-Intel.tar.gz` |
| macOS Apple Silicon | **GitHub Actions `macos-latest`**（通用构建后 thin，或 arm64-only） | `Model3-Vulkan.v<ver>.for.macOS-AppleSilicon.tar.gz` |
| Windows x64 | 本地 `build.bat -package` | `Model3-Vulkan.v<ver>.for.Windows.zip` |

上一版（`v0.9.8.6x`）的 Linux 与 macOS Intel 包都是在这台 LAN 笔记本上**远程**编的；
macOS 两个包当时还额外走了 CI 的 **universal** 构建，再用 `llvm-lipo-18 -thin` 拆成
Intel / Apple Silicon。现在建议保持同样的分工：Linux 和 macOS Intel 在笔记本上编，
Apple Silicon 走 GitHub Actions 最稳；如果笔记本的 Zig 能编 x86_64，就在笔记本上出 Intel，CI 的
universal 包只负责 arm64。

### 0.1 远程构建主机（LAN 笔记本）

| 项目 | 值 |
| --- | --- |
| 主机 | LAN 笔记本（Intel i5-8365U / UHD 620；macOS 13.7.8 黑苹果环境 + Ubuntu 24.04 环境） |
| Linux 访问 | `ssh rex@192.168.1.67`（本机实测；或 VS Code Remote-SSH） |
| macOS 访问 | `ssh x1c7_macos@192.168.1.67`；GUI 用 macOS Screen Sharing（`vnc://192.168.1.67`） |
| 源码位置 | 笔记本**本地磁盘**（如 `~/m3v-src`），不要在 SMB/网络共享上直接编译 |
| 产物回传 | `scp` / `rsync` / SMB 到 Windows 工作站的 `v<ver>_upload/` 或 `build-package/` |

> 双系统共用同一个 IP：切换 macOS / Ubuntu 后 SSH host key 会变。用
> `ssh-keyscan -t ed25519 192.168.1.67 >> ~/.ssh/known_hosts` 追加新 key（`known_hosts` 允许同一主机
> 多条），不要长期用 `StrictHostKeyChecking=no` 绕过。

工作流：

1. Windows 工作站：改 `Src/Version.h`、`RELEASE_NOTES.md`、文档；先出 Windows 包。
2. 把当前源码同步到笔记本**本地磁盘**，或从同一个 SMB 共享**复制到本地**再编译。
   笔记本上的命令示例（路径按实际共享改）：

   ```bash
   rsync -a --delete \
     --exclude build-cmake --exclude build-package --exclude dist \
     --exclude Libraries --exclude Frameworks \
     --exclude 'v9.8.*_source' --exclude 'v9.8.*_upload' --exclude '*.log' \
     /path/to/share/ ./
   ```

   Windows 侧没有 `rsync` 时，用自带的 OpenSSH `scp` 或 `robocopy` 把树放到共享，再在笔记本上
   执行上面的命令。**不要**在 SMB 共享上直接 `cmake`/`ninja`：权限、符号链接和文件锁会让
   打包失败。`--delete` 不会删掉被 `--exclude` 排除的 `Libraries/` 与 `Frameworks/`，那正是
   我们要保留的本地构建缓存。

   **同步后先转 LF**：Windows 的 GNU tar 会原样保留 CRLF，macOS 的 bash 会把 `set -euo pipefail`
   读成 `pipefail\r`，报 `: invalid option name`。在目标机上执行一次：

   ```bash
   find . \( -name '*.sh' -o -name '*.cmake' -o -name 'CMakeLists.txt' \
            -o -name '*.txt' -o -name '*.md' -o -name '*.py' \) \
     -not -path './MSVC/*' -not -path './Libraries/*' \
     -exec perl -pi -e 's/\r\n/\n/g' {} +
   ```
3. SSH 到 Linux 环境按 §2 编译；Screen Sharing 到 macOS 环境按 §3 编译。
4. 在笔记本上生成 tar.gz 与 SHA-256，`scp`/SMB 回 Windows。
5. Windows 汇总四个资产和 `SHA256SUMS.txt`，按 §5 发布。

注意事项：

- 笔记本上的 `Frameworks/`（MoltenVK）和 `Libraries/` 是本地构建缓存，同步时排除；
  `--delete` 不要删掉 macOS 的 `Frameworks/`。
- 上一版 macOS 用过 `~/m3v-legacy-src`（含 `Src/OSD/OSX/FileSystemPath.cpp` 的 `Data/`
  修复），`~/m3v-src` 是缺该修复的副本；现在统一用当前树，不要再拿旧副本。
- 笔记本上 `roms/` 可能是符号链接；清目录前 `ls -ld roms`，不要 `rm -rf roms/*`。
- 如果笔记本在 macOS / Ubuntu 之间切换，确认当前启动的是目标系统再开工；macOS 13.x
  只能 `.st`，`.zst` 需要 macOS 14+。

### 0.2 一页速查

| # | 步骤 | 在哪执行 | 详细 |
| --- | --- | --- | --- |
| 1 | 只改 `MODEL3VULKAN_VERSION`，更新 `RELEASE_NOTES.md` / `CHANGELOG.md` | Windows | §1 |
| 2 | `build.bat -package` 出 Windows zip | Windows | §1 |
| 3 | 源码同步到笔记本**本地磁盘**（不进 SMB） | Windows → 笔记本 | §0.1 |
| 4 | 编译 + `package-cmake.sh Linux` | SSH → Ubuntu 24.04 | §2 |
| 5 | 编译 + `package-cmake.sh macOS` 后改名 `-Intel` | Screen Sharing → macOS 13.7.8 | §3 |
| 6 | `build-model3.yml` 出 universal，`llvm-lipo -thin arm64` | GitHub Actions | §4 |
| 7 | 汇总四个资产 + `SHA256SUMS.txt`，建 release 并上传 | Windows | §5 |

四份产物缺一不可；发版前按 §6 的验证矩阵每份至少跑一次。

---

## 1. 通用准备

1. **只改 `MODEL3VULKAN_VERSION`**（`Src/Version.h`）。`SUPERMODEL_VERSION` 写进存档，
   绝不要动。
2. `RELEASE_NOTES.md` 只保留**当前版本**的正文（旧版本在
   `devdocs/RELEASE-NOTES-ARCHIVE.md`）；GitHub release 的 `body_path` 直接用它。
3. 确认工作树里没有本机 ini / 日志 / 存档会被打进包；两个打包脚本都会主动排除。
4. 第三方源码（SDL3 3.2.30、Vulkan-Headers 1.3.296、zlib 1.3.1、volk）由
   `Scripts/fetch-cmake-deps.sh` 拉取；`Libraries/` 不进 git，CI 每次都会重新拉。
   脚本需要 `curl` 与 `unzip`：Ubuntu 上由 `install-linux-deps.sh` 一并安装，macOS 自带。
5. **任何 `rm -rf <dir>/*` 之前先 `ls -ld <dir>`**：上次清 `roms/` 时它是符号链接，
   顺着链接删掉了另一个安装目录的 57 个 ROM set。
6. **先过一遍 `issues_waitlist.md` 里标着「下一版发版前」的条目**。当前一条：
   存档默认延迟 3 s → 0（`Main.cpp` 的 `kDeferredLoadSeconds`），需要先跑完全游戏矩阵
   （清单在该条目里）；不通过就明确保留，并在 `saves_fix.md` 记下例外游戏。

本文的命令块统一用 `$VER`。**在每台构建机（Windows 的 bash、笔记本的 Linux、笔记本的 macOS）
上各取一次**，在仓库根目录执行：

```bash
VER=$(grep -E '^#define[[:space:]]+MODEL3VULKAN_VERSION' Src/Version.h | awk -F'"' '{print $2}')
echo "$VER"
```

PowerShell 里等价写法：

```powershell
$VER = (Select-String -Path Src\Version.h -Pattern '^#define\s+MODEL3VULKAN_VERSION').Line.Split('"')[1]
```

`<...>` 只出现在正文的命名示例里；命令块里的尖括号会被 shell 当成重定向，不要直接粘贴。

---

## 2. Linux 远程构建（LAN 笔记本）

以下命令在笔记本的 **Ubuntu 24.04** 环境执行（SSH 进去后）：

### 2.1 系统与依赖

推荐 **Ubuntu 24.04**。不要用更新的发行版或 `ubuntu-latest` 直接出包：`glibc` 基线会被
镜像带高，Steam Deck / Ubuntu 24.04 会在动态加载阶段直接退出（无窗口、无日志）。

```bash
# 基础依赖（脚本里包含 g++-14、libdecor、libwebp、SDL2、Vulkan loader 等）
echo 'PASSWORD' | sudo -S bash Scripts/install-linux-deps.sh

# .zst 存档的 HostCore（Native AOT）需要：
sudo apt install -y clang zlib1g-dev
# 另装 .NET 11 SDK（Microsoft apt 源或 dotnet-install.sh）；本机装在 ~/.dotnet
```

**`dotnet` 必须在 `cmake -S` 时就已在 PATH**（下一节）：本机是非交互 SSH 登录，不会读
`~/.bashrc`，所以先在每个会话里 `export PATH="$HOME/.dotnet:$PATH"`。

`install-linux-deps.sh` 安装的是编译期依赖；运行时只需要 Vulkan 1.3 驱动和
`libwebp`（发行包已内置字体，不再依赖发行版 CJK 字体）。

### 2.2 拉第三方源码并配置

```bash
bash Scripts/fetch-cmake-deps.sh

# HostCore 的 target 由 CMake 在 configure 时用 find_program(dotnet) 决定；
# 本机 SDK 在 ~/.dotnet，不在非交互 SSH 的 PATH 里。
export PATH="$HOME/.dotnet:$PATH"

cmake -S . -B build-cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc-14 \
  -DCMAKE_CXX_COMPILER=g++-14 \
  -DUSE_SDL2=OFF -DUSE_SDL3=ON -DSUPERMODEL_SDL3_PURE_VULKAN=ON
```

`g++-14` 是必须的：启动层用了 `<print>`，Ubuntu 24.04 默认的 GCC 13 不支持。
换过编译器或上次 configure 失败时，先 `rm -rf build-cmake` 再重新配置，否则 CMake 会沿用
缓存里的旧编译器。

### 2.3 编译与 HostCore

```bash
cmake --build build-cmake --target Model3-Vulkan --parallel
cmake --build build-cmake --target hostcore
```

`hostcore` 会 `dotnet publish` Native AOT 的 `libModel3VulkanHostCore.so` 到
`build-cmake/bin/`。**不能从 Windows/macOS 交叉编译**，必须在目标 Linux 上执行；
没有它 `.zst` 存档不可用，程序仍能运行（回退 `.st`）。

如果看到 `ninja: error: unknown target 'hostcore'`，就是 configure 时 `dotnet` 不在 PATH；
补上 `export PATH="$HOME/.dotnet:$PATH"` 后**重新 configure** 即可，不用删构建目录。

### 2.4 glibc / libstdc++ 基线

```bash
readelf -V build-cmake/bin/Model3-Vulkan \
  | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -Vu | tail -1
readelf -V build-cmake/bin/Model3-Vulkan \
  | grep -oE 'CXXABI_[0-9]+\.[0-9]+(\.[0-9]+)?' | sort -Vu | tail -1
```

在 Ubuntu 24.04 + g++-14 上，实测最高约 `GLIBC_2.38`、`CXXABI_1.3.15`、
`GLIBCXX_3.4.32`。CI 的守卫允许到 `GLIBC_2.39` / `CXXABI_1.3.15`；超过就失败，
不要放行。

### 2.5 打包

```bash
bash Scripts/package-cmake.sh Linux
```

产物：`Model3-Vulkan.v<ver>.for.Linux.tar.gz`。

脚本会：

- 复制 `build-cmake/bin/Model3-Vulkan` 和 `libModel3VulkanHostCore.so`；
- 复制 `Config/`（`Games.xml`、`Music.xml`），**删除 `*.ini` / `*.bak-*`**；
- 复制 `Assets/`、根目录 `Model3VulkanBezel.png`、`Docs/LICENSE.txt`、
  `Docs/README from The Supermodel Team.txt`、`Docs/NOTICE.md`、
  `LICENSE-ADDITIONAL-TERMS.md`、`TRADEMARK.md`；
- 复制 `README-PACKAGE.txt` → `README.txt`、`usermanual*.md`、`supported-games.txt`；
- 新增（2026-11-07）复制 `RELEASE_NOTES.md`，避免 `README.txt` 里的引用落空；
- 只对 Linux 复制 `Scripts/install-linux-deps.sh`；
- 生成空 `Data/{NVRAM,Saves,Screenshots,Cache,Analysis}` 和 `roms/README.txt`。

### 2.6 验证

```bash
tar -tzf Model3-Vulkan.v$VER.for.Linux.tar.gz \
  | grep -E 'Model3-Vulkan$|libModel3VulkanHostCore.so|Data/Config/Games.xml|usermanual'
```

解压到空目录，直接运行：

```bash
mkdir -p /tmp/m3v-linux && tar -xzf Model3-Vulkan.v$VER.for.Linux.tar.gz -C /tmp/m3v-linux
cd /tmp/m3v-linux/Model3-Vulkan.v$VER
./Model3-Vulkan -gui
```

检查 `Data/Model3Vulkan.log`：

```
Model 3-Vulkan v<ver> ...
Vulkan: using ...
PPC: x64 hybrid JIT enabled ...
Audio: device '...' has N channel(s) ...
```

从 SSH 启动时如果卡在 Wayland present，用 Xwayland：

```bash
# 变量赋值里不做通配符展开，先解析出实际路径再导出
export DISPLAY=:0
export XAUTHORITY="$(ls /run/user/1000/.mutter-Xwaylandauth.* | head -1)"
SDL_VIDEODRIVER=x11 ./Model3-Vulkan -gui
```

### 2.7 Linux 常见错误

| 症状 | 原因 / 处理 |
| --- | --- |
| 启动即退出，无窗口无日志 | glibc / GLIBCXX 基线太高；在 Ubuntu 24.04 + g++-14 重编，`readelf -V` 检查 |
| 设置窗口没有标题栏、拖不动 | 缺 `libdecor-0-dev`（GNOME Wayland） |
| 中文全是方块 / 控件在文字无 | 字体探测拒绝了无拉丁字形的字体；发行包已带 `wqy-microhei.ttc`，检查 `Data/Assets/Fonts/` |
| `.zst` 存档不可用 | `libModel3VulkanHostCore.so` 缺失或架构不对；在目标 Linux 上 `cmake --build build-cmake --target hostcore` |
| `ninja: error: unknown target 'hostcore'` | configure 时 `dotnet` 不在 PATH；导出 `~/.dotnet` 后重新 configure（不用删构建目录） |
| 从 SSH 启动卡住 | Wayland present 的测试环境问题；改用 Xwayland（见上） |
| 列表里的 ROM 路径不对 | 发行包用 `<exe>/Data/`；源码树没有 `Data/` 时回退当前目录 / XDG，确认运行目录 |

---

## 3. macOS Intel 远程构建（同一台 LAN 笔记本）

以下命令在笔记本的 **macOS 13.7.8 Intel** 环境执行（Screen Sharing 进去后）：

### 3.1 环境

- 就是 §0.1 的 LAN 笔记本，macOS 13.x（Intel / 黑苹果），可作最低验证环境。
- **`cmake` / `ninja` 是 Python 用户目录里的包**（`pip install cmake ninja`），不在非交互 SSH
  的 PATH 里（`.zprofile` 只加了 Python framework 的 bin）。每个会话先导出：

  ```bash
  export PATH="$HOME/Library/Python/3.14/bin:/Library/Frameworks/Python.framework/Versions/3.14/bin:$PATH"
  cmake --version && ninja --version
  ```

- **Apple clang 14 不支持 C++23**（没有 `<expected>` / `<print>`）。用 Python
  `ziglang` 0.17 包提供的 `~/bin/zig-cc` / `~/bin/zig-cxx` 包装器；如果你有更新的
  Xcode / Apple clang，可直接用系统编译器。
- **`dotnet` 默认没有装**（见 §3.5）。

如果 `~/bin/zig-cc` / `~/bin/zig-cxx` 不存在，可按上一版的做法包一层（按本机实际路径调整）：

```sh
mkdir -p ~/bin
cat > ~/bin/zig-cc <<'EOF'
#!/bin/sh
exec python3 -m ziglang cc "$@"
EOF
cat > ~/bin/zig-cxx <<'EOF'
#!/bin/sh
exec python3 -m ziglang c++ "$@"
EOF
chmod +x ~/bin/zig-cc ~/bin/zig-cxx
```

### 3.2 拉第三方源码与 MoltenVK

```bash
bash Scripts/fetch-cmake-deps.sh
mkdir -p Frameworks
curl -fsSL --retry 5 --retry-delay 2 \
  -o /tmp/MoltenVK-macos.tar \
  https://github.com/KhronosGroup/MoltenVK/releases/download/v1.3.0/MoltenVK-macos.tar
echo '57b4184ded521b08a63e3642552ebef8b9e98c2f0bcffa49bd93bbe1301c173d  /tmp/MoltenVK-macos.tar' \
  | shasum -a 256 -c -
tar -xf /tmp/MoltenVK-macos.tar -C /tmp
cp /tmp/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib Frameworks/
cp /tmp/MoltenVK/MoltenVK/dynamic/dylib/macOS/MoltenVK_icd.json Frameworks/
cp /tmp/MoltenVK/LICENSE Frameworks/LICENSE-MoltenVK.txt
```

`Scripts/package-cmake.sh macOS` 只有在 `Frameworks/` 存在时才复制 MoltenVK；缺了它
macOS 包无法启动。

### 3.3 配置与编译（x86_64）

```bash
cmake -S . -B build-cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
  -DUSE_SDL2=OFF -DUSE_SDL3=ON -DSUPERMODEL_SDL3_PURE_VULKAN=ON \
  -DCMAKE_C_COMPILER=$HOME/bin/zig-cc \
  -DCMAKE_CXX_COMPILER=$HOME/bin/zig-cxx

cmake --build build-cmake --target Model3-Vulkan --parallel
```

换编译器（Apple clang ↔ Zig）或上次 configure 失败时，先 `rm -rf build-cmake`：CMake 会把
编译器路径写进缓存，直接重配会继续报旧的错误。

Zig 的 clang 驱动在 Release 下把 `-Wdate-time` 当成错误，而启动横幅故意使用 `__DATE__` /
`__TIME__`。`CMakeLists.txt` 现在给 Clang 目标加了 `-Wno-date-time`；如果你在旧树上看到
`expansion of date or time macro is not reproducible [-Werror,-Wdate-time]`，临时加
`-DCMAKE_CXX_FLAGS=-Wno-date-time` 也能编过。

上一版用 `build-zig`；本文统一用 `build-cmake`，因为 `package-cmake.sh` 固定从
`build-cmake/bin` 取文件。如果你坚持 `build-zig`，打包前把 `Model3-Vulkan` 复制到
`build-cmake/bin/`。

### 3.4 修 deployment target

Zig 默认按主机系统写 `minos`（上一版主机是 13.7.8，会拦住 13.0–13.6）。构建后检查：

```bash
xcrun vtool -show-build build-cmake/bin/Model3-Vulkan
```

如果 `minos` 高于 13.3：

```bash
xcrun vtool -set-build-version macos 13.0 13.3 -output /tmp/M3V build-cmake/bin/Model3-Vulkan
mv /tmp/M3V build-cmake/bin/Model3-Vulkan
chmod +x build-cmake/bin/Model3-Vulkan
```

macOS 13 的 CLT 里 `vtool` **不接受只写 `-replace`**（会报 `error: one output file must be
specified`），必须用 `-output` 再覆盖。`vtool` 在 Command Line Tools 里，用 `xcrun` 调避免
PATH 差异。

### 3.5 HostCore（x64）

**这台笔记本没有装 .NET SDK**（2026-11-07 实测）。两条路：

1. **复用上一版 CI 的 universal dylib**（HostCore 源码自 2026-10-28 未改，导出符号一致）：

   ```bash
   cp ~/m3v-986x/Model3-Vulkan.v0.9.8.6x/libModel3VulkanHostCore.dylib build-cmake/bin/
   nm -gU build-cmake/bin/libModel3VulkanHostCore.dylib | grep HostCore_
   ```

2. 或在目标系统装 .NET 11 SDK 后自己发布：

   ```bash
   dotnet publish HostCore/Model3VulkanHostCore.csproj -c Release -r osx-x64 \
     -o HostCore/publish-x64 -p:AppleMinOSVersion=13.3
   cp HostCore/publish-x64/libModel3VulkanHostCore.dylib build-cmake/bin/
   ```

该 dylib 需要 macOS 14+ 的 Swift 运行库；在 13.x 上启动会打印
`host core unavailable ... _swift_getTypeByMangledNameInContext2` 并回退 `.st`，与上一版行为一致。

（也可以用 `cmake --build build-cmake --target hostcore`；CMake 会按
`CMAKE_OSX_DEPLOYMENT_TARGET` 传 `AppleMinOSVersion`，但仍需要 dotnet。）

### 3.6 打包与命名

```bash
bash Scripts/package-cmake.sh macOS
# 产物：Model3-Vulkan.v$VER.for.macOS.tar.gz
mv Model3-Vulkan.v$VER.for.macOS.tar.gz \
   Model3-Vulkan.v$VER.for.macOS-Intel.tar.gz
```

如果你之后用 CI 的 universal 包拆包，则不要在这里重命名；统一在拆包后命名。

### 3.7 验证

```bash
file build-cmake/bin/Model3-Vulkan
lipo -info build-cmake/bin/Model3-Vulkan
otool -L build-cmake/bin/Model3-Vulkan
xcrun vtool -show-build build-cmake/bin/Model3-Vulkan
```

解压 tar，运行 `./Model3-Vulkan -print-games` 做冒烟（SSH 会话跑 `-gui` 连不到桌面窗口）。
macOS 上日志写在 **exe 旁边**：`./Model3Vulkan.log`（不是 `Data/`），先 grep 版本横幅与设备：

```bash
grep -E "Model 3-Vulkan|Vulkan: using|MoltenVK|host core" Model3Vulkan.log | head
```

macOS 13.x 上 `.zst` 会回退 `.st`，这是预期；macOS 14+ 才有 HostCore 的 Swift 运行库。

### 3.8 macOS 常见错误

| 症状 | 原因 / 处理 |
| --- | --- |
| 编译报 `<expected>` / `<print>` 找不到 | Apple clang 14；改用 Zig 包装器或更新 Xcode |
| `cmake` / `ninja: command not found` | 在 `~/Library/Python/3.14/bin`；按 §3.1 导出 PATH |
| `fetch-cmake-deps.sh: invalid option name` | 源码是 CRLF；按 §0.1 转 LF |
| `expansion of date or time macro ... [-Werror,-Wdate-time]` | Zig Release；CMakeLists 已加 `-Wno-date-time` |
| `use of undeclared identifier '_stricmp'` | 新代码用了 MSVC 专有函数；用 `CaseInsensitiveCompare`（已在源码修复） |
| 本机 Zig 编 arm64 失败（CoreFoundation 头 / `FLT_MAX`） | 不要在本地交叉编 arm64；arm64 走 CI（见 §4） |
| `vtool ... one output file must be specified` | macOS 13 的 `vtool` 要用 `-output`，见 §3.4 |
| 启动报 `VkResult -7` | 不是构建问题；确认代码包含 `VK_KHR_portability_enumeration` 的按需启用修复 |
| `host core unavailable ... _swift_getTypeByMangledNameInContext2` | macOS 13 缺新 Swift 运行库；预期，自动回退 `.st` |
| 控制台刷 `[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Metal does not support disabling primitive restart` | MoltenVK 对 `primitiveRestartEnable = VK_FALSE` 的 strip 管线警告；源码已在 5 处 strip 管线设 `VK_TRUE`，旧树可照做，渲染不变 |
| `.zst` 在 macOS 13 不可用 | 预期；HostCore 需要 macOS 14+，自动回退 `.st` |
| 包启动找不到 `Data/` | 确认 `package-cmake.sh` 生成的布局是 `<exe>/Data/`；不要在别的目录启动 |
| 设置窗口没有标题栏 | 与 Linux 不同，macOS 由 Cocoa 提供；检查是否从正确的 `.app`/终端启动 |
| `vtool` 显示 minos 13.7.8 | 用 §3.4 的 `xcrun vtool -set-build-version ... 13.0 13.3 -output ...` |

---

## 4. macOS Apple Silicon（GitHub Actions）

### 4.1 为什么用 CI

上一版**笔记本上的 Zig** 编 arm64 失败：
`-target aarch64-macos` 在 SDL3 的 hidapi 上找不到 `CoreFoundation` 头，补
`-isysroot`/`-iframework` 后又卡在 imgui 的 `FLT_MAX`。CI 的 macOS runner 用系统
Xcode 工具链，直接出 universal 包，最稳。

### 4.2 触发构建

`build-model3.yml` 的 macOS job 本来就是（节选，不是完整 YAML）：

```yaml
runs-on: macos-latest
- CMAKE_OSX_ARCHITECTURES="x86_64;arm64"
- CMAKE_OSX_DEPLOYMENT_TARGET=13.3
- MoltenVK 1.3.0
- HostCore x64 + arm64，lipo -create 成 universal
```

两种触发方式：

```bash
# 1) 手动跑当前分支
gh workflow run build-model3.yml
gh run list --workflow build-model3.yml --limit 5

# 2) 推 tag "v$VER"（release.yml 会调用同一个 workflow）
#    注意：tag 触发也会尝试创建 release，见 §5。
```

下载 macOS artifact：

```bash
# 最近一次 macOS 构建的 run id
RUN=$(gh run list --workflow build-model3.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run download "$RUN" -n Model3-Vulkan-macOS -D /tmp/m3v-macos
# 得到 Model3-Vulkan.v$VER.for.macOS.tar.gz（universal）
```

### 4.3 拆成 Apple Silicon / Intel

```bash
mkdir -p /tmp/m3v-thin && tar -xzf /tmp/m3v-macos/Model3-Vulkan.v$VER.for.macOS.tar.gz -C /tmp/m3v-thin
cd /tmp/m3v-thin/Model3-Vulkan.v$VER

# Homebrew 的 llvm 会把二进制装成 llvm-lipo / llvm-lipo-18 两种名字；
# 都没有时回退到 Xcode 自带的 lipo（同样支持 -thin）。
LIPO="$(command -v llvm-lipo || command -v llvm-lipo-18 || xcrun -f lipo)"
echo "using $LIPO"
for f in Model3-Vulkan libModel3VulkanHostCore.dylib Frameworks/libMoltenVK.dylib; do
  "$LIPO" -thin arm64 "$f" -output "$f.arm64"
  mv "$f.arm64" "$f"
done
lipo -info Model3-Vulkan
lipo -info libModel3VulkanHostCore.dylib
lipo -info Frameworks/libMoltenVK.dylib

cd /tmp/m3v-thin
tar -czf Model3-Vulkan.v$VER.for.macOS-AppleSilicon.tar.gz Model3-Vulkan.v$VER
```

Intel 包同理：把 `arm64` 换成 `x86_64`，输出
`Model3-Vulkan.v<ver>.for.macOS-Intel.tar.gz`。**三个二进制都要 thin**：
主程序、HostCore、MoltenVK；只 thin 主程序会让 `.zst` 或 Vulkan 在另一种架构上失败。
`MoltenVK_icd.json` 是文本，不用处理。对已经是单架构的文件再 `-thin` 会报错，属正常。

本地 Intel 构建（§3）不需要 thin：HostCore / MoltenVK 保持 universal 也能在 Intel 上加载；
只有从 CI 的 universal 包拆架构时才要把三个都拆。

**两个包要从同一份 universal tar 各自解压一次**：上面是在原目录里就地替换，thin 成 arm64
之后文件里已经没有 x86_64，不能接着在同一目录再拆 Intel。

### 4.4 只出 arm64（可选）

如果你不想先在 CI 出 universal 再拆，可以在一根临时分支上把 workflow 的
`-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"` 改成 `arm64`，然后：

```bash
gh workflow run build-model3.yml --ref release/apple-silicon
gh run list --workflow build-model3.yml --limit 3   # 等新的 run 出现再取 id
RUN=$(gh run list --workflow build-model3.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch "$RUN" --exit-status
gh run download "$RUN" -n Model3-Vulkan-macOS -D /tmp/m3v-arm64
mv /tmp/m3v-arm64/Model3-Vulkan.v$VER.for.macOS.tar.gz \
   Model3-Vulkan.v$VER.for.macOS-AppleSilicon.tar.gz
# 构建完删除临时分支
```

Intel 包仍由本地 macOS Intel 构建（§3）。两种方案二选一，不要同时用同一个版本号
覆盖对方。

### 4.5 验证

```bash
lipo -info Model3-Vulkan
file Model3-Vulkan libModel3VulkanHostCore.dylib Frameworks/libMoltenVK.dylib
```

在 M 系列 Mac 上解压运行 `./Model3-Vulkan -gui`，检查：

- 启动横幅版本；
- `Vulkan: using ...`（MoltenVK）；
- `PPC: x64 hybrid JIT enabled`；
- `Data/Model3Vulkan.log` 无 `[Error]`；
- macOS 14+ 下 `.zst` 保存/读取正常；13.x 下自动 `.st`。

---

## 5. 发布资产与 SHA256SUMS

### 5.1 命名

```
Model3-Vulkan.v<ver>.for.Windows.zip
Model3-Vulkan.v<ver>.for.Linux.tar.gz
Model3-Vulkan.v<ver>.for.macOS-Intel.tar.gz
Model3-Vulkan.v<ver>.for.macOS-AppleSilicon.tar.gz
SHA256SUMS.txt
```

### 5.2 校验和

```bash
sha256sum \
  Model3-Vulkan.v$VER.for.Windows.zip \
  Model3-Vulkan.v$VER.for.Linux.tar.gz \
  Model3-Vulkan.v$VER.for.macOS-Intel.tar.gz \
  Model3-Vulkan.v$VER.for.macOS-AppleSilicon.tar.gz \
  > SHA256SUMS.txt
cat SHA256SUMS.txt
```

macOS 没有 `sha256sum` 时用 `shasum -a 256`。四个资产通常汇总在 Windows 上，PowerShell 等价写法：

```powershell
$ver = (Select-String -Path Src\Version.h -Pattern '^#define\s+MODEL3VULKAN_VERSION').Line.Split('"')[1]
$lines = Get-FileHash "Model3-Vulkan.v$ver.for.*.*" -Algorithm SHA256 |
  ForEach-Object { "{0}  {1}" -f $_.Hash.ToLower(), (Split-Path $_.Path -Leaf) }
[IO.File]::WriteAllText("$PWD\SHA256SUMS.txt", ($lines -join "`n") + "`n")
```

`SHA256SUMS.txt` 用两空格分隔、**LF 换行**（`sha256sum` 的格式）。`Out-File` 会写 CRLF，
别人在 Linux 上跑 `sha256sum -c` 会报 `\r` 问题，所以上面用 `WriteAllText`。

### 5.3 GitHub release

`release.yml` 在 `v*` tag 上调用 `build-model3.yml`，并用
`body_path: RELEASE_NOTES.md` 创建 release；它只上传 `artifacts/*/*.tar.gz`
（Linux/macOS），**Windows zip 需要手动上传**。

上一版的稳妥做法：

```bash
# 0) 查 workflow id / 名字（release.yml 的 name 是 "Build and Release"）
gh workflow list

# 1) 先禁用 tag 触发的 release workflow（用未预期源码建 release 会出错）
gh workflow disable "Build and Release"     # 或 gh workflow disable <id>

# 2) 手动建 release（tag 名必须等于 Src/Version.h 的 MODEL3VULKAN_VERSION）
gh release create "v$VER" --target master \
  --title "Model 3-Vulkan v$VER" \
  --notes-file RELEASE_NOTES.md

# 3) 上传四个资产 + SHA256SUMS.txt
gh release upload "v$VER" \
  Model3-Vulkan.v$VER.for.Windows.zip \
  Model3-Vulkan.v$VER.for.Linux.tar.gz \
  Model3-Vulkan.v$VER.for.macOS-Intel.tar.gz \
  Model3-Vulkan.v$VER.for.macOS-AppleSilicon.tar.gz \
  SHA256SUMS.txt

# 4) 恢复 workflow
gh workflow enable "Build and Release"
```

恢复后**不要**再手动 re-run 该 workflow：它会重新跑 CI 并尝试覆盖已经上传的资产。

如果走 CI 自动 release，确认 `RELEASE_NOTES.md` 只含当前版本，且 CI 产物名与
`SHA256SUMS.txt` 对得上；Windows zip 仍要手动补传。

---

## 6. 验证矩阵

| 平台 | 最低验证 | 检查点 |
| --- | --- | --- |
| Linux | Ubuntu 24.04 + Intel UHD 620 + Xwayland | 启动横幅版本、游戏列表、进游戏画面、截图、`Data/Model3Vulkan.log` 无错误 |
| macOS Intel | 13.7.8 + MoltenVK 1.3.0 | 设置窗口、ROM 识别、Scud Race 运行、游戏内 Tab 面板鼠标/键盘、`.st` 回退 |
| macOS Apple Silicon | M 系列 + macOS 14+ | 启动、Vulkan 设备、`.zst` 保存/读取、设置窗口 |
| 通用 | 每个包解压到空目录 | 无 ini/日志/存档；`Data/Config/Games.xml`、`Assets/Boxarts`、字体、机柜图齐全 |

---

## 7. 故障排查速查

| 现象 | 先看 |
| --- | --- |
| Linux 启动即退出 | `readelf -V` 的 GLIBC/CXXABI；是否在 Ubuntu 24.04 + g++-14 构建 |
| Linux 无标题栏 | `libdecor-0-dev`；GNOME Wayland |
| Linux 中文方块 | `Data/Assets/Fonts/wqy-microhei.ttc`；`ProbeFontFile()` 拒绝无拉丁字形 |
| Linux `.zst` 不可用 | `libModel3VulkanHostCore.so` 是否在 exe 旁；是否在 Linux 上 `target hostcore` |
| macOS `VkResult -7` | 代码里的 portability enumeration 修复；不是驱动/显卡问题 |
| macOS `.zst` 不可用 | macOS 13.x 预期回退 `.st`；14+ 检查 dylib/`AppleMinOSVersion=13.3` |
| macOS 编译 C++23 失败 | Apple clang 14 → Zig 包装器或更新 Xcode |
| CI arm64 失败 | 不要在本地 Zig 交叉编；用 `macos-latest` |
| 包内出现 ini/日志 | `package-cmake.sh` 的 `find ... -delete` 和 `*.ini` 过滤；检查 `Data/Config` |
| `roms/` 清理误删 | 先 `ls -ld roms`；符号链接不能 `rm -rf roms/*` |

---

## 8. 文件地图

| 文件 | 用途 |
| --- | --- |
| `Src/Version.h` | `MODEL3VULKAN_VERSION`（发版只改这里） |
| `Scripts/install-linux-deps.sh` | Ubuntu 依赖安装 |
| `Scripts/fetch-cmake-deps.sh` | 拉 SDL3 / Vulkan-Headers / zlib / volk |
| `Scripts/package-cmake.sh` | Linux/macOS 打包（含 `RELEASE_NOTES.md`） |
| `CMakeLists.txt` | `hostcore` 目标、`AppleMinOSVersion` 处理 |
| `.github/workflows/build-model3.yml` | Linux + macOS CI（universal、MoltenVK、HostCore） |
| `.github/workflows/release.yml` | tag 触发的 release（`body_path: RELEASE_NOTES.md`） |
| `MacOS/MacOS_performance.md` | macOS 实机验证、`VkResult -7`、`.zst` 14+ 记录 |
| `devdocs/CHANGELOG.md` | 2026-10-31 `v0.9.8.6l` / `v0.9.8.6x` 的原始发布记录 |
| `v9.8.6x_upload/` | 上一版三个资产、`SHA256SUMS.txt`、发布正文（历史参考） |
