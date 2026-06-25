# MultiMaterialSlicer

MultiMaterialSlicer 是一个面向多材料光固化打印机的 Qt/C++ 桌面 APP。当前版本把原先的“Chitubox 切片导出多个文件夹，再用 Python 合并”的流程整合到一个应用里：在 APP 中导入多个 STL、设置模型变换和材料、按材料导出 PNG 切片、生成 `config.yaml`，再调用后端命令行工具生成最终 `run.gcode` 和合并后的图像文件。

当前代码重点面向 Windows 可用、macOS 可演示、Linux 后续可迁移的路线。界面使用 Qt 5.12.12 和 `.ui` 文件，3D 视图使用轻量的 `QOpenGLWidget`。

## 当前能力

- 左侧 3D 视图可以显示多个 STL 模型。
- 选中模型后可以在右侧输入平移、旋转、等比例缩放数值。
- 选中模型后可以复制，复制件保留网格、变换、材料和颜色。
- 每个模型可以分配材料槽，默认材料数量为 3，用户可以调整材料数量。
- 材料参数 UI 使用“光强”表述，不再显示“电流”。配置和 GCode 需要的电流值由机器/材料库中的光强到电流映射计算。
- 机器与材料参数从 `config/machine_material_presets.yaml` 读取。读不到配置库时，APP 会要求用户选择配置文件；仍然无法读取时会禁用主要操作。
- 切片高级选项包含“挡板 (groove)”和“刮板1/2/3 (slide_0/1/2)”。
- 后端以命令行工具形式运行：macOS 为 `slice_merge_tool`，Windows 为 `slice_merge_tool.exe`。开发模式仍保留 `slice_1080p.py --config ... --output ...`。
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
├── resources/
├── scripts/
│   ├── package_backend_macos.sh
│   ├── package_backend_windows.ps1
│   ├── package_macos.sh
│   └── package_windows.ps1
├── slice_1080p.py
├── src/
├── tools/
│   └── generate_demo_stls.py
└── ui/
    └── MainWindow.ui
```

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

当前 macOS 包默认是 x86_64。Apple Silicon 机器可以通过 Rosetta 运行。如果后续要生成 arm64 或 universal 包，可以把 `scripts/package_backend_macos.sh` 和 `scripts/package_macos.sh` 扩展成多架构产物。

## Windows 打包

在 Windows 上安装 Qt 5.12.12、CMake、Visual Studio C++ 工具链和 Python 后运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -QtPrefix C:\Qt\5.12.12\msvc2017_64
```

脚本会默认先构建 `backend_dist\slice_merge_tool.exe`，然后编译 Qt APP，调用 `windeployqt.exe`，最后生成：

```text
dist\MultiMaterialSlicer-win64.zip
```

如果只想单独打包后端：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_backend_windows.ps1
```

## 后端工具

正式工作流里，APP 调用命令行后端：

```bash
slice_merge_tool --config <config.yaml> --output <merged_output_dir>
```

开发调试时，也可以直接运行 Python 源码：

```bash
python3 slice_1080p.py --config <config.yaml> --output <merged_output_dir>
```

后端会读取 APP 生成的 `config.yaml`，读取每个材料文件夹中的 PNG，按照材料切换、曝光、挡板和刮板参数生成最终输出。

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
- APP 内置 `--selftest` 可以导入 STL、设置材料、导出切片、生成 `config.yaml` 和 `run.gcode`。
- 使用 Computer Use 实际打开最终打包 APP，完成了模型导入、材料修改、数值变换、复制模型、切换切片页、勾选挡板和刮板、点击生成、确认成功弹窗、检查输出文件的流程。

Computer Use 对 macOS 原生文件选择器控制不稳定，所以自动化测试中使用启动参数导入 STL 来覆盖同一套导入代码。人工演示时，“导入 STL”按钮会打开系统文件选择器，可以正常选择 `demo_stl` 中的 STL 文件。

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
