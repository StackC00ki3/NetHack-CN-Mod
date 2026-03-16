# NetHackW 汉化注入 DLL（Drop-in）

这个目录提供一个可直接放到 NetHackW 同目录使用的 DLL 方案。

## 原理

- 目标文件名必须是 `winmm.dll`。
- `NetHackW.exe` 依赖 `winmm`，启动时会优先从程序目录加载同名 DLL。
- 本 DLL 会：
1. 代理并转发 `PlaySoundA/W` 与 `sndPlaySoundA/W` 到系统 `winmm.dll`。
2. 在进程内通过 IAT Hook 拦截 `SetWindowTextA` / `DrawTextA` / `TextOutA`，对内置词典中的英文文本替换为中文。

## 构建（CMake，推荐）

在 PowerShell 或 CMD 中执行：

```bat
cd /d D:\Download\nethack-367-src\nethack-367-src\NetHack-3.6.7\tools\nethack_i18n_mod
cmake -S . -B build -A x64
cmake --build build --config Release
```

若你使用 MinGW + Ninja：

```bat
cd /d D:\Download\nethack-367-src\nethack-367-src\NetHack-3.6.7\tools\nethack_i18n_mod
cmake -S . -B build-mingw -G Ninja
cmake --build build-mingw
```

编译产物位置：

- `build\Release\winmm.dll`（VS 多配置生成器）
- `build\winmm.dll`（单配置生成器，如 Ninja）

说明：

- MinGW/Ninja 与 MSVC 都可用。
- `/DEF:` 仅适用于 MSVC，当前工程已自动按编译器处理。

## 目录结构

- `src/`：核心 C 源码（代理逻辑、Hook、JSON 解析）
- `include/`：头文件（资源 ID、公共声明）
- `resources/`：RC 与 DEF 资源/导出定义
- `locales/`：翻译词典 JSON
- `scripts/`：辅助脚本
- `docs/`：文档
- `output/`：字符串提取输出样本

## 使用

1. 将编译产物 `winmm.dll` 复制到 `NetHackW.exe` 所在目录。
2. 启动游戏，ENJOY!


## 自定义词典（推荐 Transifex 工作流）

词典已改为从 DLL 内嵌 JSON 资源读取：

- 源语言模板：`en.json`
- 中文目标文件：`zh-CN.json`
- 资源脚本：`winmm_zh_proxy.rc`
- 解析器实现：`cJSON.c` / `cJSON.h`

对应位置：

- `locales/en.json`
- `locales/zh-CN.json`
- `resources/winmm_zh_proxy.rc`
- `src/cJSON.c` / `include/cJSON.h`

格式要求（Key-Value JSON）：

```json
{
	"Messages": "消息",
	"Save?": "要保存吗？"
}
```

注意事项：

- key 必须是英文原文（运行时 exact match）。
- value 为翻译文本。
- 仅支持“扁平对象”，不支持嵌套。
- 以 `__` 开头的键会被忽略，可用于元数据（如 `__meta`）。

Transifex 建议：

1. 上传 `en.json` 作为 source（en）。
2. 在 Transifex 中完成 zh-CN 翻译。
3. 下载后覆盖 `zh-CN.json`。
4. 重新编译，翻译会随 DLL 资源一起更新。

兼容性说明：

- 若资源缺失或 JSON 解析失败，DLL 会自动回退到 `winmm_proxy_main.c` 内置词典。
