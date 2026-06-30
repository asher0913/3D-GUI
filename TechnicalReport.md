# MultiMaterialSlicer Technical Report

Updated: 2026-06-29

## 1. Summary

MultiMaterialSlicer is a Qt/C++ desktop application for a multi-material resin printer slicing workflow. The current version integrates the previous external process of exporting per-material folders and then running a Python merge script. The application now imports STL models and STEP assemblies, displays them in a lightweight 3D view, lets the user edit transforms and material assignments, exports per-material PNG slices, writes `config.yaml`, and calls a packaged backend command-line tool to generate merged images and `run.gcode`.

The application uses:

- C++17
- CMake
- Qt 5.12.12
- Qt Widgets
- Qt Designer `.ui`
- `QOpenGLWidget` for the 3D viewport
- Python source for the merge backend, packaged with PyInstaller for release
- CadQuery/OCP-based STEP conversion helper, packaged with PyInstaller for release

## 2. Main Workflow

The implemented workflow is:

1. Load the machine/material preset YAML library.
2. Import one or more STL files, or import a STEP/STP assembly.
3. Display imported models in the OpenGL viewport.
4. Select a normal model and edit translation, rotation, uniform scale, and material.
5. For STEP assemblies, select the parent node to move/rotate/scale the whole assembly, and select child solids to assign materials independently.
6. Optionally duplicate selected models.
7. Configure material count and per-material exposure/strength settings.
8. Export per-material PNG folders.
9. Write `config.yaml`.
10. Run backend command-line tool:

```bash
slice_merge_tool --config <config.yaml> --output <merged_dir>
```

11. Produce merged PNG output and `run.gcode`.

## 3. Architecture

| Area | Implementation | Notes |
|---|---|---|
| UI | `ui/MainWindow.ui`, `src/MainWindow.*` | Qt Designer-editable UI with resizable main window |
| 3D View | `src/OpenGLView.*` | Lightweight OpenGL viewport based on `QOpenGLWidget` |
| STL Loading | `src/StlMesh.*` | Binary/ASCII STL import path with finite-coordinate and size validation |
| STEP Loading | `tools/step_to_stl_parts.py`, `backend_dist/step_to_stl_parts` | Converts STEP solids to per-part STL plus `manifest.json` |
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
- Groove is displayed as “挡板” inside the slicing page's regular options.
- `slide_0`、`slide_1`、`slide_2` are displayed as “刮板1”、“刮板2”、“刮板3” inside the regular options.
- The print settings group now lives on the slicing page rather than the model page.
- The advanced options group now exposes backend machine/GCode parameters such as `max_height`, `z_acc_h`, `clean_tank`, `dry_tank`, `drop_time_bottom`, `ASS`, and `ASS_times`.
- A manually edited flat `config.yaml` can be imported to populate print settings, exposure settings, regular options, and advanced parameters.
- The user-facing workflow calls a backend command-line tool rather than exposing a Python script as the formal runtime.
- STEP/STP assembly import is available through “导入 STEP”.
- STEP assemblies appear as tree parents with child solid leaves.
- STEP child solids can have independent materials.
- STEP parent transforms move all children together while preserving their imported relative geometry.

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
The viewport now uploads each mesh into OpenGL vertex buffers once and reuses those buffers during rendering. Shared mesh ownership is used for duplicated models, so copying a model no longer deep-copies all vertex data. Rim lighting now uses normals transformed into view space, so the visual highlight stays camera-relative.

Potential future upgrade paths:

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
It now writes editable advanced machine/GCode parameters from the UI table instead of using only fixed defaults.

## 8.5. STEP Assembly Import

STEP import is intentionally implemented through a replaceable command-line helper rather than embedding a full CAD kernel directly into the Qt GUI. The default helper is:

```text
tools/step_to_stl_parts.py
```

Release packaging turns it into:

```text
macOS:   backend_dist/step_to_stl_parts
Windows: backend_dist/step_to_stl_parts.exe
```

The helper runs:

```bash
step_to_stl_parts --input <model.step> --output <parts_dir>
```

It writes one STL per STEP solid plus:

```text
manifest.json
```

The GUI reads the manifest and creates a tree:

```text
step示例.step
  实体1    material 1
  实体2    material 2
```

The current default converter splits by solid using OCP/OpenCascade. It has been validated with `demo_step/step示例.step`, which produces two child solids. The manifest-based boundary leaves room to replace the converter later with a deeper XCAF/OCC assembly parser if production files require full multi-level CAD assembly hierarchy, instance reuse, or richer naming.

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
MultiMaterialSlicer.app/Contents/MacOS/slice_merge_tool
MultiMaterialSlicer.app/Contents/MacOS/step_to_stl_parts
```

## 11. Windows Packaging

Relevant scripts:

```text
scripts/package_backend_windows.ps1
scripts/package_windows.ps1
```

The Windows packaging flow is:

1. Build `slice_merge_tool.exe` with PyInstaller.
2. Build `step_to_stl_parts.exe` with PyInstaller and CadQuery/OCP.
3. Build the Qt application with CMake and Visual Studio.
4. Copy config, backend, and STEP converter files.
5. Run `windeployqt.exe`.
6. Copy `demo_stl` and `demo_step` into `examples/`.
7. Smoke-check required runtime files and helper startup.
8. Optionally run packaged app self-test with `-RunSelfTest`.
9. Create `dist\MultiMaterialSlicer-win64.zip`.

The scripts are present and aligned with the macOS flow. Python 3.10-3.12 is recommended because CadQuery/OCP wheels may not exist for newer Python versions. A final Windows binary should still be produced and validated on a real Windows machine.

## 12. Self-Test

An internal self-test mode exists:

```bash
open -W -n MultiMaterialSlicer.app --args --selftest <a.stl> <b.stl>
```

It also supports STEP assembly input:

```bash
open -W -n MultiMaterialSlicer.app --args --selftest demo_step/step示例.step
```

The self-test imports models, configures materials, exports PNGs, writes config, calls the backend, and validates output. It is useful for quickly checking a packaged app before a meeting.
The self-test now uses Qt's system temporary directory (`QDir::tempPath()`), so it is suitable for both macOS and Windows.

## 13. GUI Validation

The final packaged macOS app was actually opened and operated with Computer Use. The validated GUI flow included:

- App launch.
- Default material count inspection.
- STL import through the application startup import path.
- STEP import through the application startup import path.
- STEP model tree display: `step示例.step -> 实体1 / 实体2`.
- STEP child material edit from material 1 to material 2.
- STEP parent transform editing with child solids moving together.
- Model selection.
- Material assignment.
- Translation, rotation, and scale editing.
- Model duplication.
- Switching to the slicing tab.
- Checking “光强” labels.
- Checking “挡板” and “刮板1/2/3” labels in regular options.
- Checking the advanced machine/GCode parameter table and `config.yaml` import entry.
- Running the export and backend workflow.
- Confirming the success dialog.
- Checking generated files.

The only automation limitation is that Computer Use does not reliably operate the macOS native file picker. The import buttons themselves open the correct pickers; for automation, startup STL/STEP arguments were used to exercise the same import code paths.

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

Additional STEP validation:

- `tools/step_to_stl_parts.py --input demo_step/step示例.step --output /tmp/mms_step_test` produced two STL files and `manifest.json`.
- `backend_dist/step_to_stl_parts --input demo_step/step示例.step --output /tmp/mms_step_exe_test` produced the same two-part output.
- `open -n build-x86_64/MultiMaterialSlicer.app --args --selftest demo_step/step示例.step` completed and generated `config.yaml`, per-material PNGs, merged PNGs, and `run.gcode`.

## 15. Stability And Performance Fixes

The latest stabilization pass addressed the reviewed critical/high-priority issues:

- `SliceExporter` now rejects non-finite transformed geometry and validates layer count math before integer conversion.
- `StlMesh` rejects NaN/Inf vertices, validates binary STL declared sizes, and checks triangle-count multiplication before reserving memory.
- Model tree material editors resolve models by stable IDs rather than stale row indices.
- `SliceWorker` supports progress signals, user cancellation, backend startup timeout, and a 10-minute backend execution timeout with process kill.
- STEP conversion also has a timeout instead of waiting forever.
- `OpenGLView` uses VBOs for model meshes and releases unused buffers.
- Duplicated `ModelInstance` objects share mesh data through `QSharedPointer<StlMesh>`.
- Strength/current interpolation sorts finite mapping points before interpolation, so YAML mapping order no longer changes the result.

Verification performed after these fixes:

- `cmake --build build-x86_64 -j$(sysctl -n hw.ncpu)` passed.
- `xmllint --noout ui/MainWindow.ui` passed.
- `python3 -m py_compile slice_1080p.py tools/step_to_stl_parts.py` passed.
- Bad STL with a NaN vertex was rejected without crashing the workflow.
- `build-x86_64/MultiMaterialSlicer.app/Contents/MacOS/MultiMaterialSlicer --selftest demo_step/step示例.step` passed.
- `bash scripts/package_macos.sh` regenerated `dist/MultiMaterialSlicer-mac-x86_64.zip`.
- The final zip was extracted to `/tmp/mms_pkg_fix_verify`, then `--selftest demo_step/step示例.step` passed from the extracted app.
- `codesign --verify --deep --strict --verbose=2 /tmp/mms_pkg_fix_verify/MultiMaterialSlicer.app` passed.
- Computer Use GUI validation on the extracted app completed STL import, material edits, transform edit, duplicate/remove, slicing page inspection, full GUI export, success dialog, and output-file verification.

The regenerated macOS zip is:

```text
dist/MultiMaterialSlicer-mac-x86_64.zip
SHA-256: 33512d426ae6e137f23b82d851f5893f4cc32ecdd3b9d2c58b0a425e394e644a
```

## 16. Medium Audit Fixes

The 2026-06-30 medium-priority audit was reviewed against the current codebase. The following actionable issues were fixed:

- Material transition output is no longer hard-coded only. Global defaults can be configured through `material_transition_enabled`, `material_transition_current`, and `material_transition_time`; imported per-pair keys such as `m0Tom1_current` are preserved and used when writing `config.yaml`.
- Binary STL detection now prefers structurally valid binary files, so a binary STL header containing `facet` or `vertex` is not misparsed as ASCII.
- The STL preview loader rejects files larger than 512 MB before `readAll()`, reducing OOM risk for accidental huge files.
- Preset numeric parsing now validates `toInt()` / `toDouble()` results and rejects invalid dimensions, pixel sizes, material limits, and mismatched current/strength maps.
- `runWorkflow()` now checks for at least one visible non-empty model before starting.
- Development Python lookup now uses `QStandardPaths::findExecutable("python3")` first, then falls back through multiple Homebrew/system candidates.
- The preset library lookup supports `--config` / `--preset-config` and a CMake-provided `PRESET_CONFIG_PATH`; it no longer scans arbitrary adjacent YAML files.
- Build-plate surface, grid, border, and axis geometry are cached and only regenerated when the plate size changes.
- Slice export reuses one grayscale `QImage` per material instead of reallocating an image for every layer/material pair.
- Assembly/model ID counters use 64-bit counters.

Items judged valid but intentionally left as product-level follow-ups rather than small bug fixes:

- Project save/load.
- Undo/redo.
- Moving self-test code into a dedicated test target.
- Full `tr()`/Qt Linguist internationalization.
- A formal unit-test suite.

## 17. Current Risks and Follow-Ups

Remaining engineering follow-ups:

- Validate the Windows package on an actual Windows machine.
- Add project save/load for iterative work sessions.
- Add undo/redo for destructive and transform/material operations.
- Split `--selftest` into a dedicated test harness or Qt Test target when the codebase moves beyond demo stage.
- Add full Qt internationalization if English/Japanese/Korean users become a delivery target.
- Add formal unit tests for `StlMesh`, `PresetLoader`, `ConfigWriter`, `SliceExporter`, and strength/current interpolation.
- Decide whether macOS should ship x86_64 only, arm64 only, or universal.
- If distributing STEP import to Intel Mac users, rebuild `step_to_stl_parts` under an x86_64 Python environment. The helper built on this Apple Silicon machine is arm64 and has been validated on Apple Silicon.
- Add formal installer/signing/notarization for production distribution.
- Improve high-poly STL performance further if real customer files are much larger than demo files, mainly through preview LOD or background import.
- Extend the STEP converter to preserve full XCAF assembly hierarchy if production STEP files need deeper nested CAD trees beyond per-solid splitting.
- Strengthen the slicer geometry algorithm if production accuracy requirements exceed the current lightweight implementation.
- Add repeatable GUI automation that does not depend on native OS file dialogs.

## 18. Conclusion

The current implementation satisfies the requested integrated demo workflow: a Qt/C++ APP with editable `.ui`, OpenGL STL/STEP preview, per-model transform/material control, STEP parent/child tree behavior, adjustable material count, model duplication, YAML machine/material presets, config import, editable advanced machine/GCode parameters, per-material PNG export, backend command-line invocation, generated `config.yaml`, generated `run.gcode`, macOS packaging, and actual GUI validation.
