# ClipGuard 2.60.0 on Unplugged Renderer v2.3.0

Branch `clipguard-2.60.0`, based on tag `v2.3.0` (70ae00d). ClipGuard changes only.

## What changed

- `Source/ClipGuard.h`: the 2.25.4 port (`CDbleFix`) replaced by ClipGuard 2.60.0
  (`CClipGuard`). Your native additions are kept: `SetNativeSettings`,
  `RequestLog`, `PicturePct` / `FixAmount` / `WhiteCeil` / `HotActive`, and the
  `%LOCALAPPDATA%\Unplugged\logs\clipguard` log folder.
- `Source/DX11VideoProcessor.cpp/.h`: the ClipGuard hooks updated to 2.60.0
  (`m_Dble*` renamed `m_ClipGuard*`): measure and apply pass with look-ahead,
  calibration LUT loading (titan.cube / valerion.cube from ClipGuard Meter),
  the overlay and the F6 info banner, the F11 log. Your `[CG]` trace lines are kept.
- `Shaders/d3d11/ps_final_pass.hlsl` + `FinalPass`: ClipGuard's Dither row
  (Static / Temporal) uses the free `_pad` slot as `ditherOffset`; Static (0, 0)
  is unchanged behaviour.
- `Source/D3DUtil/D3D11Font.cpp`: `CreateFontBitmap` passes the font flags
  (ClipGuard's bold headings need them).

## Who owns the settings

- ClipGuard.exe running (shared memory `Local\ClipGuard_v1`, refreshed every
  second): ClipGuard.exe owns every setting, as before.
- ClipGuard.exe not running: the native `ClipGuardSettings` block from
  `Settings_t` is used (fresh install: Off). Features that only ClipGuard.exe
  sets (projector model, LUT, info banner) stay off in that case.
- Neither: `clipguard.ini` next to the filter, as before.

## Left out (not ClipGuard)

Dolby Vision changes, aspect-ratio rounding, full/limited range check, banner
tone-curve and file HDR lines, ML grade off for SDR, BT.709 -> BT.2020 final
step, the full-screen "fill" fix in `VideoRenderer.cpp`, and the FSRCNNX upscaler.
