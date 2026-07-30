# EDF 4.1 Native Subtitles

Real-time subtitles for **Earth Defense Force 4.1** on Windows, implemented as
an in-process [EDF Mod Loader](https://github.com/BlueAmulet/EDFModLoader)
plugin.

![EDF 4.1 subtitle overlay](subtitle.png)

> [!WARNING]
> **AIGC disclosure**
>
> This project contains AI-generated and AI-assisted content and code.
>
> - The subtitle corpus was transcribed with Whisper and later reviewed or
>   translated with ChatGPT/Codex using an in-game terminology dictionary.
>   Lines may still be mistranscribed, mistranslated, assigned to the wrong
>   speaker, or otherwise inaccurate.
> - The original Python/Frida prototype was developed with substantial
>   assistance from Claude Sonnet 4.6.
> - The migration from that prototype to this native C++ EDF Mod Loader
>   plugin—including hook integration, rendering, build files, debugging, and
>   documentation—was substantially assisted by ChatGPT Codex 5.6 Sol.
>
> The native plugin has been compiled, core-tested, and validated in game, but
> it should not be treated as independently hand-audited software. Review the
> source and use it at your own risk. The migration changes how subtitles are
> detected and displayed; it does not verify or improve the subtitle corpus.

## What this mod aims to do

EDF 4.1 has a large amount of radio and ambient dialogue without complete
on-screen subtitles. This mod listens for the game's voice cue calls, looks up
the matching line in a subtitle data file, and renders it directly into the
game's DirectX 11 frame.

The native implementation aims to:

- work in windowed, borderless, and exclusive-fullscreen modes;
- avoid the performance and focus problems of an external Python overlay;
- run without Python, Frida, a separate overlay executable, or administrator
  privileges;
- display several overlapping radio and ambient lines at once;
- make language, timing, font, color, and layout choices configurable.

The plugin observes voice playback; it does not replace, modify, or synthesize
the game's audio.

## Project origin

This repository began as the EDF 4.1 Python/Frida prototype retained in
[`voice_hook.py`](voice_hook.py).

The native migration was based on the EDF 5 subtitle plugin
[vixen256/subtitles](https://github.com/vixen256/subtitles). Its in-process
plugin and DirectX rendering approach provided the path away from the external
overlay. The implementation here was adapted for EDF 4.1's Mod Loader entry
point, executable layout, voice functions, data files, configuration, and
rendering requirements.

The plugin also incorporates:

- [Microsoft Detours](https://github.com/microsoft/Detours) for function hooks;
- [Dear ImGui](https://github.com/ocornut/imgui) for DirectX 11 text rendering.

Their licenses are included in release packages under `Mods\Plugins\licenses`.
See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for details.

## Requirements and compatibility

- A 64-bit Windows installation of **Earth Defense Force 4.1**.
- [EDF Mod Loader](https://github.com/BlueAmulet/EDFModLoader).
- The supported Steam `EDF41.exe` build.

The current hooks were validated against an executable with PE timestamp
`0x57AAB039` and image size `0xD98000`. Other game builds may require different
voice-function addresses.

Only the EDF Mod Loader core is required. The optional `Plugins41.zip` bundle
contains other plugins and is not a dependency of this subtitle mod.

## Installing

Close the game before installing or updating the mod.

### 1. Install EDF Mod Loader

Download the EDF Mod Loader core archive and extract its contents into the EDF
4.1 game directory. `winmm.dll` and `ModLoader.ini` should be beside
`EDF41.exe`.

The default Steam location is usually similar to:

```text
...\SteamLibrary\steamapps\common\Earth Defense Force 4.1\
```

### 2. Install the subtitle plugin

Download the release archive for this mod and extract its contents into the
same game directory. Merge the included `Mods` directory with the existing
one.

The result should look like this:

```text
Earth Defense Force 4.1\
├── EDF41.exe
├── winmm.dll
├── ModLoader.ini
└── Mods\
    └── Plugins\
        ├── edf41_subtitles.dll
        ├── subtitles.toml
        ├── subtitles.txt
        ├── subtitles_en.txt
        ├── subtitles_zh.txt
        └── cuelength.txt
```

If you built the mod yourself, copy the **contents** of `out` into the game
directory. Do not copy the `out` directory itself.

### 3. Run the game

Start EDF 4.1 normally through Steam. A successful load creates:

```text
Mods\Plugins\edf41_subtitles.log
```

The end of the initialization section should contain:

```text
EDF 4.1 native subtitles initialized successfully.
```

## Configuration

Edit this file while the game is closed:

```text
Mods\Plugins\subtitles.toml
```

Paths in the configuration may be absolute or relative to
`Mods\Plugins`. Colors use six-digit `#RRGGBB` values.

### Select a subtitle language

The package includes three subtitle files:

```toml
# Legacy/default data
subtitle_file = "subtitles.txt"

# English
subtitle_file = "subtitles_en.txt"

# Traditional Chinese
subtitle_file = "subtitles_zh.txt"
```

Use only one `subtitle_file` line. All bundled text files are UTF-8.

### Common settings

| Setting | Purpose |
|---|---|
| `subtitle_file` | Subtitle text file to load |
| `duration_file` | Per-cue audio duration file |
| `font_path` | Font file; leave empty for automatic selection |
| `font_size` | Subtitle text size |
| `maximum_subtitles` | Maximum number of simultaneous lines |
| `maximum_width` | Maximum subtitle box width in pixels |
| `margin_bottom` | Distance from the bottom of the game frame |
| `padding_x`, `padding_y` | Inner subtitle-box padding |
| `line_gap` | Space between simultaneous subtitles |
| `event_color` | Event and radio dialogue color |
| `ambient_color` | Ambient NPC dialogue color |
| `background_color` | Subtitle background color |
| `background_alpha` | Background opacity from `0.0` to `1.0` |

For example, to use an explicit Traditional Chinese font:

```toml
font_path = "C:/Windows/Fonts/msjh.ttc"
font_size = 24.0
```

### Timing settings

| Setting | Purpose |
|---|---|
| `fallback_display_ms` | Duration used when a cue has no duration entry |
| `minimum_display_ms` | Minimum time a subtitle remains visible |
| `tail_padding_ms` | Extra display time after the recorded cue duration |
| `fade_in_ms` | Fade-in duration |
| `fade_out_ms` | Fade-out duration |

### Diagnostics and advanced settings

Set the following option to log every detected cue and every cue missing from
the selected subtitle file:

```toml
debug_voice_events = true
```

This can make the log grow quickly, so turn it off after troubleshooting.

`event_voice_rva` and `ambient_voice_rva` are executable addresses relative to
`EDF41.exe`. They are not ordinary appearance settings. Do not change them
unless you are porting the mod to a different executable build and have
identified the functions in a disassembler.

## Subtitle data

The text and timing files use simple `key=value` records:

| File | Format | Purpose |
|---|---|---|
| `subtitles*.txt` | `cue=subtitle text` | UTF-8 subtitle text |
| `cuelength.txt` | `cue=duration_ms` | Audio duration in milliseconds |

Only the first `=` on a line separates the cue from its value, so subtitle text
may contain additional `=` characters. Cue names ending in `_E` or `_S` are
normalized to the base cue before lookup. Missing duration entries use
`fallback_display_ms`.

You can provide your own subtitle and duration files and select them through
`subtitles.toml`.

## Current limitations

- Subtitle coverage and accuracy depend on the selected data file. Some voice
  cues are missing, and the bundled AI-assisted text contains errors.
- The voice hooks target a specific Steam executable build.
- The mod requires EDF Mod Loader and the DirectX 11 game renderer.
- A game or Mod Loader update may change compatibility even when the plugin
  itself has not changed.

## Troubleshooting

### `ModLoader.log` is not created

EDF Mod Loader itself did not start. Confirm that its `winmm.dll` and
`ModLoader.ini` are directly beside `EDF41.exe`.

### The plugin is absent from `ModLoader.log`

Confirm that `edf41_subtitles.dll` is directly under `Mods\Plugins`, without an
extra archive, release, or `out` directory in the path.

### `edf41_subtitles.log` is not created

Read `ModLoader.log`. The Mod Loader may have found the DLL but rejected it
before the plugin entry point ran. Make sure you are using a current EDF 4.1
build of the subtitle plugin.

### Initialization succeeds, but no subtitles appear

Enable cue logging:

```toml
debug_voice_events = true
```

Reproduce a voice line and inspect `edf41_subtitles.log`. A detected cue with no
matching text indicates incomplete subtitle data. No detected cues may indicate
an unsupported `EDF41.exe` build.

### Text appears as squares

Select a font that covers the chosen language:

```toml
font_path = "C:/Windows/Fonts/msjh.ttc"
```

### The plugin reports an invalid or non-executable voice address

The installed `EDF41.exe` does not match the supported build or the configured
RVAs were changed. Restore the packaged configuration. Porting another build
requires re-identifying both functions; do not guess addresses.

## Building from source (optional)

Building is only necessary if you want to modify the plugin. Release users do
not need a compiler, Python, Frida, Rust, MSYS2, or administrator elevation.

The supported build environment is a standalone 64-bit UCRT MinGW-w64
toolchain containing:

- GCC and G++;
- CMake;
- Ninja;
- Git for the first dependency download.

Add its `bin` directory to `PATH`. For example:

```bat
set PATH=D:\bin\mingw64\bin;%PATH%
```

From CMD in the repository root, run:

```bat
build.cmd Release
```

If the toolchain is installed at `D:\bin\mingw64`, the script can locate it
automatically. The first configuration downloads pinned revisions of Detours
and Dear ImGui. Later builds reuse the local dependency checkout.

The script builds the DLL, runs the native core tests, and prepares:

```text
out\Mods\Plugins\
```

For a symbol-rich diagnostic build:

```bat
build.cmd Debug
```

The main source directories are:

```text
Source\   Native plugin, hooks, configuration, and renderer
Tests\    Native data, timing, and stacking regression tests
dist\     Packaged default configuration
```
