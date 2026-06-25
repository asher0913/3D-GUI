# MultiMaterialSlicer 当前状态索引

更新时间：2026-06-25

这个文件原来是旧版 README 副本，内容已经更新为当前版本索引。主入口请看：

```text
README.md
```

## 当前结论

MultiMaterialSlicer 现在是一个 Qt/C++17/CMake 桌面 APP，用于多材料光固化打印机演示工作流。它已经可以：

- 导入多个 STL。
- 在 OpenGL 3D 视图中显示模型。
- 对每个模型设置平移、旋转、等比例缩放。
- 对每个模型设置材料。
- 默认使用 3 个材料，并允许调整材料数量。
- 复制选中的模型。
- 使用“光强”作为 UI 字段。
- 从 YAML 机器/材料库读取参数。
- 导出每个材料对应的 PNG 文件夹。
- 生成 `config.yaml`。
- 调用后端命令行工具 `slice_merge_tool` 或 `slice_merge_tool.exe`。
- 生成合并后的 PNG 和 `run.gcode`。
- 在 macOS 上打包成自包含 zip。

## 推荐阅读顺序

1. `README.md`：项目入口、构建、打包、输出结构、验证结果。
2. `打包与会议演示指南.md`：会议现场如何打开、怎么演示、用哪些 STL。
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
```

## 重新打包

macOS：

```bash
bash scripts/package_macos.sh
```

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -QtPrefix C:\Qt\5.12.12\msvc2017_64
```

## 已验证事项

- macOS APP 可以打包。
- 打包 APP 可以打开。
- 后端工具在包内。
- APP 可以实际运行 GUI 流程。
- 可以生成 `config.yaml`、材料 PNG、合并 PNG 和 `run.gcode`。
- `groove` 显示为“挡板”。
- `slide_0/1/2` 显示为“刮板1/2/3”。
- UI 中使用“光强”。
- 窗口可以调整大小。

Windows 包脚本已经准备好，但最终 Windows zip 需要在 Windows 实机上生成和验证。
