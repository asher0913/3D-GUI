# MultiMaterialSlicer 当前状态索引

更新时间：2026-06-29

这个文件原来是旧版 README 副本，内容已经更新为当前版本索引。主入口请看：

```text
README.md
```

## 当前结论

MultiMaterialSlicer 现在是一个 Qt/C++17/CMake 桌面 APP，用于多材料光固化打印机演示工作流。它已经可以：

- 导入多个 STL。
- 导入 STEP/STP 装配体，并在模型树中显示父节点和子实体。
- 在 OpenGL 3D 视图中显示模型。
- 对每个模型设置平移、旋转、等比例缩放。
- 对每个模型设置材料。
- 对 STEP 子实体单独设置材料，选中 STEP 父节点时整体平移/旋转/缩放。
- 默认使用 3 个材料，并允许调整材料数量。
- 复制选中的模型。
- 使用“光强”作为 UI 字段。
- 打印设置位于切片页。
- 挡板和刮板位于“常规选项”。
- 截图中的机器/GCode 参数位于“高级选项”表格。
- 可以从手动编辑的 `config.yaml` 导入参数。
- 从 YAML 机器/材料库读取参数。
- 导出每个材料对应的 PNG 文件夹。
- 生成 `config.yaml`。
- 调用后端命令行工具 `slice_merge_tool` 或 `slice_merge_tool.exe`。
- 调用 STEP 转换工具 `step_to_stl_parts` 或 `step_to_stl_parts.exe`。
- 生成合并后的 PNG 和 `run.gcode`。
- 在 macOS 上打包成自包含 zip。
- 对异常 STL 做 NaN/Inf 和二进制三角数溢出防护。
- 使用 OpenGL VBO 缓存模型网格，减少每帧重复上传。
- 使用稳定模型 ID 更新材料微调框，删除/复制模型后不会写错对象。
- 切片和后端流程支持进度、取消按钮和后端超时保护。

## 推荐阅读顺序

1. `README.md`：项目入口、构建、打包、输出结构、验证结果。
2. `打包与会议演示指南.md`：会议现场如何打开、怎么演示、用哪些 STL/STEP。
3. `代码生成与修改说明.md`：详细流水账，说明每个模块做了哪些修改。
4. `TechnicalReport.md`：技术架构、数据流、模块职责、风险和后续工作。
5. `下一步修改需求提示词.md`：可以直接交给其他 AI 或开发者的后续开发提示词。

## 当前打包产物

macOS 当前包：

```text
dist/MultiMaterialSlicer-mac-x86_64.zip
```

会议演示建议使用这个包，并搭配：

```text
demo_stl/multi_A_base_plate.stl
demo_stl/multi_B_cross_insert.stl
demo_stl/03_star_badge.stl
demo_step/step示例.step
```

## 重新打包

macOS：

```bash
bash scripts/package_macos.sh
```

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -QtPrefix C:\Qt\5.12.12\msvc2017_64 -RunSelfTest
```

Windows 推荐安装 Python 3.10/3.11/3.12；STEP 转换器依赖 CadQuery/OCP。打包脚本会生成并检查 `slice_merge_tool.exe`、`step_to_stl_parts.exe`、`qwindows.dll`、配置文件和 examples 目录。

## 已验证事项

- macOS APP 可以打包。
- 打包 APP 可以打开。
- 后端工具在包内。
- STEP 转换工具在包内。
- APP 可以实际运行 GUI 流程。
- `demo_step/step示例.step` 可以被拆成 `实体1` 和 `实体2`。
- STEP 子实体材料可以从 1 改成 2。
- STEP 父节点 X 位移可以整体移动两个子实体。
- 可以生成 `config.yaml`、材料 PNG、合并 PNG 和 `run.gcode`。
- `groove` 在常规选项中显示为“挡板”。
- `slide_0/1/2` 在常规选项中显示为“刮板1/2/3”。
- 高级选项表可以编辑 `max_height`、`z_acc_h`、`clean_tank`、`dry_tank`、`drop_time_bottom`、`ASS_times` 等参数。
- “导入 config.yaml”可以回填参数。
- UI 中使用“光强”。
- 窗口可以调整大小。
- 2026-06-29 修复评审指出的 NaN STL、二进制 STL 溢出、模型索引失效、后端无限等待、OpenGL 非 VBO、光强映射未排序、网格深拷贝、边缘光法线空间、零进度反馈和不可取消等问题。
- 2026-06-29 重新打包 `dist/MultiMaterialSlicer-mac-x86_64.zip`，从最终 zip 解包后通过 STEP 自测和签名验证。
- 2026-06-29 使用 Computer Use 实际操作最终解包 APP：文件选择器导入 STL、改材料、改 X 位移、复制、删除、切到切片页、点击生成，最终成功输出 `config.yaml`、材料 PNG、`merged/run.gcode` 和 merged PNG。

Windows 包脚本已经准备好，但最终 Windows zip 需要在 Windows 实机上生成和验证。
