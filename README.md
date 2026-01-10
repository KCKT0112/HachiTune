<div align="center">
  <h1> HachiTune </h1>

  <img src="logo.png" alt="Logo" width="300" />

  <p>
    A lightweight HachiTune built with JUCE framework, featuring FCPE pitch detection and neural vocoder synthesis.
  </p>

  <p>
    <a href="#features">Features</a> •
    <a href="#building">Building</a> •
    <a href="#usage">Usage</a> •
    <a href="#keyboard-shortcuts">Shortcuts</a>
  </p>
</div>

## Features

- Piano roll interface for visualizing and editing pitch
- FCPE (Fast Context-aware Pitch Estimation) neural pitch detection
- ONNX Runtime vocoder integration with real-time preview
- GPU acceleration support (CUDA, DirectML, CoreML)
- VST3 and AU plugin formats with ARA support
- Multi-language UI (English, 简体中文, 繁體中文, 日本語)
- Undo/redo for pitch modifications
- Real-time audio playback
- Import WAV/MP3/FLAC, export WAV

## Prerequisites

- CMake 3.22+
- C++17 compatible compiler
- Git (for submodules)

### Platform-specific

| Platform | Requirements |
|----------|-------------|
| Windows | Visual Studio 2019+, Windows SDK |
| macOS | Xcode with command line tools |
| Linux | GCC/Clang, ALSA dev libraries |

## Building

### Quick Build

```bash
# Clone with submodules
git clone --recursive https://github.com/KCKT0112/HachiTune.git
cd HachiTune

# Build
./build.sh
```

### Manual Build

```bash
# Initialize submodules
git submodule update --init --recursive

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

### Build Options

| Option | Description |
|--------|-------------|
| `ARA_SDK_PATH` | Path to ARA SDK for ARA plugin support |
| `ONNXRUNTIME_URL` | Custom ONNX Runtime download URL |

### Output

Build artifacts are in `build/PitchEditorPlugin_artefacts/Release/`:
- `HachiTune.app` - Standalone application (macOS)
- `HachiTune.vst3` - VST3 plugin
- `HachiTune.component` - AU plugin (macOS)

## Usage

1. **Open**: Load audio file (WAV/MP3/FLAC)
2. **Analyze**: Audio is automatically analyzed for pitch
3. **Edit**:
   - Select mode: Click notes to select, drag to adjust pitch
   - Draw mode: Draw directly on pitch curve
4. **Preview**: Changes are synthesized in real-time
5. **Export**: Save modified audio as WAV

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Space` | Play/Pause |
| `Escape` | Stop / Exit draw mode |
| `D` | Toggle draw mode |
| `Cmd/Ctrl+Z` | Undo |
| `Cmd/Ctrl+Shift+Z` | Redo |
| `Cmd/Ctrl+S` | Save project |
| `Mouse wheel` | Scroll horizontally |
| `Shift+wheel` | Scroll vertically |
| `Cmd/Ctrl+wheel` | Zoom |

## Settings

Access via Edit > Settings:

- **Language**: UI language selection
- **Device**: Inference device (CPU/CUDA/DirectML/CoreML)
- **GPU Device**: GPU selection for multi-GPU systems
- **Threads**: CPU thread count (0 = auto)
- **Audio**: Output device settings (standalone only)

## Project Structure

```
pitch_editor_juce/
├── Source/
│   ├── Audio/          # Audio engine, pitch detection, vocoder
│   ├── Models/         # Note, Project data models
│   ├── UI/             # UI components
│   ├── Utils/          # Constants, localization, undo manager
│   └── Plugin/         # VST3/AU plugin wrapper
├── Resources/
│   └── lang/           # Localization files
├── third_party/
│   ├── JUCE/           # JUCE framework (submodule)
│   └── ARA_SDK/        # ARA SDK (submodule, optional)
└── CMakeLists.txt
```
