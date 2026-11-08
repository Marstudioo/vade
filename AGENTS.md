# AGENTS.md — Vade 开发作业手册

> 给编码 agent / 新开发者：先读 `docs/DESIGN.md`（长什么样），再读本文（怎么干活）。

## 构建（WSL Ubuntu）

```bash
cd /mnt/e/Hermes_Workspace/vade
bash /mnt/e/DSH_Workspace/vade_build.sh          # 全流程：configure→build→deb
# 产物：dist/Vade_<ver>_rev1_arm64.deb
```

- 工具链：cmake preset `cp0-cross-package`（Ninja Multi-Config，aarch64 交叉编译）。
- `.cache/` 是目录联接 → `cz0-template/.cache`（BSP+依赖，勿动、勿复制）。
- **坑**：勿用 `cp -al` 复制含符号链接的 sysroot（丢软链 → ld 找不到 libc）；`cmd | tail` 会吞退出码，用 `${PIPESTATUS[0]}`。

## 桌面预览（UI 迭代，不碰真机）

```bash
cmake --preset linux-x86-64 && cmake --build --preset linux-x86-64-rel
./build/linux-x86-64/Release/vade     # SDL 窗口 320×170 真实视口
```
验收：双主题 + 键 4~8 导航 + 原生视口文本裁切（官方 ui-feature skill 的验收清单）。

## 部署与真机测试

**调试铁律（Mars 2026-01-09 定）：每次真机调试后必须收尾——关闭 vade 进程 + 恢复 APPLaunch 启动器**
（`bash /tmp/vade_restore.sh`），严禁让测试界面残留在屏幕。vade_test.sh 已内置强制收尾；
手动测试后同样执行。远程测试三件套：`vade_test.sh`（停启动器→起应用→evdev 注入→截屏→**自动收尾**）、
`vade_keytype.py`（按键注入，支持 `hold:esc` 长按）、`vade_restore.sh`（恢复启动器）。

```powershell
# adb 在 E:\DSH_Workspace\platform-tools\adb.exe（WSL 里起不来，用 PowerShell）
adb push dist/Vade_*.deb /tmp/
adb shell 'dpkg -i /tmp/Vade_*.deb'
# 截屏验证：adb shell 'cat /dev/fb0 > /tmp/s.raw' → 拉回用 E:\DSH_Workspace\fb_decode.py
```

## 代码结构（MVVM 铁律，来自官方 ui-feature skill）

| 层 | 位置 | 规则 |
|---|---|---|
| 持久状态 | `src/model` | 不放 LVGL 类型 |
| UI 动作/可观察状态 | `src/viewmodel` | 经 subject 发布 |
| 绑定助手 | `src/reactive` | 先复用 bind_*/observe_obj，勿造轮子 |
| 屏幕/组件 | `src/view` | BaseScreen/BaseWidgets 派生；320×170 常量勿改 |
| 硬件行为 | `src/platform` | 桌面/设备用编译宏隔离 |

数据流：`input → widget 回调 → ViewModel action → Model 更新 → subject → 绑定刷新`。
新页面 6 步：AppPage 枚举 → ViewModel → Screen 子类 → ScreenManager 注册 → 导航图标/键位 → 帮助文本。

## 命名与打包

- 五个身份分开：显示名 `Vade` / 包名 `vade` / 配置标识 `vade` / 图标基名 `vade` / 分享码 `VADE`。
- 改名对照官方 `cardputerzero-template-customization` skill 的 identity-map；改完跑：
  `cmake -P .agents/skills/cardputerzero-package-release/scripts/validate_app_builder.cmake`
- **严禁**硬编码 /dev/fb0（真机显示拓扑以实测为准：fb0 MIPI 320×170 RGB565）。

## 发布（占坑链路）

```bash
cd /mnt/e/Hermes_Workspace/cz0-appbuilder
PYTHONPATH=scripts python3 -m czdev publish --deb /mnt/e/Hermes_Workspace/vade/dist/Vade_*.deb
```
- 凭据：WSL `~/.czdev/credentials`（classic 令牌，scope=repo+admin:public_key，勿勾 user:email）。
- 网络：github.com 被墙；git 已全局重写走 ssh.github.com:443；api/uploads 直连。
- PR 需附实机演示视频；首发人工审核；版本必须递增（`czdev bump`）。

## 官方硬规则合规清单（cardputer.cc/#/documents/cp0-dev，2026-01-09 通读）

**文档取材三层铁律（官方人员 2026-01-09 提醒）**：官方 GitHub 仓库存有**早期 AI 生成的过时文档**（官网入口已隐藏但文件仍在，AI 会拉到）。取材只认：①官网**有入口**的文档 ②真机实测 ③官方人员口径；旧文档仅作历史资料，**与实测/官方口径冲突时以后者为准**。已知作废项：CP0_keys 的"长按 TAB=AI 语音"（实际=锁屏，talk 从未实现）。

原文档已存 `E:\DSH_Workspace\vade_docs\cp0dev\`（80 份，zh-CN CP0_* 是硬规则正典）。要点：

- **deb 文件名**：`packagename_version-revision_arm64.deb`（如 `vade_0.2.1-rev1_arm64.deb`），大小写/连字符都算校验。
- **元数据**：app_name 商店唯一；package_name 小写+`.-+`（发布后不可改）；share_code 4 位字母数字（发布后不改）；
  分类只能从 13 枚举选 1~2；权限 7 项必须全部显式 true/false；summary 默认英文，中日文进 store.locales。
- **图标**：方 PNG 128~512（荐 256），**不建议自带圆角/边框**；**截图 4 张 320×170 PNG**（上架硬指标）。
- **字体**：系统已装 DejaVu / JetBrains Mono / Noto CJK——**"注意正确选择地区语言变体"**（我们用 SC face ✓）；≥12px、Regular 及更粗。
- **文件位置**：设置文件 `$HOME/.config/vade/` ✓。
- **运行**：禁 root；GUI 默认 `Terminal=false`；`.desktop` 的 `Sysplause=true`（默认）=独占 framebuffer，后台服务才 false。
- **键位**：Esc 短按返回/长按回 HOME（launcher 层还会 SIGTERM 双保险）；**长按 TAB = 系统锁屏**（勿占用；旧文档称 AI 语音入口，已作废）；
  **官方推荐：非字母输入态直接用 F/Z/X/C 当方向键**（不必 fn）；底部 5 键图标中心 x=49/105/161/217/273、y=153、22×22。
- **隐私**：提交政策要求声明七项（访问哪些数据/为何/是否离机/给谁/存多久/可删否/第三方共享）——写进 store.description；
  网络+文件系统+云服务=高敏感应用，审核更严，须声明源码开放程度与已知风险（高 CPU/存储/电量影响）。
- **首发流程**：czdev publish → PR 评论区附**实机演示视频（启动+主要功能+退出，三段都要）** → 人工审核；更新版自动合并。
- **显示**：320×170 RGB565；fb 设备读 `APPLAUNCH_LINUX_FBDEV_DEVICE` 环境变量（fallback /dev/fb0），严禁硬编码。
- **实测差异存档**：官方 tech-specs 写电池 1500mAh/IMU=LSM6DS3，与实机文档（1750mAh/BMI270）不一致——以实机为准。

## 提交规范

Conventional Commits（feat/fix/docs/refactor/chore）+ 语义化版本；作者 `Marstudioo <marstudioo@users.noreply.github.com>`。
