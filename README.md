# Multi Material Slicer

一个面向多材料光固化打印机的 Qt/C++17 切片 App 原型。当前工作流：

1. 导入多个 STL。
2. 在右侧面板分别设置每个模型的位置、旋转、等比缩放和材料。
3. 在切片页用“每材料曝光参数表格”分别设置每种材料的底层/普通曝光时间与电流。
4. 按材料把 STL 切成 `material_0/1.png`、`material_1/1.png` 这样的 PNG 序列。
5. 生成兼容旧合并脚本的 `config.yaml`。
6. 调用 `slice_1080p.py --config ... --output ...`，输出合并后的投影 PNG 和 `run.gcode`。

整个导出流程（切片 → 写 `config.yaml` → 调用 Python）运行在后台线程，执行期间界面不卡死，日志实时刷新。

界面采用深色科技蓝主题（ChituBox / Lychee 风格），3D 视图带渐变背景与网格构建平台。详细技术实现见 [代码生成与修改说明.md](代码生成与修改说明.md)。

## Build

macOS 上如果使用本机已有的 Qt 5.12.12：

```bash
cmake -S . -B build-x86_64 \
  -DCMAKE_PREFIX_PATH=/Users/asher/Qt/5.12.12/clang_64 \
  -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build-x86_64 -j4
```

当前这套 Qt 是 `clang_64/x86_64`，Apple Silicon 默认会编成 `arm64`，所以需要显式设置 `CMAKE_OSX_ARCHITECTURES=x86_64`。

构建后可以直接打开：

```bash
open build-x86_64/MultiMaterialSlicer.app
```

也可以启动时直接导入 STL：

```bash
open build-x86_64/MultiMaterialSlicer.app --args /tmp/entity1-demo.stl
```

macOS 对 Documents、Desktop、iCloud Drive 等目录有隐私权限限制；如果通过命令行参数传入这些目录下的 STL，临时未签名 `.app` 可能会被系统拦截。正常使用时通过 App 内的“导入 STL”文件框选择模型，系统会授予文件访问权限。

Windows 上建议安装 Qt 5.12.12 MSVC 或 MinGW 套件，然后把 `CMAKE_PREFIX_PATH` 指到对应 Qt 目录，例如：

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\5.12.12\msvc2017_64
cmake --build build --config Release
```

## Python Merge Script

`slice_1080p.py` 已改成纯命令行工具，不再依赖旧的 PyQt UI：

```bash
python3 slice_1080p.py --config /path/to/config.yaml --output /path/to/merged
```

也支持位置参数：

```bash
python3 slice_1080p.py /path/to/config.yaml /path/to/merged
```

需要 Python 包：

```bash
pip install opencv-python numpy pyyaml
```

## Output Layout

App 默认输出：

```text
job_output/
  material_0/
    1.png
    2.png
  material_1/
    1.png
    2.png
  config.yaml
  merged/
    1_0.png
    1_1.png
    run.gcode
    config.yaml
```

## Notes

- 3D 视图使用自写 `QOpenGLWidget`，不依赖 Qt3D、VTK 或 CGAL，迁移成本低。
- STL 切片当前采用三角面与层平面求交、扫描线奇偶填充，适合封闭 STL 的基础切片。切片前会按每个三角面的 Z 范围把它分配到对应层（分桶），每层只测试可能相交的三角面，避免逐层遍历全部三角面。
- STL 读取同时支持 ASCII 与二进制；二进制 STL 允许结尾带额外 padding（部分导出软件会追加 metadata），不会因文件大小不严格等于 `84 + 50×三角形数` 而被误判成 ASCII。
- 每种材料的曝光参数（底层/普通曝光时间、电流）相互独立，分别写入 `config.yaml` 的 `bottom_exposure_time_i` / `standard_exposure_time_i` 等字段。
- 导出工作流在后台 `QThread` 中执行（见 `src/SliceWorker.*`），UI 不会冻结；线程在结束或退出时会被干净停止，避免崩溃。
- 界面为深色科技蓝主题，通过 Qt 样式表实现并经 `resources/resources.qrc` 编译进可执行文件（见 `resources/style.qss`、`src/main.cpp`）。
- App 带隐藏自检模式 `--selftest`，可在命令行下跑通完整导出链路并用退出码报告结果，便于回归测试。
- `config.yaml` 保留旧脚本字段，并补了 `output_width/output_height/pixel_size_mm` 便于 App 控制投影尺寸。

## 自检 / 回归测试

无需 GUI 点击即可验证完整导出链路：

```bash
build-x86_64/MultiMaterialSlicer.app/Contents/MacOS/MultiMaterialSlicer \
  --selftest demo_stl/multi_A_base_plate.stl demo_stl/multi_B_cross_insert.stl
```

它会自动配置 2 种材料（不同曝光）、触发与界面按钮相同的导出流程，输出到 `/tmp/selftest_out`，成功退出码为 0。

## 修订记录

### 第二轮：界面美化 + 真机实测修复

1. 深色科技蓝主题（QSS）：卡片化分组、统一控件四态、强调色 CTA、瘦滚动条等。
2. 3D 视图升级：渐变背景、10mm 网格构建平台、三色坐标轴、增强光照（含边缘光）。
3. 修复后台线程清理时序导致的崩溃（`QThread: Destroyed while thread is still running` / SIGABRT）。
4. 补充 macOS Bundle Identifier 等元数据。
5. 移除 Qt 5.12 不支持的 QSS `letter-spacing` 属性。
6. 新增 `--selftest` 命令行自检模式。
7. 真机端到端实测通过（退出码 0，每材料曝光贯穿到 GCode）。

### 第一轮：功能与正确性修复（对照需求逐条验证通过）

1. 导出流程从主线程同步执行改为后台线程，消除界面冻结。
2. 切片页改为“每材料曝光参数表格”，每种材料可独立设置底层/普通曝光时间与电流，并一路写入 `config.yaml` 与 `run.gcode`。
3. 底层与普通曝光的电流从共用一个控件改为可分别设置。
4. 二进制 STL 检测容忍结尾 padding，避免带 metadata 的二进制 STL 加载失败。
5. 切片按层对三角面分桶，提升大模型切片速度。
