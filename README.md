# MultiMaterialSlicer

MultiMaterialSlicer 是一个面向多材料光固化打印机的 Qt/C++ 桌面 APP。当前版本把原先的“Chitubox 切片导出多个文件夹，再用 Python 合并”的流程整合到一个应用里：在 APP 中导入多个 STL 或 STEP 装配体、设置模型变换和材料、按材料导出 PNG 切片、生成 `config.yaml`，再调用后端命令行工具生成最终 `run.gcode` 和合并后的图像文件。

当前代码重点面向 Windows 可用、macOS 可演示、Linux 后续可迁移的路线。界面使用 Qt 5.12.12 和 `.ui` 文件，3D 视图使用轻量的 `QOpenGLWidget`。

更新时间：2026-06-30

## 当前能力

- 左侧 3D 视图可以显示多个 STL 模型，也可以导入 STEP/STP 装配体。
- 3D 视图使用 OpenGL VBO 缓存网格数据，模型加载后不会在每帧重复上传全部顶点。
- 底板网格/边框/坐标轴几何会缓存，只有构建平台尺寸变化时才重建。
- STEP 导入会保留装配层级，在模型树中显示为“根装配体 -> 子装配体 -> 子实体叶子节点”的多级树。
- 选中任意 STEP 装配体节点时，平移、旋转、缩放会作为整体应用到该节点下所有子实体；父子装配体的变换会按层级矩阵叠加。
- 选中 STEP 子实体时，可以单独设置材料，但不能单独破坏装配体相对位置。
- 选中模型后可以在右侧输入平移、旋转、等比例缩放数值。
- 选中模型后可以复制，复制件保留网格、变换、材料和颜色。
- 每个模型可以分配材料槽，默认材料数量为 3，用户可以调整材料数量。
- 材料参数 UI 使用“光强”表述，不再显示“电流”。配置和 GCode 需要的电流值由机器/材料库中的光强到电流映射计算。
- 机器与材料参数从 `config/machine_material_presets.yaml` 读取。读不到配置库时，APP 会要求用户选择配置文件；仍然无法读取时会禁用主要操作。
- 切片页顶部包含打印设置，模型页只保留模型导入、选择、复制、删除和变换。
- 切片常规选项包含“挡板 (groove)”和“刮板1/2/3 (slide_0/1/2)”。
- 切片高级选项包含 `max_height`、`z_acc_h`、`clean_tank`、`dry_tank`、`drop_time_bottom`、`ASS_times` 等后端机器/GCode 参数。
- 材料转换默认参数可以在高级选项里通过 `material_transition_enabled`、`material_transition_current`、`material_transition_time` 配置；从 `config.yaml` 导入的 `m0Tom1_current` 等单向转换参数也会保留并写出。
- 可以从手动编辑的 `config.yaml` 导入参数，回填打印设置、曝光设置、常规选项和高级选项。
- 后端以命令行工具形式运行：macOS 为 `slice_merge_tool`，Windows 为 `slice_merge_tool.exe`。开发模式仍保留 `slice_1080p.py --config ... --output ...`。
- 切片和后端执行过程有进度反馈、取消按钮和后端超时保护，后端工具挂起时不会无限等待。
- STL 读取和切片导出会拒绝 NaN/Inf 坐标，并检查二进制 STL 声明三角数，避免异常文件导致崩溃或未定义行为。
- 预设库数值解析会校验非法数字、0 宽高、0 像素尺寸和映射长度，不会静默把非法值变成 0。
- STL 读取会优先识别结构合理的二进制 STL，并给预览加载加 512MB 文件上限，避免二进制头注释误判 ASCII 或大文件 OOM。
- APP 主窗口已经改为可调节大小。
- 打包脚本会把 Qt 运行库、预设库和后端工具一起放进发布包。

## 目录结构

```text
.
├── CMakeLists.txt
├── config/
│   └── machine_material_presets.yaml
├── demo_stl/
│   ├── 01_basic_block.stl
│   ├── 02_cylinder_20mm.stl
│   ├── 03_star_badge.stl
│   ├── 04_three_towers.stl
│   ├── multi_A_base_plate.stl
│   └── multi_B_cross_insert.stl
├── demo_step/
│   └── step示例.step
├── resources/
├── scripts/
│   ├── package_backend_macos.sh
│   ├── package_backend_windows.ps1
│   ├── package_step_macos.sh
│   ├── package_step_windows.ps1
│   ├── package_macos.sh
│   └── package_windows.ps1
├── slice_1080p.py
├── src/
├── tools/
│   ├── generate_demo_stls.py
│   └── step_to_stl_parts.py
└── ui/
    └── MainWindow.ui
```

## 推荐阅读顺序

1. `README.md`：项目入口、功能范围、构建、打包、输出结构和验证结果。
2. `打包与会议演示指南.md`：会议现场如何打开、怎么演示、用哪些 STL/STEP。
3. `代码生成与修改说明.md`：详细流水账，说明每个模块做了哪些修改。
4. `TechnicalReport.md`：技术架构、数据流、模块职责、风险和后续工作。
5. `下一步修改需求提示词.md`：可以直接交给其他 AI 或开发者的后续开发提示词。

## macOS 构建和打包

本机当前验证使用的是 Qt 5.12.12 clang_64，路径为：

```bash
/Users/asher/Qt/5.12.12/clang_64
```

生成可演示的自包含 macOS 包：

```bash
bash scripts/package_macos.sh
```

输出文件：

```text
dist/MultiMaterialSlicer-mac-x86_64.zip
```

注意：仓库放在 iCloud Drive 下时，直接运行 `./scripts/package_macos.sh` 有时会遇到脚本执行或扩展属性问题，因此推荐使用 `bash scripts/package_macos.sh`。脚本内部会清理 iCloud 文件属性，并进行 ad-hoc 签名。

当前 macOS GUI 包默认是 x86_64。Apple Silicon 机器可以通过 Rosetta 运行。STEP 转换器 `step_to_stl_parts` 会在本机用 CadQuery/OCP 打包，当前这台 Apple Silicon 机器生成的是 arm64 helper，x86_64 GUI 可以正常启动该 helper。若要给 Intel Mac 分发 STEP 导入能力，应在 Intel Mac 或 x86_64 Python 环境中重新运行 `scripts/package_step_macos.sh`。

## Windows 打包

在 Windows 上安装 Qt 5.12.12、CMake、Visual Studio C++ 工具链和 Python 3.10/3.11/3.12 后运行。STEP 转换器依赖 CadQuery/OCP，建议安装 Python 3.12 并勾选 “Add Python to PATH”：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -QtPrefix C:\Qt\5.12.12\msvc2017_64
```

脚本会默认先构建 `backend_dist\slice_merge_tool.exe` 和 `backend_dist\step_to_stl_parts.exe`，然后编译 Qt APP，调用 `windeployqt.exe`，检查必需文件和 helper 是否能启动，最后生成：

```text
dist\MultiMaterialSlicer-win64.zip
```

会议前在 Windows 展示机上建议加 `-RunSelfTest`，让打包脚本直接运行包内 STEP 示例自检：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -QtPrefix C:\Qt\5.12.12\msvc2017_64 -RunSelfTest
```

生成的 Windows 发布目录会包含：

```text
dist\MultiMaterialSlicer-win64\
├── MultiMaterialSlicer.exe
├── slice_merge_tool.exe
├── step_to_stl_parts.exe
├── machine_material_presets.yaml
├── platforms\qwindows.dll
└── examples\
    ├── demo_stl\
    └── demo_step\
```

如果只想单独打包后端：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_backend_windows.ps1
```

如果只想单独打包 STEP 转换器：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_step_windows.ps1
```

Windows 包脚本已经准备好，但最终 Windows zip 应在 Windows 实机上生成和验证。

## 后端工具

正式工作流里，APP 调用命令行后端：

```bash
slice_merge_tool --config <config.yaml> --output <merged_output_dir>
```

开发调试时，也可以直接运行 Python 源码：

```bash
python3 slice_1080p.py --config <config.yaml> --output <merged_output_dir>
```

后端会读取 APP 生成的 `config.yaml`，读取每个材料文件夹中的 PNG，按照材料切换、曝光、挡板、刮板和高级机器参数生成最终输出。

STEP 导入使用另一个可替换命令行工具：

```bash
step_to_stl_parts --input <model.step> --output <parts_dir>
```

它会输出 `manifest.json` 和每个子实体对应的 STL。默认实现是 `tools/step_to_stl_parts.py`，使用 CadQuery wheel 中的 OCP/OpenCascade 内核；发布包优先调用打包后的 `step_to_stl_parts` / `step_to_stl_parts.exe`。

## 输出结构

一次完整导出会生成类似结构：

```text
output/
├── config.yaml
├── material_0/
│   ├── 1.png
│   └── 2.png
├── material_1/
│   ├── 1.png
│   └── 2.png
├── material_2/
│   ├── 1.png
│   └── 2.png
└── merged/
    ├── config.yaml
    ├── 1_0.png
    └── run.gcode
```

其中 `material_N` 是 APP 按材料导出的切片文件夹，`merged` 是后端命令行工具合并后的最终输出。

## 实测验证

已完成的本机验证：

- `python3 -m py_compile slice_1080p.py` 通过。
- `bash scripts/package_macos.sh` 可以生成自包含 zip 包。
- 解压后的 `MultiMaterialSlicer.app` 通过 `codesign --verify --deep --strict`。
- 后端 `slice_merge_tool --help` 可以正常运行。
- APP 内置 `--selftest` 可以导入 STL/STEP、设置材料、导出切片、生成 `config.yaml` 和 `run.gcode`。
- `tools/step_to_stl_parts.py` 已用 `demo_step/step示例.step` 实测，可拆出 2 个子实体。
- 打包后的 `backend_dist/step_to_stl_parts` 已实测可直接转换 `demo_step/step示例.step`。
- APP 内置 `--selftest demo_step/step示例.step` 已实测通过：导入 STEP、拆 2 个子实体、切片、生成 `config.yaml` 和 `run.gcode`。
- 使用 Computer Use 实际打开最终打包 APP，完成了模型导入、材料修改、数值变换、复制模型、切换切片页、勾选挡板和刮板、点击生成、确认成功弹窗、检查输出文件的流程。此后又新增了切片页打印设置、高级参数表和 `config.yaml` 导入入口。
- 使用 Computer Use 实际打开当前构建 APP，确认 STEP 树显示 `step示例.step -> 实体1/实体2`，第二个子实体材料可改为 2，父节点 X 位移可整体移动装配体。
- 2026-06-29 重新修复并验证了评审指出的稳定性/性能问题：NaN STL 防护、二进制 STL 三角数溢出防护、模型树材料控件稳定 ID、后端 10 分钟超时、取消按钮、切片进度、OpenGL VBO、视图空间法线、光强映射排序、模型复制共享网格。
- 2026-06-29 重新运行 `cmake --build build-x86_64`、`xmllint --noout ui/MainWindow.ui`、`python3 -m py_compile slice_1080p.py tools/step_to_stl_parts.py`、坏 NaN STL 自测、STEP 装配体自测，均通过。
- 2026-06-29 重新打包 `dist/MultiMaterialSlicer-mac-x86_64.zip`，从最终 zip 解包后运行 `--selftest demo_step/step示例.step` 通过，并通过 `codesign --verify --deep --strict`。
- 2026-06-29 使用 Computer Use 操作最终解包 APP：通过文件选择器导入 `demo_stl/multi_A_base_plate.stl`，修改材料 1 -> 2 -> 3，修改 X 位移，复制并删除模型，切到切片页，从 GUI 点击“导出切片并生成 GCode”，成功生成 `/Users/asher/MultiMaterialSlicerOutput/config.yaml`、材料 PNG、`merged/run.gcode` 和 102 个 merged 文件。
- 2026-06-30 对 Medium 清单中可直接落地的项做了修复：材料转换参数可配置、二进制/ASCII STL 判别更稳、预设库非法数值报错、所有模型不可见时阻止运行、Python 查找使用 PATH 和多版本 fallback、底板几何缓存、切片 QImage 复用、STL 大文件上限、配置路径支持 `--config`/编译期路径、装配体/模型 ID 使用 64 位计数。
- 2026-06-30 使用 Computer Use 实际操作最终打包 APP，导入嵌套 STEP 测试件，确认 GUI 树显示 `RootAssembly -> InnerAssembly -> InnerBase/InnerPeg`，`InnerPeg` 可单独改为材料2，`InnerAssembly` 位移只带动其子实体，`RootAssembly` 位移会与子装配体位移正确叠加，复制和删除装配体可用。

早期自动化测试中也使用过启动参数导入 STL/STEP 来覆盖同一套导入代码。人工演示时，“导入 STL”和“导入 STEP”按钮会打开系统文件选择器，可以正常选择 `demo_stl` 中的 STL 文件和 `demo_step` 中的 STEP 文件。

## 快速演示模型

仓库自带演示模型：

```text
demo_stl/01_basic_block.stl
demo_stl/02_cylinder_20mm.stl
demo_stl/03_star_badge.stl
demo_stl/04_three_towers.stl
demo_stl/multi_A_base_plate.stl
demo_stl/multi_B_cross_insert.stl
```

会议演示建议使用 `multi_A_base_plate.stl` 和 `multi_B_cross_insert.stl`，一个设为材料1，一个设为材料2，再复制其中一个模型并改为材料3，能比较直观展示多材料工作流。

STEP 装配体演示文件：

```text
demo_step/step示例.step
```

导入后模型树会显示一个 STEP 父节点和两个子实体。建议把 `实体2` 的材料列从 1 改成 2，再选中父节点调整 X/Y/旋转，展示“父级整体移动、子实体独立材料”的流程。

多级 STEP 装配体验证使用过包含“根装配体 -> 子装配体 -> 子实体”的测试件；导入后可检查子实体独立材料、子装配体整体移动和父子装配体变换叠加。
