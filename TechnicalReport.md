# MultiMaterialSlicer Technical Report

Updated: 2026-06-25

## 1. Summary

MultiMaterialSlicer is a Qt/C++ desktop application for a multi-material resin printer slicing workflow. The current version integrates the previous external process of exporting per-material folders and then running a Python merge script. The application now imports STL models, displays them in a lightweight 3D view, lets the user edit transforms and material assignments, exports per-material PNG slices, writes `config.yaml`, and calls a packaged backend command-line tool to generate merged images and `run.gcode`.

The application uses:

- C++17
- CMake
- Qt 5.12.12
- Qt Widgets
- Qt Designer `.ui`
- `QOpenGLWidget` for the 3D viewport
- Python source for the merge backend, packaged with PyInstaller for release

## 2. Main Workflow

The implemented workflow is:

1. Load the machine/material preset YAML library.
2. Import one or more STL files.
3. Display imported models in the OpenGL viewport.
4. Select a model and edit translation, rotation, uniform scale, and material.
5. Optionally duplicate selected models.
6. Configure material count and per-material exposure/strength settings.
7. Export per-material PNG folders.
8. Write `config.yaml`.
9. Run backend command-line tool:

```bash
slice_merge_tool --config <config.yaml> --output <merged_dir>
```

10. Produce merged PNG output and `run.gcode`.

## 3. Architecture

| Area | Implementation | Notes |
|---|---|---|
| UI | `ui/MainWindow.ui`, `src/MainWindow.*` | Qt Designer-editable UI with resizable main window |
| 3D View | `src/OpenGLView.*` | Lightweight OpenGL viewport based on `QOpenGLWidget` |
| STL Loading | `src/StlLoader.*` | Binary/ASCII STL import path |
| Slicing | `src/SliceExporter.*` | Per-material PNG export |
| Config | `src/ConfigWriter.*` | Writes backend-compatible `config.yaml` |
| Presets | `src/PresetLibrary.h`, `src/PresetLoader.*` | Loads machine/material YAML library |
| Worker | `src/SliceWorker.*` | Runs backend process and streams logs |
| Backend | `slice_1080p.py` | Merge and GCode logic, packaged as CLI tool |
| Packaging | `scripts/package_*.sh`, `scripts/package_*.ps1` | macOS and Windows packaging scripts |

## 4. UI Design

The UI is designed as an engineering workstation rather than a marketing page. It follows a Chitubox-like layout:

- Large 3D viewport on the left.
- Operational panel on the right.
- Model list and model transforms close together.
- Slicing and backend controls in a separate tab.
- Dark styling in `resources/style.qss`.

Implemented meeting requirements:

- Default material count is 3.
- Material count can be changed in the UI.
- The label “电流” was replaced by “光强” in user-facing fields.
- Selected model can be duplicated.
- The main window is resizable.
- Groove is displayed as “挡板”.
- `slide_0`、`slide_1`、`slide_2` are displayed as “刮板1”、“刮板2”、“刮板3”.
- The user-facing workflow calls a backend command-line tool rather than exposing a Python script as the formal runtime.

## 5. 3D Framework

The current 3D backend is OpenGL through Qt:

```cpp
class OpenGLView : public QOpenGLWidget, protected QOpenGLFunctions
```

This choice keeps the viewport lightweight and portable:

- Qt 5.12 includes OpenGL support.
- It avoids bringing in a full 3D engine.
- It is enough for STL previews, model colors, transforms, build plate display, camera rotation, and zoom.
- It can run on macOS, Windows, and Linux as long as the machine has a functional OpenGL driver or compatibility layer.

For the current application scope, OpenGL is not expected to be the bottleneck. The heavier parts are geometry slicing, PNG generation, image merging, and backend I/O. The current models used for demonstration render smoothly.

Potential future upgrade paths:

- Use vertex buffers more aggressively for very large STL files.
- Add mesh decimation or preview LOD.
- Move more slicing work to worker threads.
- Replace only the viewport implementation if a future graphics backend becomes necessary.

## 6. Preset Library

The preset YAML file is:

```text
config/machine_material_presets.yaml
```

At runtime the app searches common locations:

- macOS bundle resources.
- Executable directory.
- Current working directory.
- Project-root-style `config/` directories.

If no valid preset file can be loaded, the app asks the user to choose one. If loading still fails, key controls are disabled. This matches the requirement that the software should not operate without a readable machine/material library.

The preset library controls:

- Machine name and ID.
- Build dimensions.
- Output resolution.
- Default material count.
- Material names.
- Default exposure parameters.
- Strength-to-current mappings.

## 7. Strength and Current

The UI uses “光强” because that is the operator-facing concept requested in the meeting. The backend still needs current-related values to generate compatible GCode and configuration.

The bridge is:

1. User enters or selects strength values in the GUI.
2. `ConfigWriter` reads the chosen material preset.
3. Strength is converted to current using the material/machine mapping.
4. `config.yaml` includes both strength-facing and backend-compatible current-facing fields where needed.

This keeps the UI language correct while preserving backend compatibility.

## 8. Output Config

`ConfigWriter` writes one `config.yaml` per run. Important fields include:

```yaml
material_num: 3
img_path_0: /path/to/material_0
img_path_1: /path/to/material_1
img_path_2: /path/to/material_2
groove: true
slide_0: true
slide_1: false
slide_2: false
```

It also writes machine dimensions, resolution, layer thickness, exposure values, strength/current values, and material transition parameters.

## 9. Backend Runtime

The backend source remains:

```text
slice_1080p.py
```

It now supports:

```bash
python3 slice_1080p.py --config <config.yaml> --output <merged_dir>
```

Release packaging turns it into:

```text
macOS:   backend_dist/slice_merge_tool
Windows: backend_dist/slice_merge_tool.exe
```

The GUI production path calls the packaged backend executable directly. The Python source path remains useful for development and debugging.

## 10. macOS Packaging

Relevant scripts:

```text
scripts/package_backend_macos.sh
scripts/package_macos.sh
```

`package_macos.sh` performs:

1. Backend PyInstaller build.
2. CMake build.
3. Resource copying.
4. Qt deployment with `macdeployqt`.
5. iCloud metadata cleanup.
6. Ad-hoc code signing.
7. Zip creation.

Current output:

```text
dist/MultiMaterialSlicer-mac-x86_64.zip
```

The packaged app contains the backend tool in:

```text
MultiMaterialSlicer.app/Contents/Resources/slice_merge_tool
```

## 11. Windows Packaging

Relevant scripts:

```text
scripts/package_backend_windows.ps1
scripts/package_windows.ps1
```

The Windows packaging flow is:

1. Build `slice_merge_tool.exe` with PyInstaller.
2. Build the Qt application with CMake and Visual Studio.
3. Copy config and backend files.
4. Run `windeployqt.exe`.
5. Create `dist\MultiMaterialSlicer-win64.zip`.

The scripts are present and aligned with the macOS flow. A final Windows binary should be produced and validated on a real Windows machine.

## 12. Self-Test

An internal self-test mode exists:

```bash
open -W -n MultiMaterialSlicer.app --args --selftest <a.stl> <b.stl>
```

The self-test imports models, configures materials, exports PNGs, writes config, calls the backend, and validates output. It is useful for quickly checking a packaged app before a meeting.

## 13. GUI Validation

The final packaged macOS app was actually opened and operated with Computer Use. The validated GUI flow included:

- App launch.
- Default material count inspection.
- STL import through the application startup import path.
- Model selection.
- Material assignment.
- Translation, rotation, and scale editing.
- Model duplication.
- Switching to the slicing tab.
- Checking “光强” labels.
- Checking “挡板” and “刮板1/2/3” labels.
- Running the export and backend workflow.
- Confirming the success dialog.
- Checking generated files.

The only automation limitation is that Computer Use does not reliably operate the macOS native file picker. The import button itself opens the correct picker; for automation, startup STL arguments were used to exercise the same import code path.

## 14. Verified Output

A GUI-driven run produced:

```text
config.yaml
material_0/*.png
material_1/*.png
material_2/*.png
merged/config.yaml
merged/run.gcode
merged/*.png
```

The generated config contained:

- `material_num: 3`
- material image paths
- strength/current fields
- `groove: true`
- `slide_0: true`
- `slide_1: false`
- `slide_2: false`

The generated GCode contained:

- `groove`
- `slide 0`
- `proj`

## 15. Current Risks and Follow-Ups

Remaining engineering follow-ups:

- Validate the Windows package on an actual Windows machine.
- Decide whether macOS should ship x86_64 only, arm64 only, or universal.
- Add formal installer/signing/notarization for production distribution.
- Improve high-poly STL performance if real customer files are much larger than demo files.
- Strengthen the slicer geometry algorithm if production accuracy requirements exceed the current lightweight implementation.
- Add repeatable GUI automation that does not depend on native OS file dialogs.

## 16. Conclusion

The current implementation satisfies the requested integrated demo workflow: a Qt/C++ APP with editable `.ui`, OpenGL STL preview, per-model transform/material control, adjustable material count, model duplication, YAML machine/material presets, per-material PNG export, backend command-line invocation, generated `config.yaml`, generated `run.gcode`, macOS packaging, and actual GUI validation.
