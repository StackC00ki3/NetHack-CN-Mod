# NetHackW 汉化模组

一个把 winmm.dll 放到 NetHackW.exe 同目录即可使用的 nethack 汉化模组。

## 使用

1. 将编译产物 `winmm.dll` 复制到 `NetHackW.exe` 所在目录。
2. 启动游戏，ENJOY!

## 原理

- 目标文件名必须是 `winmm.dll`。
- `NetHackW.exe` 依赖 `winmm`，启动时会优先从程序目录加载同名 DLL。
- 本 DLL 会：
1. 在进程内通过 IAT Hook 拦截 `SetWindowTextA` / `DrawTextA` / `TextOutA`，对内置词典中的英文文本替换为中文。
2. hook vpline 对格式化字符串翻译

## 构建（CMake，推荐）

在 PowerShell 或 CMD 中执行：

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

构建 x86（Win32）版本：

```bat
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

说明：把对应架构的 `winmm.dll` 放到对应架构的 `NetHackW.exe` 同目录即可。

若你使用 MinGW + Ninja：

```bat
cmake -S . -B build-mingw -G Ninja
cmake --build build-mingw
```

## 目录结构

- `src/`：核心 C 源码
- `include/`：头文件
- `resources/`：RC 与 DEF 资源/导出定义
- `locales/`：翻译词典 JSON
- `scripts/`：辅助脚本
- `docs/`：文档
- `output/`：字符串提取输出样本
