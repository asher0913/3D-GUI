# 多材料光固化切片软件 — 技术报告

> 版本：0.1.0 ｜ 文档日期：2026-06-24
> 适用对象：代码审阅者、后续维护者
> 项目代号：MultiMaterialSlicer

---

## 1. 概述

本项目是一台**多材料光固化（DLP/LCD 投影式）3D 打印机**的切片上位机软件原型，使用 **C++17 + Qt 5.12 + CMake** 开发，目标平台为 **Windows**（同时可在 macOS 上开发与运行）。

软件把原先"ChituBox 分材料切片 → 手工解压 → Python 脚本合并"的割裂流程，整合成一个单一桌面应用：

1. 在 3D 视图中导入并摆放多个 STL 模型；
2. 为每个模型指定材料、设置平移/旋转/等比缩放；
3. 为每种材料单独配置曝光参数；
4. 一键按材料把模型切成 PNG 层图序列；
5. 生成 `config.yaml`；
6. 命令行调用 Python 合并脚本，输出最终投影图序列与机器运动控制文件 `run.gcode`。

设计原则：

- **3D 框架轻量化、可迁移**：自写 `QOpenGLWidget` 渲染，不依赖 Qt3D / VTK / CGAL 等重型库。
- **UI 可视化可编辑**：界面用 Qt Designer `.ui` 文件描述，便于后续直接编辑。
- **复用既有 Python 资产**：切片合并与 GCode 生成逻辑沿用并改造原 Python 脚本，C++ 端只负责 STL→PNG 切片、配置生成、流程编排。

---

## 2. 技术栈

| 维度 | 选型 |
| --- | --- |
| 语言标准 | C++17 |
| GUI 框架 | Qt 5.12（Widgets、OpenGL 模块） |
| 构建系统 | CMake ≥ 3.16，开启 `AUTOMOC` / `AUTOUIC` / `AUTORCC` |
| 3D 渲染 | `QOpenGLWidget` + `QOpenGLShaderProgram`（OpenGL 2.x / GLSL 120 级别着色器） |
| 界面主题 | Qt 样式表（QSS），通过 Qt 资源系统（`.qrc`）内嵌 |
| 合并脚本 | Python 3，依赖 `opencv-python`、`numpy`、`pyyaml` |
| 进程间调用 | `QProcess` 调用 Python，命令行传 `--config` / `--output` |
| 配置格式 | YAML（`config.yaml`） |

---

## 3. 目录结构

```
3D-GUI/
├── CMakeLists.txt              # 构建脚本
├── ui/
│   └── MainWindow.ui           # Qt Designer 主窗口布局
├── resources/
│   ├── style.qss               # 深色主题样式表
│   └── resources.qrc           # Qt 资源描述（内嵌 style.qss）
├── src/
│   ├── main.cpp                # 程序入口、主题加载
│   ├── MainWindow.{h,cpp}      # 主窗口：UI 逻辑与工作流编排
│   ├── OpenGLView.{h,cpp}      # 3D 视图：渲染与交互
│   ├── StlMesh.{h,cpp}         # STL 解析（ASCII / 二进制）
│   ├── ModelTypes.h            # 核心数据结构
│   ├── SliceExporter.{h,cpp}   # STL → 每材料 PNG 层图切片
│   ├── ConfigWriter.{h,cpp}    # 生成 config.yaml
│   └── SliceWorker.{h,cpp}     # 后台线程：编排切片+配置+Python
├── slice_1080p.py              # Python 合并脚本（多材料合并 + GCode）
├── scripts/
│   ├── package_macos.sh        # macOS 打包（macdeployqt + zip）
│   └── package_windows.ps1     # Windows 打包（windeployqt + zip）
├── demo_stl/                   # 演示用 STL
└── tools/generate_demo_stls.py # 生成演示 STL
```

---

## 4. 总体架构

```
┌──────────────────────────────────────────────────────────────┐
│                         主线程 (GUI)                            │
│                                                                │
│  ┌────────────┐   导入/变换/材料   ┌──────────────────────┐    │
│  │ OpenGLView │ ◀───────────────▶ │  MainWindow          │    │
│  │ (3D 渲染)  │   QVector<Model>  │  - UI 信号槽          │    │
│  └────────────┘                   │  - 材料曝光表格        │    │
│                                   │  - collectSettings()  │    │
│                                   │  - runWorkflow()      │    │
│                                   └───────────┬──────────┘    │
│                                               │ 创建/启动      │
└───────────────────────────────────────────────┼──────────────┘
                                                │ moveToThread
                          ┌─────────────────────▼──────────────┐
                          │        后台线程 (QThread)            │
                          │         SliceWorker::process()      │
                          │                                     │
                          │  1. SliceExporter::exportSlices()   │
                          │       STL → material_i/*.png        │
                          │  2. ConfigWriter::writeYaml()       │
                          │       → config.yaml                 │
                          │  3. QProcess → slice_1080p.py       │
                          │       → merged/*.png + run.gcode     │
                          └──────────────┬──────────────────────┘
                                         │ logMessage / finished 信号
                                         ▼
                                   主线程更新日志/弹窗
```

数据所有权：模型列表 `QVector<ModelInstance>` 由 `MainWindow` 持有，`OpenGLView` 仅持有只读指针。导出时 `SliceWorker` 拿到 **模型与设置的副本**，因此后台运行期间主线程仍可自由操作自身数据。

---

## 5. 核心数据结构（`src/ModelTypes.h`）

### 5.1 `Transform` — 模型变换

```cpp
struct Transform {
    QVector3D translation;       // 平移 (mm)
    QVector3D rotationDeg;       // 欧拉角 (度)，Z-Y-X 顺序
    float     scale = 1.0f;      // 等比缩放
    QMatrix4x4 matrix() const;   // 合成 4x4 变换矩阵
};
```

`matrix()` 的合成顺序为 **平移 × Rz × Ry × Rx × 缩放**，与 UI 上"先缩放再旋转再平移"的直觉一致。该矩阵同时用于 3D 渲染和切片（保证所见即所切）。

### 5.2 `ModelInstance` — 单个模型实例

包含 STL 路径、显示名、网格数据 `StlMesh`、`Transform`、材料编号 `materialIndex`、显示颜色、可见性。

### 5.3 `MaterialExposure` — 每材料曝光参数

```cpp
struct MaterialExposure {
    double bottomExposureTime    = 7.0;   // 底层曝光时间 (s)
    int    bottomExposureCurrent = 15;    // 底层曝光电流
    double standardExposureTime  = 2.5;   // 普通层曝光时间 (s)
    int    standardExposureCurrent = 15;  // 普通层曝光电流
};
```

### 5.4 `SliceSettings` — 切片全局参数

投影分辨率（`outputWidth/outputHeight`）、像素尺寸 `pixelSizeMm`、层高 `layerHeightMm`、材料数量、底层层数、**每材料曝光参数数组 `QVector<MaterialExposure> materials`**、Python 路径、合并脚本路径、输出目录。

> 设计要点：曝光参数从"单一标量"改为"每材料一组"，这是多材料打印的核心——不同树脂需要不同曝光，参数最终一路写入 `run.gcode` 的 `proj` 指令。

---

## 6. 模块实现详解

### 6.1 程序入口与主题（`src/main.cpp`）

启动序列：

1. `AA_EnableHighDpiScaling` + `AA_UseHighDpiPixmaps`：高分屏适配。
2. `QApplication::setStyle("Fusion")`：以 Fusion 作为跨平台一致、对 QSS 友好的基础风格。
3. `QApplication::setPalette(darkPalette())`：设置深色 `QPalette`，使 QSS 未完全覆盖的原生元素（系统菜单、文件对话框、tooltip）也保持深色，避免闪白。
4. 从 Qt 资源 `:/style.qss` 读取样式表并 `setStyleSheet()`。
5. 构造并显示 `MainWindow`。

### 6.2 主窗口（`src/MainWindow.{h,cpp}`）

`MainWindow` 是整个应用的协调中心，职责：

**(a) 初始化**
- `ui->setupUi(this)` 加载 `.ui` 布局；
- 把模型列表指针交给 `OpenGLView`；
- 设默认输出目录、Python 路径、脚本路径；
- 给按钮设系统标准图标；
- 建立全部信号-槽连接。

**(b) 模型管理**
- `importModels()` / `importModelFiles()`：通过 `QFileDialog::getOpenFileNames` 选 STL，逐个用 `StlMesh::load()` 解析，失败的记录到日志、不中断；成功的归一化到平台中心（`normalizeToBuildPlateOrigin()`），追加到 `m_models`。
- `removeSelectedModel()`：移除选中模型。
- `refreshModelList()`：刷新列表显示，条目形如 `名称 [M材料号]`。

**(c) 变换与材料同步**
- `selectedModelEdited()`：把右侧数值控件（XYZ 平移、XYZ 旋转、缩放、材料下拉）写回当前选中模型，并刷新 3D 视图。
- `loadSelectedModelToControls()`：反向，把选中模型的数值回填到控件。用 `m_loadingSelection` 标志位防止回填时触发"已编辑"槽造成递归。

**(d) 每材料曝光表格** —— `rebuildMaterialTable()`
- 表格 `materialTable`（`QTableWidget`）行数 = 材料数量，5 列：材料 / 底层时间 / 底层电流 / 普通时间 / 普通电流。
- 每个数值单元格放置 `QDoubleSpinBox` / `QSpinBox` 作为 cell widget（而非纯文本），保证输入校验与步进。
- **重建时先读出旧值再重建**，从而在材料数量变化时保留用户已填的参数。

**(e) 参数收集** —— `collectSettings()`
- 从各控件读取全局切片参数；
- 遍历 `materialTable` 的 cell widget，用 `qobject_cast` 取回每材料 `MaterialExposure`；
- 组装成 `SliceSettings`。

**(f) 工作流编排** —— `runWorkflow()`
1. 前置校验：至少 1 个模型、输出目录非空、Python 路径非空、合并脚本存在；
2. 进入"运行中"状态（`setRunningState(true)` 禁用按钮、显示等待光标）；
3. 创建 `QThread` + `SliceWorker`（持有 models/settings 副本），`moveToThread`；
4. 连接信号：`thread.started → worker.process`、`worker.logMessage → appendLog`、`worker.finished → workerFinished`、`worker.finished → thread.quit`、`worker.finished → worker.deleteLater`、`thread.finished → thread.deleteLater`；
5. 启动线程。

**(g) 完成回调** —— `workerFinished()`
- 恢复界面状态；
- 普通模式：弹成功/失败提示、写日志、更新状态栏；
- 自检模式：打印结果、`quit()+wait()` 线程后以退出码退出（见 §10）。

**(h) 析构清理**
- 若退出时仍有运行中的 worker 线程，先 `quit()+wait()` 再析构，避免"线程仍在运行就被销毁"导致的崩溃。

### 6.3 3D 视图（`src/OpenGLView.{h,cpp}`）

继承 `QOpenGLWidget` + `QOpenGLFunctions`，**不使用任何第三方 3D 库**。

**渲染管线（`paintGL`）**
1. 清屏；
2. `drawBackground()`：关闭深度测试，用独立着色器 `m_bgProgram` 画一个覆盖 NDC 全屏的垂直渐变四边形（顶深底浅偏蓝），形成工作室背景；
3. 重新开启深度测试，按当前 yaw/pitch/distance/pan 构造投影矩阵与视图矩阵；
4. `drawBuildPlate()`：画带 10mm 网格、外边框的构建平台 + 三色原点坐标轴（X 红 / Y 绿 / Z 蓝）；
5. 遍历模型，逐个 `drawMesh()`；选中模型颜色提亮。

**着色器**
- 主着色器（GLSL 120）：顶点着色器做 MVP 变换并传法线；片段着色器做 **主光 + 补光 + 环境光 + 边缘光（rim light）** 的简单光照模型，提升立体感与高光。
- 背景着色器：纯顶点色插值，画渐变。

**网格绘制（`drawMesh`）**
- 通用函数，支持 `GL_TRIANGLES`（实体）和 `GL_LINES`（网格/坐标轴）；
- 用 `setAttributeArray` 传顶点与法线，`glDrawArrays` 绘制——即时模式数组，无 VBO，足够当前数据量且代码简单、可移植。

**相机交互**
- 左键拖拽：旋转视角（yaw/pitch，pitch 限位 ±89°）；
- 右键拖拽：平移视角（pan）；
- 滚轮：缩放（distance，限位 20–2000）。

### 6.4 STL 解析（`src/StlMesh.{h,cpp}`）

**格式判定逻辑**（关键，避免误判）
1. 先看文本是否含 `facet` 且 `vertex` 关键字 → 判为 **ASCII**；
2. 否则，当 `文件大小 ≥ 84 + 50 × 三角形数` 时 → 判为 **二进制**（**容忍结尾 padding**，部分导出软件会追加 metadata，旧实现要求严格相等会误判失败）；
3. 二者皆不满足则回退尝试 ASCII。

**解析**
- 二进制：按 50 字节/三角形读取法线 + 3 顶点（小端 `qFromLittleEndian` + `memcpy` 还原 float）；
- ASCII：正则提取所有 `vertex x y z`，按每 3 个组成三角面；
- 法线缺失或退化时用叉积重算（`safeNormal`）。

**几何后处理**
- `updateBounds()` 算包围盒；
- `normalizeToBuildPlateOrigin()` 把模型 XY 中心移到原点、底面贴到 Z=0，便于摆放与切片。

### 6.5 STL 切片（`src/SliceExporter.{h,cpp}`）

把 3D 场景按材料光栅化成黑白 PNG 层图序列。核心算法：

**(1) 变换与收集**
- 每个模型的三角面经 `Transform::matrix()` 变换到世界坐标；
- 按材料分桶到 `trianglesByMaterial`；同时求全局最大 Z，得到总层数 `layerCount = ceil(maxZ / layerHeight)`。

**(2) 按层分桶优化（性能关键）**
- 为每个三角面计算 `[zMin, zMax]`，把它登记到所跨越的层区间 `[ceil(zMin/h), floor(zMax/h)]`；
- 这样每层只需测试**可能与该层平面相交**的三角面，避免逐层遍历全部三角面，把复杂度从 O(三角形数 × 层数) 降到接近 O(三角形数 + 交点数)，对大模型显著提速。

**(3) 逐层切片**
- 对每层 Z 平面、每个材料：
  - 对桶内每个三角面求与平面的交线段（`trianglePlaneSegment`，处理顶点恰在平面、边跨越平面等情形，取最远两点成段）；
  - 交点投影到像素平面（`toPixel`，按 `pixelSizeMm` 换算，居中）；
  - **扫描线奇偶填充**（`rasterizeSegments`）：对每条扫描线收集与各线段的交点 x，排序后两两配对填充 255，得到实心截面；
  - 输出 `Format_Grayscale8` 的 `{层号}.png` 到 `material_i/` 目录。

**(4) 目录清理**
- 导出前清掉旧的纯数字命名 PNG（`cleanMaterialDir`），避免残留层干扰后续 Python 的"最大层号"判定。

### 6.6 配置生成（`src/ConfigWriter.{h,cpp}`）

把 `SliceSettings` + 各材料切片目录写成 `config.yaml`，字段兼容 Python 脚本：

- 全局：`layer_height`、`pixel_size_mm`、`output_width/height`、`material_num`；
- 每材料路径：`img_path_0`、`img_path_1` …（绝对路径，统一正斜杠、加引号）；
- 每材料曝光：`bottom_exposure_time_i`、`bottom_exposure_current_i`、`standard_exposure_time_i`、`standard_exposure_current_i`；
- 机器默认参数：清洗/风干/运动/AMS/ASS/延时拍摄等一整套（`writeMachineDefaults`）；
- 材料间切换参数：`m{src}Tom{dst}` 系列。

### 6.7 后台工作线程（`src/SliceWorker.{h,cpp}`）

一个 `QObject`，`moveToThread` 到后台 `QThread`，`process()` 槽里顺序执行：

1. `SliceExporter::exportSlices()` → 每材料 PNG；
2. `ConfigWriter::writeYaml()` → `config.yaml`；
3. 创建 `merged/` 子目录；
4. `QProcess` 调用 `python slice_1080p.py --config <config> --output <merged>`，`waitForFinished(-1)` 阻塞等待（在后台线程阻塞，主线程不受影响）；
5. 收集 stdout/stderr，按退出码判定成败。

通过 `logMessage(QString)` 实时回传日志，`finished(success, summary, outputDir)` 通知主线程。持有数据副本，与主线程零共享、无锁。

### 6.8 Python 合并脚本（`slice_1080p.py`）

纯命令行工具（已去除原 PyQt UI 依赖），输入 `config.yaml` + 输出目录，做三件事：

1. **扫描**：统计每层每材料的有效像素数（`countNonZero`），并把各材料层图裁剪/居中适配到投影分辨率；
2. **换槽路径规划（动态规划）**：`_plan_tank_path` 以"层"为阶段、"当前所在料槽（材料）"为状态做 DP，最小化整次打印的换槽次数（换料即清洗+风干，代价高）。状态转移综合考虑该层有效材料数、上一层所在槽是否仍有效等，回溯得到每层应处于的料槽序列；
3. **生成**：按规划顺序输出合并后的投影图 `{层}_{槽}.png`，并写 `run.gcode`——含平台/玻璃/料槽运动、曝光指令 `proj <图> <时间> <电流>`、清洗风干、可选 AMS/ASS/alpha/beta/延时拍摄等。最后打印 `change_times` 与预估打印时长。

命令行同时支持位置参数 `config output` 与 `--config/--output`，并支持中文路径下的图像读写（`np.fromfile`/`imdecode`）。

---

## 7. 端到端数据流

```
STL 文件 ──load──▶ StlMesh(顶点/法线) ──Transform──▶ 世界坐标三角面
   │
   ├─(按材料分桶 + 逐层扫描线填充)──▶ material_0/*.png, material_1/*.png …
   │
SliceSettings ──ConfigWriter──▶ config.yaml
   │
   └──QProcess──▶ slice_1080p.py(--config, --output)
                       │
                       ├─扫描有效像素
                       ├─DP 规划换槽顺序
                       └──▶ merged/{层}_{槽}.png + run.gcode
```

---

## 8. 界面与主题实现

- **布局**：`ui/MainWindow.ui`，`QSplitter` 左 3D 视图 / 右控制面板；右侧 `QTabWidget` 分"模型"页（导入、列表、变换）与"切片"页（材料数量、分辨率、层高、每材料曝光表格、输出/Python/脚本路径、导出按钮、日志）。
- **主题**：`resources/style.qss` 深色科技蓝（强调色 `#2ca0dc`），经 `resources/resources.qrc` 由 `AUTORCC` 编译进可执行文件，运行时无外部依赖。覆盖按钮四态、输入框聚焦态、标签页下划线、列表/表格、滚动条、tooltip 等。
- **HCI**：卡片化分组与留白、主操作按钮做实心 CTA 并加手型光标与说明 tooltip、危险操作（移除）悬停红色、选中模型 3D 高亮。

> Qt 5.12 注意：QSS 是 CSS 的子集，**不支持 `letter-spacing`** 等属性，使用会打印警告，需避免。

---

## 9. 并发模型

- **唯一后台线程**：导出全流程（CPU 密集的切片 + 阻塞的 Python 进程）放在一个 `QThread`，主线程只负责 UI 与信号处理，导出期间界面不冻结。
- **零共享**：`SliceWorker` 持有数据副本，与主线程无共享状态，因此无需互斥锁。
- **生命周期安全**：worker/thread 通过信号驱动的 `deleteLater` 自动回收；`workerFinished` 与析构函数都对线程做 `quit()+wait()`，杜绝"线程运行中被销毁"的崩溃（该缺陷曾在真机实测中以 SIGABRT 暴露并已修复）。

---

## 10. 自检模式与测试验证

### 10.1 `--selftest` 命令行自检

为在**无需 GUI 鼠标操作**下验证完整链路，内置隐藏模式：

```bash
MultiMaterialSlicer --selftest a.stl b.stl
```

`MainWindow::runSelfTest()` 自动配置 2 材料（不同曝光）、指派模型、设输出目录，然后调用与界面按钮**完全相同**的 `runWorkflow()`；`workerFinished()` 在自检模式下以退出码报告结果（成功 0 / 失败 1）。可纳入命令行回归测试 / CI。

### 10.2 已验证结果（真机，macOS + Qt 5.12.12 x86_64）

| 验证项 | 结果 |
| --- | --- |
| 编译 | 通过（多次，含主题改动后） |
| 完整导出流程（真机进程） | 退出码 0，无崩溃 |
| 每材料切片层数 | material_0 / material_1 各 180 层 |
| `config.yaml` 每材料曝光 | 材料0 = 8/20/3/21，材料1 = 5.5/12/1.8/13（相互独立） |
| Python 合并产物 | merged 280 张 PNG + `run.gcode` |
| GCode 曝光指令 | `proj 1_0.png 8.00 20` / `proj 1_1.png 5.50 12` |
| ASCII / 二进制（含 padding）STL | 均正确加载 |
| 深色主题真机渲染 | 截图确认正常 |

> 限制说明：自动化工具无法点进 Rosetta 下的 x86_64 App 窗口（屏幕合成器遮挡检测识别不到该窗口），故 GUI 鼠标点击演示由 `--selftest` 命令行模式覆盖；切片页表格的视觉沿用同一套已生效 QSS，未单独逐像素截图。

---

## 11. 构建与打包

### 11.1 构建（macOS，本机 Qt 为 x86_64）

```bash
cmake -S . -B build-x86_64 \
  -DCMAKE_PREFIX_PATH=/Users/asher/Qt/5.12.12/clang_64 \
  -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build-x86_64 -j4
```

> Apple Silicon 默认编 arm64，而本机 Qt 是 x86_64，故须显式 `CMAKE_OSX_ARCHITECTURES=x86_64`（运行经 Rosetta）。

### 11.2 打包

- macOS：`./scripts/package_macos.sh` → `macdeployqt` 收集 Qt 框架 → `dist/MultiMaterialSlicer-mac-x86_64.zip`。
- Windows：`.\scripts\package_windows.ps1 -QtPrefix C:\Qt\5.12.12\msvc2017_64` → `windeployqt` 收集 DLL → `dist\MultiMaterialSlicer-win64.zip`。

`CMakeLists.txt` 构建后会把 `slice_1080p.py` 拷到可执行文件同级目录，App 默认就近查找；macOS 还设置了 Bundle Identifier `com.multimaterial.slicer` 等元数据。

### 11.3 运行期依赖

- Python 3 + `pip install opencv-python numpy pyyaml`；
- App 切片页可指定 Python 解释器路径；macOS 默认优先 `/opt/homebrew/opt/python@3.12/bin/python3.12`，Windows 优先 exe 同级 `python/python.exe`，否则回退到 `PATH` 中的 `python`。

---

## 12. 配置文件（`config.yaml`）关键字段

| 字段 | 含义 |
| --- | --- |
| `layer_height` | 层高 (mm) |
| `output_width` / `output_height` | 投影分辨率 (px) |
| `pixel_size_mm` | 像素物理尺寸 (mm/px) |
| `material_num` | 材料数量 |
| `img_path_i` | 第 i 材料的切片 PNG 目录 |
| `bottom_layers` | 底层层数 |
| `bottom_exposure_time_i` / `_current_i` | 第 i 材料底层曝光时间 / 电流 |
| `standard_exposure_time_i` / `_current_i` | 第 i 材料普通层曝光时间 / 电流 |
| `m{a}Tom{b}` 系列 | a→b 材料切换的开关/电流/时间 |
| 清洗/风干/运动/AMS/ASS/延时 | 机器运动与流程参数 |

---

## 13. 已知限制与后续工作

**当前限制**
- 切片适用于封闭网格；无模型修复、法线修复、非流形/破面检测。
- 无支撑生成、无岛/孤岛检测、无边缘抗锯齿/灰度。
- 输出目录不跨重启记忆（每次启动重置为默认目录）。
- 切片采用即时模式数组渲染，超大模型（百万面级）渲染与切片仍有优化空间。
- Windows 端未做真机回归（开发机为 macOS）。

**建议后续**
- 短期：输出目录持久化（`QSettings`）/ 时间戳子目录避免覆盖；导出前模型越界检查；机器参数（清洗/风干/AMS 等）做成可编辑表单；工程保存/打开。
- 中期：STL 修复与更稳健的轮廓提取；抗锯齿/灰度边缘；支撑生成；更准的打印时长估算。
- 长期：将 Python 合并逻辑迁移到 C++ 以去除运行时依赖；抽象打印机 Profile（分辨率/槽位/材料库）；Windows 安装包与代码签名。

---

## 14. 相关文档

- `README.md` — 快速上手与构建。
- `代码生成与修改说明.md` — 逐文件改造与修复说明（含两轮修订记录）。
- `打包与会议演示指南.md` — 打包步骤与演示脚本。
