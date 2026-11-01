# AutoMine（LeviLamina 客户端模组源码）

客户端自动挖掘模组：以本地玩家为中心，自动挖掘半径 5（11×11×11）内除基岩/流体外的所有方块。

> 本目录是**源码工程**，需要在 Windows 上自行编译出 `auto-mine.dll` 才能使用。
> 当前交付环境是 Linux，无法编译 LeviLamina 模组。

## 功能与参数

- 半径：5 格（立方体范围）
- 方块筛选：跳过空气、水/岩浆等流体、硬度 < 0 的不可破坏方块（基岩、屏障、命令方块等）
- 触及限制：只挖距眼睛 5.2 格以内的方块（更远的方块服务器会拒绝）
- 实现方式：订阅 `ClientLevelTickEvent`，每 tick 驱动本地玩家的 `GameMode`
  （`startDestroyBlock` / `continueDestroyBlock` / `stopDestroyBlock`），由客户端向服务器发包
- 行为：进入世界后**自动开始**，无需任何按键

参数都在 [AutoMineMod.cpp](src/mod/AutoMineMod.cpp) 顶部的匿名命名空间里：

```cpp
constexpr int    kRadius             = 5;   // 挖掘半径
constexpr double kMaxReach           = 5.2; // 触及距离
constexpr int    kTargetTimeoutTicks = 400; // 单方块超时
constexpr uchar  kFace               = 1;   // 挖掘面
```

## 编译环境

- Windows 10/11 x64
- Visual Studio 2022，安装「使用 C++ 的桌面开发」工作负载（MSVC v143 + Windows SDK）
- [xmake](https://xmake.io/) 2.9+（含 xmake-vscode 可选）
- Git（xmake 会拉取 levimc-repo 和 levilamina 包）
- 与你的 Minecraft 基岩版版本匹配的 LeviLamina **client** 版本

## 用 GitHub Actions 编译（无需本地环境）

仓库里已带好 `.github/workflows/build.yml`：

1. 在 GitHub 新建一个仓库，把本目录全部文件上传（网页端 “uploading an existing file” 即可）
2. 打开仓库的 **Actions** 页，左侧选择 “Build AutoMine (client, win-x64)”
3. 点 **Run workflow** → 选 `main` 分支 → 运行
4. 等 3–5 分钟，在运行详情页底部下载 **Artifacts → auto-mine-win-x64**
5. 解压后得到 `auto-mine.dll` + `manifest.json`，放进 `mods/auto-mine/`

工作流使用 `windows-latest`（自带 VS2022 与 Windows SDK），通过 `xmake-io/github-action-setup-xmake` 安装 xmake，
自动拉取 levimc-repo 与 LeviLamina client 包。

## 本地编译步骤

在项目根目录执行：

```bash
xmake f --target_type=client -m release
xmake
```

产物位置（大致）：

```
build/windows/x64/release/auto-mine.dll
build/windows/x64/release/auto-mine/        # modpacker 打好的目录（含 manifest.json）
```

如果 `add_requires("levilamina")` 拉到的版本和你的游戏版本不匹配，
在 `xmake.lua` 里写死版本，例如：

```lua
add_requires("levilamina 26.20.4", {configs = {target_type = get_config("target_type")}})
```

版本必须与 LeviLauncher / LeviLamina 客户端所支持的 Bedrock 版本一致，
否则会在加载时因符号不匹配而崩溃。

## 安装到 LeviLauncher

把编译出的 `auto-mine.dll` 和 `manifest.json` 放到客户端模组目录：

```
<游戏目录或 LeviLauncher 的 mods 目录>/mods/auto-mine/
    auto-mine.dll
    manifest.json
```

然后启动游戏即可。若使用 modpacker 生成的 `auto-mine/` 目录，直接整个拷过去即可。

## 注意事项

1. **开启即自动挖**：进入世界后会立刻开始清除周围方块，包括脚下的方块，可能导致坠落/掉入洞穴。
2. **服务器风险**：自动挖掘属于作弊类行为，公共服务器可能拒绝、踢出或封禁账号。建议只在自己开的单人/局域网世界使用。
3. **触及限制**：服务端对挖掘距离有校验，半径 5 的立方体中超出 5.2 格的部分会被服务器忽略（代码中已跳过，避免刷无效包）。
4. 若编译报错提示找不到某些 `mc/...` 头文件或函数签名不一致，说明 LeviLamina 版本与该源码不匹配，需要按对应版本的 SDK 调整 include/函数签名。
5. 生存模式下挖硬度较高的方块需要时间，模组每 tick 只推进一个方块；创造模式会更快。
