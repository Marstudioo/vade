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

## 提交规范

Conventional Commits（feat/fix/docs/refactor/chore）+ 语义化版本；作者 `Marstudioo <marstudioo@users.noreply.github.com>`。
