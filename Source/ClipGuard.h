/*
 * ClipGuard Media Player 0.9.0 (ClipGuard, formerly DBLEFix) -- highlight compensation for projectors with dynamic laser dimming
 * (DBLE). In dark scenes the projector lowers the laser and raises the signal
 * gain, which pushes bright highlights into clipping. This lowers highlights in
 * dark pictures first, so after the projector's gain they land below clipping.
 *
 * Everything runs on the FINISHED SDR picture: one small measurement pass and
 * one full-screen pass at the end of the post-scale chain (after the grade,
 * taste dials, bar repaint and custom shaders; before interlace output and the
 * final calibration-LUT / dither pass). Nothing upstream is changed.
 *
 * Settings come from ClipGuard.exe through shared memory (live, with hotkeys and
 * the on-screen overlay) or, without it, from clipguard.ini next to this filter.
 *
 * This file is part of a modified Unplugged / MPC Video Renderer, GNU GPL v3.
 */

#pragma once

#include <d3d11.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include <cstring>
#include <dwrite.h>
#include <d3dcommon.h>
#include "D3DUtil/D3D11Font.h"

// Defined in Shaders.cpp (runtime HLSL compile via d3dcompiler_47.dll).
HRESULT CompileShader(const std::string& srcCode, const D3D_SHADER_MACRO* pDefines, LPCSTR pTarget, ID3DBlob** ppCode);

// ---- shared memory with ClipGuard.exe ----------------------------------------
// Layout must match ClipGuard.cs (StructLayout Sequential, Pack = 4). 256 bytes.
#pragma pack(push, 4)
struct ClipGuardShared
{
	uint32_t magic;          // kClipGuardMagic
	uint32_t version;        // 1
	// written by ClipGuard.exe
	int32_t  enabled;
	float    strength;       // 0..1
	float    triggerPct;     // "Fix starts below": picture brightness (% of SDR white light) where the fix begins
	float    knee;           // highlight start, signal 0.20..0.95
	int32_t  view;           // 0 normal, 1 fix areas, 2 split, 3 side by side
	int32_t  overlayOn;
	int32_t  overlayPinned;  // 1 = always shown, 0 = hides 8 s after the last key
	int32_t  selected;       // overlay row
	uint32_t lastKeyTick;    // GetTickCount() of the last hotkey
	uint32_t exeTick;        // GetTickCount(), refreshed every second while ClipGuard.exe runs
	// written by the renderer
	uint32_t rendTick;
	float    pictureBrightness; // % of SDR white light, content area
	float    sceneNits;         // Unplugged's own scene mean (source nits), -1 if not available
	float    amount;            // smoothed fix amount 0..1
	float    ceiling;           // where peak white goes out
	float    kneeEff;           // knee actually used
	int32_t  outputPQ;          // 1 = HDR output, the fix is off
	float    srcMax;            // unused since 2.1 (0)
	float    clipFrac;          // fraction of picture cells with clipped pixels
	float    bottomBarFrac;     // bottom letterbox bar, fraction of the video height
	// written by ClipGuard.exe (added later, so after the renderer fields)
	float    colorProtect;      // 0 = judge pixels by their strongest channel, 1 = by brightness
	float    fullPct;           // "Full effect below": picture brightness with the full fix (0 = old exe: trigger/4)
	float    textSkipPct;       // "Text skip": no fix when Text is above this % (0 = off)
	// written by the renderer
	float    textPct;           // % of the frame's highlights that sit bright-on-black with no glow (text, stars)
	int32_t  textSkipped;       // 1 = this frame reads as text, the fix is held off
	// written by ClipGuard.exe
	float    hotSpot;           // "Hot spot" 0..1: local exposure cut on small, very bright spots, 1 = 3 stops (0 = off)
	// written by the renderer
	float    hotFrac;           // share of picture cells at the frame's peak (0..1)
	int32_t  hotActive;         // number of hot spots being dimmed on this frame (0 = none)
	// written by ClipGuard.exe
	int32_t  fixShape;          // "Fix shape": 0 = Normal, 1 = Anchor (was Warm spot until 2.22.0)
	int32_t  unused124;         // was the warm spot count
	// written by ClipGuard.exe
	int32_t  grid;              // 0 = normal measuring grid (128x72), 1 = fine (256x144)
	int32_t  lookAhead;         // frames the picture is held back: 0, 2, 4, 6, 12 = High Performance (1 = 2, from 2.12.0)
	// log (F11): ClipGuard.exe asks, the renderer saves, ClipGuard.exe uploads
	uint32_t logRequest;        // written by ClipGuard.exe: id of the latest log asked for (0 = none)
	uint32_t logDone;           // written by the renderer: id of the last log saved
	int32_t  logState;          // written by ClipGuard.exe: 0 none, 1 saving, 2 sending, 3 sent, 4 failed, 5 no token
	uint32_t logStateTick;      // written by ClipGuard.exe: GetTickCount() when logState changed
	// written by ClipGuard.exe
	float    hotBelowPct;       // "Hot spot below": no hot spot in pictures brighter than this % (0 = off)
	float    gradedSpot;        // "Graded spot" 0..1: gentle local highlight dimming of small graded highlights (was Grain until 2.22.0)
	int32_t  buildKind;         // written by ClipGuard.exe: 0 = public build, 1 = test build (auto-update, log upload)
	// written by the renderer: hot spot test video end-of-part code, (count << 8) | part (0 = none seen)
	uint32_t testMarker;
	// written by ClipGuard.exe
	int32_t  spotMask;          // "Hot spot mask": 0 = Patch (the whole patch), 1 = Pixel (only the light, feathered)
	int32_t  infoOn;            // F6: 1 = the info banner is shown along the top of the picture
	float    gpuLoad;           // written by ClipGuard.exe while the banner shows: the graphics card's load in % + 1 (0 = not known)
	int32_t  dimFire;           // written by ClipGuard.exe: "Dim fire": 1 = orange and red lights are hot spots too, 0 = they are left alone
	int32_t  upscaler;          // written by ClipGuard.exe: "Upscaler": 0 = Normal, 1 = FSRCNNX (2.43.0)
	int32_t  edition;           // written by ClipGuard.exe: 0 = ClipGuard, 1 = ClipGuard+ (High Performance, Upscaler, Kaleidescape) (2.44.0)
	int32_t  kscape;            // written by ClipGuard.exe: 1 = Kaleidescape is playing and Look-ahead was switched for it (2.44.0)
	int32_t  dither;            // written by ClipGuard.exe: "Dither": 0 = Static, 1 = Temporal (2.45.0)
	int32_t  projector;         // written by ClipGuard.exe: "Projector": 0 = Universal, 1 = Titan Noir Max (Predictive) (2.46.0); Valerion (2) removed again in 2.51.2
	int32_t  lutOff;            // written by ClipGuard.exe: F5, calibration LUT off for comparing (2.48.2)
	int32_t  lut;               // written by ClipGuard.exe: "LUT": 0 = None, 1 = Titan (titan.cube), 2 = Valerion (valerion.cube) (2.50.0)
	int32_t  unusedSource;      // was the Test-only Source row (2.52.0-2.63.0), no longer read
	uint32_t reserved[10];
};
#pragma pack(pop)
static_assert(sizeof(ClipGuardShared) == 256, "ClipGuardShared must stay 256 bytes (ClipGuard.cs mirrors it)");

constexpr uint32_t kClipGuardMagic   = 0x454C4244; // "DBLE"

// 2.44.4: the player's video window, for the info banner. Written by
// CMpcVideoRenderer::SetWindowPosition, read by the banner.
inline int  g_cgPlayerW = 0, g_cgPlayerH = 0;   // the player's window (client area) the video sits in
inline int  g_cgAskedW = 0, g_cgAskedH = 0;     // the video area the player asked for
inline bool g_cgFilled = false;                 // the asked area was a few pixels short and the window was filled
constexpr wchar_t  kClipGuardMapName[] = L"Local\\ClipGuard_v1";

struct ClipGuardSettings
{
	bool  enabled    = true;
	float strength   = 0.35f;
	float triggerPct = 10.0f;  // fix starts below
	float fullPct    = 3.0f;   // full effect below
	float knee       = 0.40f;
	float colorProtect = 0.90f;
	float textSkipPct = 25.0f;
	float hotSpot = 0.6f;
	float hotBelow = 3.0f;   // hot spot only in pictures darker than this %
	int   fixShape = 1;      // 0 = Normal, 1 = Anchor
	float gradedSpot = 0.5f; // graded spot dimming (0 = off)
	int   spotMask = 1;      // 0 = Patch, 1 = Pixel
	bool  dimFire = false;   // "Dim fire": false = clearly orange or red lights (flames, candles) are not hot spots
	int   upscaler = 0;      // "Upscaler": 0 = Normal, 1 = FSRCNNX (neural 2x for 1080p on 4K)
	bool  plus = false;      // ClipGuard+ (2.44.0): High Performance, Upscaler and Kaleidescape are offered only here
	bool  kscape = false;    // Look-ahead set by ClipGuard+ for a playing Kaleidescape
	int   laSource = 0;      // 2.68.0: whose look-ahead: 1 Kaleidescape, 2 Video Processor (capture), 3 Media Player (file), ClipGuard+ Player
	int   dither = 0;        // "Dither": 0 = Static (the renderer's fixed pattern), 1 = Temporal (moved every frame)
	bool  lutOff = false;    // F5: the calibration LUT off for comparing (not saved)
	int   projector = 0;     // "Projector": 0 = Universal (Reactive), 1 = Titan Noir Max (Predictive)
	int   lut = 0;           // 2.50.0: "LUT": 0 = None, 1 = Titan, 2 = Valerion
	int   grid = 1;
	int   lookAhead = 4;  // frames: 0, 2, 4, 6
	int   view       = 0;
	bool  overlayOn  = false;
	bool  infoOn     = false;  // info banner (F6)
	bool  pinned     = false;
	bool  testBuild  = false;  // ClipGuard Test: Restart & update, logs sent
	int   selected   = 0;
	DWORD lastKeyTick = 0;
};

// ---- shaders (compiled at device creation with d3dcompiler_47) --------------

static const char kClipGuardMeasureHLSL[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer ClipGuardMeasure : register(b0)
{
	float2 CellUV;
	float2 pad0;
	float4 SrcMap;
	float  pad1;
	float  pad2;
	float  MKnee;
	float  pad3;
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

// Per cell: R = mean linear luminance, G = brightest signal,
// B = highlight samples in a bright-on-black cell, A = highlight samples.
// A highlight is a sample above Highlight start. A cell is bright-on-black when
// it holds highlights AND near-black (at least 8 of 64 samples), with almost
// nothing in between: text, credits and stars. The inside of a lamp, a window
// or a lit wall has no near-black, and its edges have a glow of in-between
// levels, so those cells are not.
// A also carries, as 100 x n, the samples within 0.01 of the cell's brightest
// (a flat core: a bulb, a headlamp; skin and lit surfaces are graded).
// B also carries, as 100 x w, how warm the cell's bright half is: w = 0..99
// for (red - blue) / red of the samples at half the cell's peak or more.
float4 main(PS_INPUT input) : SV_Target
{
	const float2 origin = input.Tex - 0.5 * CellUV;
	float sumL = 0.0;
	float maxS = 0.0;
	[loop]
	for (int y = 0; y < 8; y++) {
		[loop]
		for (int x = 0; x < 8; x++) {
			const float2 uv = origin + (float2(x, y) + 0.5) * (CellUV / 8.0);
			const float3 c = saturate(tex.SampleLevel(samp, uv, 0).rgb);
			const float3 lin = pow(c, 2.2);
			sumL += dot(lin, float3(0.2126, 0.7152, 0.0722));
			maxS = max(maxS, max(c.r, max(c.g, c.b)));
		}
	}
	float bright = 0.0;
	float textN = 0.0;
	float flat = 0.0;
	float sumR = 0.0;
	float sumB = 0.0;
	if (maxS > min(MKnee, 0.75)) {
		float mid = 0.0;
		float dark = 0.0;
		[loop]
		for (int y2 = 0; y2 < 8; y2++) {
			[loop]
			for (int x2 = 0; x2 < 8; x2++) {
				const float2 uv = origin + (float2(x2, y2) + 0.5) * (CellUV / 8.0);
				const float3 c = saturate(tex.SampleLevel(samp, uv, 0).rgb);
				const float m = max(c.r, max(c.g, c.b));
				if (m >= maxS - 0.01) flat += 1.0;
				if (m >= 0.5 * maxS) { sumR += c.r; sumB += c.b; }
				if (m > MKnee) bright += 1.0;
				else if (m > 0.08) mid += 1.0;
				else dark += 1.0;
			}
		}
		if (mid <= 4.0 && dark >= 8.0) textN = bright;
	}
	const float warm = sumR > 0.0 ? saturate((sumR - sumB) / sumR) : 0.0;
	return float4(sumL / 64.0, maxS, textN + 100.0 * floor(warm * 99.0 + 0.5), bright + 100.0 * flat);
}
)HLSL";

static const char kClipGuardMainHLSL[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
Texture2D spotMask : register(t2);
SamplerState sampMask : register(s2);

cbuffer ClipGuardMain : register(b0)
{
	float Ceil;
	float Knee;
	float Bypass;
	float View;
	float4 SrcMap;
	float Pad0;
	float Pad1;
	float Pad2;
	float TexW;
	float ColorProtect;
	float SpotScale;
	float SpotFade;
	float SpotOn;
	float4 SpotCenter[8];  // xy and zw = centres of up to 16 hot spots, in picture UV
	float SpotCount;
	float Anchor;          // Fix shape Anchor: the frame's peak (strongest channel); 0 = Normal
	float GradeCeil;       // where peak white goes inside a graded spot patch
	float GradeOn;
	float4 SpotEase[4];    // per hot spot: 1 = full dimming, less = a large light, eased
	float SpotPixel;       // Hot spot mask Pixel: 1 = only the light itself is dimmed
	float SpotRad;         // its feather radius, in texture UV (height)
	float RampCeil;        // Projector Titan Noir Max: the highest signal the laser can show now (1 = no ramp squeeze)
	float RampKnee;        // ramp squeeze: untouched below this signal
	float RampPeak;        // ramp squeeze: the shown frame's peak (top of the squeeze)
	float RampKeep;        // while the laser climbs: pixels at or above this keep their level (2 = none)
	float RampPad1;
	float RampPad2;
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

float Max3(float3 c) { return max(c.r, max(c.g, c.b)); }

// Which level of a pixel the roll-off acts on. Colour protection 0: its strongest
// channel (a saturated red counts like white). 1: its brightness (luma), so bright
// saturated reds and blues are pulled down far less; whites and pale colours alike.
float Driver(float3 c)
{
	const float mx = Max3(c);
	const float y = dot(c, float3(0.2126, 0.7152, 0.0722));
	return lerp(mx, min(y, mx), saturate(ColorProtect));
}

float3 Rolloff(float3 c, float ceil, float k)
{
	if (ceil >= 0.999 || ceil <= 0.001)
		return c;
	const float m = Driver(c);
	if (m <= k)
		return c;
	const float r = (ceil - k) / max(1.0 - k, 1e-4);
	const float t = (m - k) / max(1.0 - k, 1e-4);
	const float m2 = k + (1.0 - k) * t / (1.0 + t * (1.0 / max(r, 1e-4) - 1.0));
	return c * (m2 / m);
}

float3 Tint(float3 picture, float3 tint, float amount)
{
	const float g = dot(picture, float3(0.2126, 0.7152, 0.0722));
	return lerp(picture, tint * (0.35 + 0.65 * g), amount);
}

// The normal roll-off with a given driver level m.
float3 RolloffM(float3 c, float m, float ceil, float k)
{
	if (ceil >= 0.999 || ceil <= 0.001 || m <= k)
		return c;
	const float r = (ceil - k) / max(1.0 - k, 1e-4);
	const float t = (m - k) / max(1.0 - k, 1e-4);
	const float m2 = k + (1.0 - k) * t / (1.0 + t * (1.0 / max(r, 1e-4) - 1.0));
	return c * (m2 / m);
}

// The fix for one pixel: c = the picture, wRaw / wGrade / wAnchor = the hot
// spot, graded spot and anchor maps there, gAnchor = the anchor gain there.
// Fix shape Anchor: inside large bright areas (a window, a lit wall) the
// per-pixel squeeze is replaced by a smooth gain (worked out per cell from the
// area's blurred brightness): 1 at the area's peak, so the projector's laser
// dimming still sees it, gently lower just below it, so every pixel keeps its
// ratio to its neighbours and the detail (curtain folds) stays as it is.
// Inside a graded spot patch the normal squeeze applies.
// Hot spot mask Pixel: how much of the light is around this pixel. The
// pixels near it that reach the hot level (smooth from kLo to kHi, by the
// strongest channel) are averaged over two rings, so the mask follows the
// light's own outline and fades out softly over about SpotRad around it: a
// face or a wall next to the light keeps its level.
// Up to Hot spot 0.60 (peak white down to about 57%) the hot level is 60-85%.
// A deeper cut would leave the light's core darker than the glow just outside
// that level (a grey centre with a bright rim), so from 0.60 to 0.65 the level
// slides down to Highlight start .. the cut's peak: everything the cut can
// reach in the light and its glow is lowered together and stays in order.
float LightMask(float2 uv)
{
	const float deep = saturate((0.5672 - SpotScale) / (0.5672 - 0.5415));
	const float kLo = lerp(0.60, Knee, deep);
	const float kHi = lerp(0.85, max(SpotScale, Knee + 0.04), deep);
	float tw, th;
	tex.GetDimensions(tw, th);
	const float2 r = float2(SpotRad * th / max(tw, 1.0), SpotRad);
	float sum = smoothstep(kLo, kHi, Max3(tex.SampleLevel(samp, uv, 0).rgb)) * 2.0;
	float n = 2.0;
	[unroll]
	for (int i = 0; i < 8; i++) {
		const float a = 0.785398 * i;
		const float2 d = float2(cos(a), sin(a));
		sum += smoothstep(kLo, kHi, Max3(tex.SampleLevel(samp, uv + d * r * 0.5, 0).rgb)) * 1.5;
		sum += smoothstep(kLo, kHi, Max3(tex.SampleLevel(samp, uv + float2(cos(a + 0.392699), sin(a + 0.392699)) * r, 0).rgb));
		n += 2.5;
	}
	return saturate(2.0 * sum / n);
}

// Projector Titan Noir Max (2.46.0): after a dark scene the laser comes back
// up over about 2 s, and until it does every level above what it can show is
// crushed to one flat level; and lights too small for DBLE to count are
// crushed the same way at any time (2.47.0). Highlights between RampKnee and
// the frame's peak are squeezed under RampCeil so they keep their detail.
// While the laser climbs, the pixels at the level DBLE goes by (RampKeep)
// keep it, so the projector still brings its laser up as it would.
float3 Ramp(float3 c)
{
	if (RampCeil >= 0.999)
		return c;
	const float m = Max3(c);
	const float P = RampPeak;
	const float k = RampKnee;
	if (P <= RampCeil + 0.005 || m <= k)
		return c;
	const float t = (m - k) / max(P - k, 1e-4);
	const float r = (RampCeil - k) / max(P - k, 1e-4);
	float m2 = k + (P - k) * t / (1.0 + t * (1.0 / max(r, 1e-4) - 1.0));
	m2 = lerp(m2, m, smoothstep(RampKeep - 0.06, RampKeep - 0.03, m));
	return c * (m2 / m);
}

float3 FixGuard(float3 c, float wRaw, float wGrade, float wAnchor, float gAnchor, float2 uv)
{
	const float wg = (GradeOn > 0.5) ? saturate(wGrade) : 0.0;
	float3 o = Rolloff(c, Ceil, Knee);
	if (Anchor > 0.001)
		o = lerp(o, c * gAnchor, saturate(wAnchor) * (1.0 - wg));
	if (GradeOn > 0.5) {
		// Graded spots (a lit cheek, a shirt shoulder): only the highlights in
		// the patch get a stronger roll-off (peak white down to GradeCeil);
		// everything below Highlight start is untouched, so no dark patch.
		// Judged by the strongest channel (not Color protect): skin is reddish,
		// its red channel is what clips while its brightness sits low.
		const float w = saturate(wGrade);
		if (w > 0.001)
			o = lerp(o, RolloffM(c, Max3(c), min(Ceil, GradeCeil), Knee), w);
	}
	if (SpotOn > 0.5) {
		// Hot spots: inside a soft patch over each small, very bright spot only
		// the highlights are lowered: everything up to Highlight start is left
		// as it is (a face, a body or a wall next to the light keeps its level),
		// and what is above it is scaled down so peak white lands where the
		// exposure cut puts it (SpotScale). The light keeps its shape and the
		// core stays the brightest point; the patch fades out to the normal fix.
		float w = wRaw * SpotFade;
		if (w > 0.001 && SpotPixel > 0.5)
			w *= LightMask(uv);
		if (w > 0.001) {
			const float m = Max3(c);
			const float k = Knee;
			if (m > k) {
				const float g = clamp((SpotScale - k) / max(1.0 - k, 1e-4), 0.15, 1.0);
				const float m2 = k + (m - k) * g;
				o = lerp(o, c * (m2 / m), saturate(w));
			}
		}
	}
	return o;
}

float3 Fix(float3 c, float wRaw, float wGrade, float wAnchor, float gAnchor, float2 uv)
{
	return Ramp(FixGuard(c, wRaw, wGrade, wAnchor, gAnchor, uv));
}

float4 main(PS_INPUT input) : SV_Target
{
	const float4 c = tex.Sample(samp, input.Tex);
	if (Bypass > 0.5)
		return c;

	// Picture position, 0..1 across the picture.
	const float2 mu = (input.Tex - SrcMap.xy) / SrcMap.zw;

	if (View > 2.5) {
		// Side by side: the whole picture twice at half size, without the fix on
		// the left and with it on the right, black above and below.
		const float2 px1 = max(float2(abs(ddx(input.Tex.x)), abs(ddy(input.Tex.y))), 1e-7);
		const float dx = abs(mu.x - 0.5) * SrcMap.z / px1.x;
		if (dx < 1.5)
			return float4(0.3, 0.3, 0.3, c.a);
		const bool right = mu.x >= 0.5;
		const float2 q = float2((mu.x - (right ? 0.5 : 0.0)) * 2.0, (mu.y - 0.25) * 2.0);
		if (q.y < 0.0 || q.y > 1.0)
			return float4(0.0, 0.0, 0.0, c.a);
		const float4 s = tex.SampleLevel(samp, SrcMap.xy + q * SrcMap.zw, 0);
		if (!right)
			return s;
		const float4 sm = spotMask.SampleLevel(sampMask, q, 0);
		return float4(Fix(s.rgb, sm.r, sm.g, sm.b, sm.a, SrcMap.xy + q * SrcMap.zw), s.a);
	}

	// Hot spot map and pixel size, read here (outside any branch) for the
	// test view.
	const float4 wm = spotMask.SampleLevel(sampMask, mu, 0);
	const float wRaw = wm.r;
	const float wEdge = max(fwidth(wRaw), 1e-4);
	const float2 px = max(float2(abs(ddx(input.Tex.x)), abs(ddy(input.Tex.y))), 1e-7);

	const float3 o = Fix(c.rgb, wRaw, wm.g, wm.b, wm.a, input.Tex);

	if (View < 0.5)
		return float4(o, c.a);

	if (View < 1.5) {
		// Only pixels the fix actually lowered. (A black pixel would otherwise
		// read as 0/0 and look "lowered a lot".) Changes under 3% are not marked,
		// so a light touch doesn't paint the whole frame.
		// Yellow = lowered 3-10%, red = 10-25%, blue = 25% or more.
		const float before = Max3(c.rgb);
		const float ratio = (before > 0.001) ? Max3(o) / before : 1.0;
		float3 res = o;
		if (ratio < 0.97) {
			const float3 tint = (ratio > 0.90) ? float3(1.0, 1.0, 0.0)
				: ((ratio > 0.75) ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.3, 1.0));
			res = Tint(o, tint, 0.8);
		}
		// Hot spot patches: a solid white ring where each patch begins and a
		// white dot at the centre of each spot.
		// A large light that is eased gets a dash-dot ring.
		if (SpotOn > 0.5 && SpotFade > 0.02) {
			float nd = 1e9;
			float ne = 1.0;
			[unroll]
			for (int i = 0; i < 16; i++) {
				if ((float)i < SpotCount) {
					const float4 sc = SpotCenter[i / 2];
					const float2 ct = SrcMap.xy + ((i % 2 == 0) ? sc.xy : sc.zw) * SrcMap.zw;
					const float d = length((input.Tex - ct) / px);
					if (d < 6.0)
						res = float3(1.0, 1.0, 1.0);
					if (d < nd) {
						nd = d;
						ne = SpotEase[i / 4][i % 4];
					}
				}
			}
			if (abs(wRaw - 0.05) < 1.5 * wEdge) {
				const float pat = fmod(input.Pos.x + input.Pos.y, 18.0);
				if (ne > 0.99 || pat < 10.0 || (pat >= 13.0 && pat < 15.0))
					res = float3(1.0, 1.0, 1.0);
			}
		}
		return float4(res, c.a);
	}

	// Split: left half without the fix, right half with it.
	const float u = (input.Tex.x - SrcMap.x) / SrcMap.z;
	const float dx = abs(input.Tex.x - (SrcMap.x + 0.5 * SrcMap.z)) * TexW;
	if (dx < 1.5)
		return float4(0.3, 0.3, 0.3, c.a);
	return float4(u < 0.5 ? c.rgb : o, c.a);
}
)HLSL";

// HLSL cbuffer mirrors (16-byte aligned)
struct ClipGuardMeasureCB
{
	float cellU, cellV, pad0, pad1;
	float srcOffX, srcOffY, srcScX, srcScY;
	float pad2, pad3, knee, pad4;
};
struct ClipGuardMainCB
{
	float ceil, knee, bypass, view;
	float srcOffX, srcOffY, srcScX, srcScY;
	float pad0, pad1, pad2, texW;
	float colorProtect, spotScale, spotFade, spotOn;
	float spotCenter[8][4];
	float spotCount, anchor, gradeCeil, gradeOn;
	float spotEase[4][4];
	float spotPixel, spotRad, rampCeil, rampKnee;
	float rampPeak, rampKeep, rampPad1, rampPad2;
};
static_assert(sizeof(ClipGuardMeasureCB) == 48 && sizeof(ClipGuardMainCB) == 304, "DBLE cbuffer size");

class CClipGuard
{
public:
	// Measuring grid: Normal 128x72 cells, Fine 256x144.
	static constexpr UINT  kMaxCellsX = 256;
	static constexpr UINT  kMaxCellsY = 144;
	static constexpr UINT  kStaging = 3;
	static constexpr DWORD kOverlayHideMs = 8000;
	static constexpr int   kRows = 22;   // 2.63.1: the Test-only Source row is gone
	static constexpr int   kRowUpscaler = 15;
	// Menu rows shown: ClipGuard (not +) leaves out Upscaler.
	int Rows() const { return kRows - (m_s.plus ? 0 : 1); }
	// The setting behind a shown row.
	int RowOf(int shown) const
	{
		shown = std::clamp(shown, 0, Rows() - 1);
		if (!m_s.plus && shown >= kRowUpscaler) shown++;
		return shown;
	}
	// Dim fire (2.40.0). Each cell also measures how warm its bright half is:
	// (red - blue) / red, 0 for white or bluish, about 0.33 for a flame (red
	// 1.5 x blue). A light is judged by its GLOW only (2.40.1): the cells
	// around its core that are bright enough to carry a colour. A flame's core
	// is its palest part (0.20 where its glow is clearly orange), and so is
	// any light's. With Dim fire off, a light whose glow is at kWarmFull or
	// warmer is not a hot spot (a flame, a candle, a sodium lamp); between
	// kWarmStart and kWarmFull its dimming is eased. A light with fewer than
	// kGlowMinCells glow cells has no glow: it is not fire. The main Guard
	// still applies to fire.
	// 2.40.2: the cut-off sits between a glowing figure (Captain Marvel's glow
	// reads 0.18-0.29 and must stay a hot spot) and fire (0.38-0.46 on the
	// paused Avengers frames, never under 0.30 as the warmest of a frame).
	static constexpr float kWarmStart = 0.29f;
	static constexpr float kWarmFull = 0.33f;
	// 2.42.5: with Dim fire off a very small fire (a candle flame, a match) is
	// still a hot spot: full up to kFireSmallStart of the picture (the light's
	// core), none from kFireSmallFull, so a flame near the limit doesn't snap.
	static constexpr float kFireSmallStart = 0.0025f;
	static constexpr float kFireSmallFull = 0.0050f;
	static constexpr float kWarmLog = 0.20f;   // readout: lights at least this warm count as "warm"
	static constexpr int   kGlowMinCells = 2;
	// Hot spots: compact bright areas (each fits in kHotBoxW x kHotBoxH of the
	// picture) peaking at kHotMinPeak or more, each core judged against its own
	// peak (within kHotMargin). Every light that passes the lamp and flat-core
	// checks is dimmed, up to kMaxSpots. More than kSpreadLights lights, or
	// cores over kHotMaxFrac of the picture, read "spread" (neon lines, signs)
	// and nothing is dimmed locally.
	static constexpr float kHotMinPeak = 0.80f;
	static constexpr float kHotMargin = 0.05f;
	static constexpr float kHotMaxFrac = 0.02f;
	static constexpr float kHotBoxW = 0.15f;
	static constexpr float kHotBoxH = 0.20f;
	static constexpr int   kMaxSpots = 16;       // lights dimmed per frame
	static constexpr int   kSpreadLights = 16;   // more lights than this: scattered (neon, strings), none dimmed
	static constexpr int   kMaxCands = 512;  // compact bright areas checked per frame (brightest first)
	// Large lights (a lamp shade, a big lamp up close) are not hot spots: a light
	// whose core covers more than kLargeStart of the picture gets less hot spot
	// dimming, none from kLargeFull; the main fix handles it.
	// Headlights, flashlights and streetlights are far smaller (0.36% at most
	// in the logs), lamp shades 0.6-1.1%.
	static constexpr float kLargeStart = 0.004f;
	static constexpr float kLargeFull = 0.008f;
	// A light whose core is flat white in the source (a clipped disc, such as a
	// spotlight) is eased by how flat its core is (kClipFlatStart..Full) times
	// how big it is (kClipSizeStart..Full). Since 2.38.3 a large, very flat
	// disc gets no hot spot dimming at all: a lamp of 0.26-0.58% of the picture
	// with a 72-82% flat core was pulled down hard the moment it was found
	// mid-shot. The lights tuned on are 0.20% or smaller; the larger ones that
	// are dimmed (streetlights 0.26%, a fire 0.31-0.37%) have cores at most
	// 64% flat and are not affected.
	static constexpr float kClipFlatStart = 0.65f;
	static constexpr float kClipFlatFull = 0.70f;
	static constexpr float kClipSizeStart = 0.0020f;
	static constexpr float kClipSizeFull = 0.0025f;
	// How fast a light's easing follows (per frame; about a second to settle),
	// so a light that becomes flat or large fades smoothly.
	static constexpr float kEaseRate = 0.05f;
	// A spot smaller than this share of the patch it lands in is a piece of
	// that light (a speck inside a big disc): it keeps the patch alive but
	// doesn't change its easing, size or position.
	static constexpr float kHotPiece = 0.5f;
	// A graded light that only just reaches the hot spot level (a highlight on a
	// face when the frame peaks around 76-81%, 7-10% flat) gets no hot spot
	// dimming: by its peak, none at kPeakStart and full from kPeakFull; by its
	// flat core, full from kPeakFlatFull (headlamps, streetlights) whatever its
	// peak. The larger of the two counts. Follows fast toward more dimming (a
	// light brightening) and slowly toward less.
	static constexpr float kPeakStart = 0.81f;
	static constexpr float kPeakFull = 0.84f;
	static constexpr float kPeakFlatStart = 0.15f;
	static constexpr float kPeakFlatFull = 0.20f;
	static constexpr float kPeakRateUp = 0.25f;
	// A hot spot must sit against dark surroundings (a headlamp, a flashlight).
	// The share of a ring around it (out to about twice its size) above
	// Highlight start ramps its dimming: full up to kHotRingFull, none from
	// kHotLitRing (a lamp lighting the room: the main fix handles it). A ramp, so a candle whose glow moves around the
	// limit gets a steady partial amount instead of switching on and off.
	static constexpr float kHotRingFull = 0.20f;
	static constexpr float kHotLitRing = 0.35f;
	// Readout "near": of the lit ring cells, the share within kHotNearBand of
	// the light's own peak (needs kHotNearMinCells lit cells).
	static constexpr float kHotNearBand = 0.15f;
	static constexpr int   kHotNearMinCells = 3;
	// Hot spot is for a bright light in a dark scene only: full in pictures up
	// to kHotBelowFull x "Hot spot below", fading to none at the setting.
	static constexpr float kHotBelowFull = 0.667f;
	// A hot spot needs a flat core: at least this share of its core samples
	// within 0.01 of the cell's brightest (bulbs, headlamps, headlights are flat;
	// faces and skin are graded and are left to the main fix).
	static constexpr float kHotFlatMin = 0.07f;
	// Hot spot dimming ramps in with the core's flatness, from kHotFlatMin (none)
	// to kHotFlatFull (full), so a graded light that only now and then reads
	// just over kHotFlatMin (a shaded lamp lighting a table) can't pump.
	static constexpr float kHotFlatFull = 0.09f;
	// A very bright light counts as flat whatever its core: a soft glow (a
	// glowing figure, an engine) peaking this high is a light. It is treated as
	// if its core were flat, ramping from kBrightStart (none) to kBrightFull.
	static constexpr float kBrightStart = 0.86f;
	static constexpr float kBrightFull = 0.90f;
	// Hot spot below fades over about a second (per frame), so a scene sitting
	// at the limit doesn't pump the patches in and out.
	static constexpr float kGateRate = 0.04f;
	// 2.42.4: once the picture is above the limit the patches go in about 5
	// frames. A headlight that bloomed (picture 3% -> 11%) was no longer found
	// as a hot spot; its small patch stayed where the light had last been and,
	// with the slow fade, sat as a dark blotch in the glare for a second.
	static constexpr float kGateFast = 0.35f;
	// Hysteresis: a hot spot already there stays at full strength while a bright
	// light is still in its place, even on frames where that light narrowly
	// fails a check (it reads graded or as a lamp). Only after kReleaseFrames
	// such frames in a row (about a second) is it let go and faded out.
	static constexpr int   kReleaseFrames = 30;
	// Moving lights: each hot spot keeps its speed and direction, looks for its
	// light where it should be next (the search reaching further the faster it
	// moves), and a held patch moves with its light. kMaxSpeed caps the speed
	// (cells per frame); kSpeedRate is how fast it follows.
	static constexpr float kMaxSpeed = 12.0f;
	// A held patch follows its light, and a lost one coasts, only when the hot
	// spot was already moving at least this fast (Fine cells per frame); a still
	// light's patch stays put, so it can't hop along a strip of lights (mirror
	// frames) from piece to piece.
	static constexpr float kMovingSpeed = 1.0f;
	// A patch that had two or more lights within its reach is treated as a
	// group of lights for this many frames.
	static constexpr int   kGroupFrames = 48;
	// 2.42.1: a patch only becomes a group (and may then stay put) once its
	// light has been found on kGroupSeen frames and it is not moving. A fast
	// headlight with flare reflections around it looked like a tight group on
	// its second frame, so its patch parked where the light had been and
	// dimmed empty picture for 10 frames while the light raced on.
	static constexpr int   kGroupSeen = 6;
	static constexpr float kRefindPeak = 0.08f;   // 2.42.3: how much dimmer a light may be to pick up a patch lost while moving
	static constexpr float kSpeedRate = 0.5f;
	// Strip lights (a mirror's light frame, neon tubes) are not hot spots. A
	// light is a strip piece when its core is thin and long (at most
	// kStripThin Fine cells across and at least kStripLong times as long), or
	// when the light it belongs to, grouped down to kStripLevel, is larger than
	// the compact limits (a strip whose brightness dips stays one light there).
	static constexpr float kStripThin = 2.0f;
	static constexpr float kStripLong = 3.0f;
	static constexpr float kStripLevel = 0.60f;
	// Graded spots: small bright graded highlights (a lit cheek, a shirt
	// shoulder; candidates that fail the flat-core check) up to kGradeMaxFrac
	// of the picture each, in the same dark scenes as Hot spot. Their patch map
	// follows slowly (kGradeRate per frame, about a second) both ways, so a face
	// moving into light is dimmed gradually, never switched on. Inside, only
	// the highlights get a stronger roll-off: Graded spot 1.00 = peak white at 50%.
	static constexpr float kGradeMaxFrac = 0.01f;
	static constexpr float kGradeRate = 0.05f;
	// Graded spots also count from Highlight start + kGradeAboveKnee (0.60 at
	// the default 0.40), below the hot spot level: a lit cheek can clip on the
	// projector at 69%.
	static constexpr float kGradeAboveKnee = 0.20f;
	// Fix shape Anchor: the anchor level is the peak reached by the top
	// kAnchorPct of picture cells (so one glint can't set it); only bright
	// areas of at least kAnchorMinFrac of the picture (touching cells at the
	// anchor level - kAnchorGroup or more) are held. The map follows at
	// kAnchorRate per frame (at once after a cut).
	static constexpr float kAnchorPct = 0.003f;
	static constexpr float kAnchorGroup = 0.06f;
	static constexpr float kAnchorMinFrac = 0.004f;
	static constexpr float kAnchorRate = 0.2f;
	// Anchor gain: blur radius in cells (Normal grid) and the depth of the dip
	// at full fix: at most kAnchorDip x a quarter of the range from Highlight
	// start to the anchor level, half way between them.
	static constexpr int   kAnchorBlur = 3;
	static constexpr float kAnchorDip = 1.0f;
	// Exposure inside a spot at Hot spot 1.00, in stops (3 = one eighth of the light).
	static constexpr float kSpotStops = 3.0f;
	// Hot spot mask Pixel: feather radius around the light, share of the picture height.
	static constexpr float kMaskFeather = 0.006f;
	static constexpr int   kMaxTracks = kMaxSpots;
	// Spot tracking: a spot found inside an existing patch moves that patch
	// (eased) instead of starting a new one; overlapping patches merge. A spot
	// not found keeps its patch for kHoldFrames measurements, then fades out
	// (at once after a scene cut).
	static constexpr int   kHoldFrames = 15;
	// A bright opening that reads as a strip (a tall window, a doorway) can pass
	// as one or two small lights for a frame or two while something crosses
	// it, or on the first frame of a shot; the strip hold then kept that patch
	// on the whole opening for two to three seconds. Two rules stop that:
	// a new hot spot can't start where a strip was seen in the last
	// kStripMemFrames frames (cleared at a scene cut), and a strip (since 2.38.6
	// a graded or lamp reading too) only holds a patch whose light was seen for
	// kProbation frames; a younger patch that loses its light for more than a
	// frame is dropped quickly.
	// With look-ahead, a new mid-scene light must be found on the next kConfirm
	// frames as well before its patch reaches the screen: the shown picture is
	// Lead() frames behind the measurement, so a light gone again within that
	// time (a window pane passing the hot spot level for two frames as the
	// shot brightens) is taken out of the frames not yet shown and is never
	// dimmed. Look-ahead 4 confirms with its 2 frames and the patch then
	// starts at full on the light's first frame; Look-ahead 6 confirms with 2
	// and fades the patch in on the 2 frames before, as Look-ahead 4 used to.
	static constexpr int   kConfirm = 2;
	static constexpr int   kStripMemFrames = 24;
	static constexpr int   kProbation = 6;
	static constexpr float kYoungFadeOut = 0.5f;
	static constexpr int   kMaxStripMem = 64;
	// Scene cut: picture brightness changes by this factor or more.
	static constexpr float kCutRatio = 4.0f;
	// While a light is tracked, its dimming follows the scene's darkness this
	// slowly (per frame; about 2 seconds to settle at 24 fps).
	static constexpr float kLockRate = 0.02f;
	// A new spot mid-scene starts at kFadeStart of its dimming and reaches the full
	// amount in a few frames. At a scene cut it is full at
	// once. With look-ahead beyond 2 frames it is confirmed first (kConfirm) and
	// is then full on the light's first frame, faded in on the frames before
	// where the look-ahead leaves room (Look-ahead 6).
	static constexpr float kFadeStart = 0.5f;
	static constexpr float kFadeStepHot = 0.25f;   // 0.50, 0.75, 1.00
	// 2.41.2: a light first found touching the edge of the picture is sliding
	// into the frame (a moon in a pan). Its patch starts at kFadeEdge and
	// reaches full in three more frames, at every Look-ahead setting, so the
	// light doesn't step darker while it is still entering.
	static constexpr float kFadeEdge = 0.25f;
	static constexpr float kTrackFadeOut = 0.85f;  // per frame once a lost spot's hold ends
	static constexpr int   kMaxLead = 10;          // look-ahead 12 = 10 frames of lead
	// Look-ahead 8, 10 and 12 (2.38.9) only held the picture back longer, to
	// try out that much audio delay: a new light is confirmed and faded in
	// exactly as at Look-ahead 6 (kConfirm frames, then at most kMaxPre).
	static constexpr int   kMaxPre = 2;
	// High Performance (2.39.0): Look-ahead 12, 10 frames of lead. It uses the
	// frames it can see ahead of the shown picture:
	//  - a new light is judged over its first kConfirmHP frames (it may be
	//    missed on kPendMissHP of them) and starts with the weights it has by
	//    then, so one that turns into a lamp or a large light is not dimmed;
	//  - a patch whose light is not found for Lead() frames ends on the
	//    light's last frame (2.39.1: no fade in before a new light and none
	//    out after a lost one, so a patch never sits on something else);
	//  - the Guard level is the average of the frames from Lead() before to
	//    Lead() after the shown one, within the scene (2.39.1; no lag);
	//  - the graded spot map is centred on the shown frame: kAheadWeight
	//    from the average of the frames ahead, the rest the trailing value;
	//  - a tracked light that reads as a lamp eases its patch out (and back
	//    in when it reads as a light); one that reads as a lamp or as part
	//    of a strip for Lead() frames in a row has become one, and its patch
	//    ends where that began with EndFade steps (2.39.1);
	//  - a patch's strength follows its light down without the usual lag:
	//    kAheadWeight from the light's weights on the frames ahead (2.39.1),
	//    and up as well (2.40.3: a glow that hardens into a light took a
	//    second to reach full dimming); the ring (lamp) weight stays slow on
	//    the way up, so a flame's glow coming and going can't pulse its patch.
	// Off to 6 work exactly as before.
	static constexpr int   kHPFrames = 12;
	static constexpr int   kConfirmHP = 8;
	static constexpr int   kPendMissHP = 2;
	// 2.42.0: Look-ahead 6 (4 frames of lead) uses the same method, scaled to
	// what it can see. On the test reel at Look-ahead 4, 78% of patches were
	// found on fewer than 4 frames (82% on fewer than 8), and 83% of the gaps
	// a light came back from were 4 frames or shorter (95% at 10). So at 6:
	// a new light is judged over kConfirmLite frames (it may be missed on
	// kPendMissLite), a lost one is held for Lead() = 4 frames, and the Guard,
	// graded map and patch strength take kAheadWeight x 4/10 from the frames
	// ahead (the Guard as a blend with its trailing value: 9 frames are too
	// few to average on their own). A lamp or strip reading still has to
	// last kLampRun frames; the fade-out then starts on the first frame not
	// yet shown. Off, 2 and 4 are unchanged.
	static constexpr int   kHPLiteFrames = 6;
	static constexpr int   kConfirmLite = 4;
	static constexpr int   kPendMissLite = 1;
	static constexpr int   kLampRun = 10;
	static constexpr int   kEndSteps = 3;
	static float EndFade(int i) { return i == 1 ? 0.60f : (i == 2 ? 0.30f : 0.10f); }
	static constexpr float kAheadWeight = 0.62f;
	static constexpr float kGradeRateHP = 0.10f;
	static constexpr float kLampGone = 0.02f;
	static constexpr int   kTgHist = 64;

	~CClipGuard() { ReleaseDevice(); CloseShared(); }

	// ---- device ----------------------------------------------------------------

	HRESULT InitDevice(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, const std::wstring& moduleDir)
	{
		ReleaseDevice();
		m_pDevice = pDevice;
		m_pContext = pContext;
		m_iniPath = moduleDir + L"\\clipguard.ini";

		HRESULT hr = CreatePS(kClipGuardMeasureHLSL, sizeof(kClipGuardMeasureHLSL) - 1, &m_pPSMeasure);
		if (SUCCEEDED(hr)) hr = CreatePS(kClipGuardMainHLSL, sizeof(kClipGuardMainHLSL) - 1, &m_pPSMain);
		if (SUCCEEDED(hr)) {
			D3D11_BUFFER_DESC bd = { sizeof(ClipGuardMeasureCB), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
			hr = pDevice->CreateBuffer(&bd, nullptr, &m_pCBMeasure);
			if (SUCCEEDED(hr)) {
				bd.ByteWidth = sizeof(ClipGuardMainCB);
				hr = pDevice->CreateBuffer(&bd, nullptr, &m_pCBMain);
			}
		}
		for (int g = 0; g < 2 && SUCCEEDED(hr); g++) {
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = 128u << g;
			td.Height = 72u << g;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;  // 32-bit: A packs two counts
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_RENDER_TARGET;
			hr = pDevice->CreateTexture2D(&td, nullptr, &m_pMeasureTex[g]);
			if (SUCCEEDED(hr)) hr = pDevice->CreateTexture2D(&td, nullptr, &m_pMeasureOut[g]);
			td.Usage = D3D11_USAGE_STAGING;
			td.BindFlags = 0;
			td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			for (UINT i = 0; SUCCEEDED(hr) && i < kStaging; i++) {
				hr = pDevice->CreateTexture2D(&td, nullptr, &m_pStaging[g][i]);
				if (SUCCEEDED(hr)) hr = pDevice->CreateTexture2D(&td, nullptr, &m_pStagingOut[g][i]);
			}
		}
		for (int g = 0; g < 2 && SUCCEEDED(hr); g++) {
			D3D11_TEXTURE2D_DESC md = {};
			md.Width = 128u << g;
			md.Height = 72u << g;
			md.MipLevels = 1;
			md.ArraySize = 1;
			md.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;  // r = hot spots, g = graded spots, b = anchor areas, a = anchor gain
			md.SampleDesc.Count = 1;
			md.Usage = D3D11_USAGE_DEFAULT;
			md.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			hr = pDevice->CreateTexture2D(&md, nullptr, &m_pMaskTex[g]);
			if (SUCCEEDED(hr)) hr = pDevice->CreateShaderResourceView(m_pMaskTex[g], nullptr, &m_pMaskSRV[g]);
			if (SUCCEEDED(hr) && g == 1) {
				D3D11_SAMPLER_DESC sd = {};
				sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				sd.MaxLOD = D3D11_FLOAT32_MAX;
				hr = pDevice->CreateSamplerState(&sd, &m_pMaskSampler);
			}
			std::fill(std::begin(m_mask), std::end(m_mask), 0.0f);
			std::fill(std::begin(m_grade), std::end(m_grade), 0.0f);
			std::fill(std::begin(m_anchorMap), std::end(m_anchorMap), 0.0f);
			std::fill(std::begin(m_anchorGain), std::end(m_anchorGain), 1.0f);
		}
		if (SUCCEEDED(hr)) {
			hr = m_Font.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontS.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontT.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontB.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontH.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontL.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontG.InitDeviceObjects(pDevice, pContext);
			m_fontH = 0;
		}
		if (FAILED(hr)) {
			ReleaseDevice();
		}
		m_frame = 0;
		m_smooth = -1.0f;
		m_hotSmooth = 0.0f;
		for (auto& t : m_tracks) t = Track();
		m_tracksLive = 0;
		m_spotDrawn = 0;
		m_haveMeasure = false;
		m_snapCount = 0;
		m_nNew = 0;
		m_outFrame = 0;
		m_haveAfter = false;
		AreaClear();
		return hr;
	}

	void ReleaseDevice()
	{
		m_pPSMeasure.Release();
		m_pPSMain.Release();
		m_pCBMeasure.Release();
		m_pCBMain.Release();
		for (int g = 0; g < 2; g++) {
			m_pMeasureTex[g].Release();
			for (auto& s : m_pStaging[g]) s.Release();
			m_pMeasureOut[g].Release();
			for (auto& s : m_pStagingOut[g]) s.Release();
			m_pMaskSRV[g].Release();
			m_pMaskTex[g].Release();
		}
		m_pMaskSampler.Release();
		for (int i = 0; i < kQuerySets; i++) {
			m_qDisjoint[i].Release(); m_qT0[i].Release(); m_qT1[i].Release();
			m_qPending[i] = false;
		}
		m_qOpen = false;
		m_Font.InvalidateDeviceObjects();
		m_FontS.InvalidateDeviceObjects();
		m_FontT.InvalidateDeviceObjects();
		m_FontB.InvalidateDeviceObjects();
		m_FontH.InvalidateDeviceObjects();
		m_FontL.InvalidateDeviceObjects();
		m_FontG.InvalidateDeviceObjects();
		m_fontH = 0;
		m_pDevice = nullptr;
		m_pContext = nullptr;
	}

	bool Ready() const { return m_pPSMain && m_pPSMeasure && m_pCBMain && m_pCBMeasure && m_pMeasureTex[1] && m_pStaging[1][kStaging - 1] && m_pMaskSRV[1] && m_pMaskSampler; }
	UINT CellsX() const { return m_cx; }
	UINT CellsY() const { return m_cy; }
	int LookAheadFrames() const { return m_s.enabled ? m_s.lookAhead : 0; }
	// Frames the shown picture is behind the newest measurement (look-ahead beyond 2).
	int Lead() const { return std::max(0, LookAheadFrames() - 2); }
	// Of those frames, how many confirm a new mid-scene light before its patch
	// is shown (the rest fade the patch in early). 0 without look-ahead.
	bool HP() const { return LookAheadFrames() >= kHPLiteFrames; }      // the look-ahead method: 6 and High Performance
	bool HPFull() const { return LookAheadFrames() >= kHPFrames; }     // High Performance itself
	int Confirm() const { return HPFull() ? kConfirmHP : (HP() ? kConfirmLite : std::min(Lead(), kConfirm)); }

	ID3D11PixelShader* PSMeasure() const { return m_pPSMeasure; }
	ID3D11PixelShader* PSMain() const { return m_pPSMain; }
	ID3D11Buffer* CBMeasure() const { return m_pCBMeasure; }
	ID3D11Buffer* CBMain() const { return m_pCBMain; }
	ID3D11Texture2D* MeasureTex() const { return m_pMeasureTex[m_grid]; }
	ID3D11Texture2D* MeasureOutTex() const { return m_pMeasureOut[m_grid]; }
	CD3D11Font& Font() { return m_Font; }

	// ---- per frame ---------------------------------------------------------------

	// Settings, the latest finished measurement, and the fix amount for this frame.
	void BeginFrame(bool outputPQ, float sceneNits)
	{
		CpuFrame();
		CpuStart();
		PollSettings();
		if (m_s.grid != m_grid) {
			// Grid changed: start measuring at the new size; patches start fresh.
			m_grid = std::clamp(m_s.grid, 0, 1);
			m_cx = 128u << m_grid;
			m_cy = 72u << m_grid;
			m_frame = 0;
			for (auto& t : m_tracks) t = Track();
			m_tracksLive = m_spotDrawn = 0;
			std::fill(std::begin(m_mask), std::end(m_mask), 0.0f);
			std::fill(std::begin(m_grade), std::end(m_grade), 0.0f);
			std::fill(std::begin(m_anchorMap), std::end(m_anchorMap), 0.0f);
			std::fill(std::begin(m_anchorGain), std::end(m_anchorGain), 1.0f);
			m_outFrame = 0;
			m_haveAfter = false;
			AreaClear();
		}
		m_outputPQ = outputPQ;
		m_sceneNits = sceneNits;

		if (!outputPQ && m_frame >= kStaging - 1) {
			ReadMeasurement(m_pStaging[m_grid][(m_frame + 1) % kStaging], LookAheadFrames() > 0);
		}
		// The picture as sent (after the fix) is measured only while the overlay shows.
		if (outputPQ || !OverlayVisible()) {
			m_outFrame = 0;
			m_haveAfter = false;
		} else if (m_outFrame >= kStaging - 1) {
			ReadAfter(m_pStagingOut[m_grid][(m_outFrame + 1) % kStaging]);
		}

		float target = 0.0f;
		float dark = 0.0f; // 0 above "Fix starts", 1 at or below "Full effect"
		if (m_s.enabled && !outputPQ && m_haveMeasure) {
			// None above "Fix starts", full at or below "Full effect", smooth in between.
			const float start = std::max(m_s.triggerPct, 0.2f);
			const float full = std::clamp(m_s.fullPct, 0.0f, start - 0.1f);
			const float t = std::clamp((start - m_picture) / (start - full), 0.0f, 1.0f);
			dark = t * t * (3.0f - 2.0f * t);
			target = std::clamp(m_s.strength, 0.0f, 1.0f) * dark;
		}
		// Text skip: a frame whose highlights are mostly bright-on-black with no
		// glow (titles, credits, stars) is left alone. Eases out and back in.
		m_textSkipped = m_haveMeasure && m_s.textSkipPct > 0.0f && m_brightSamples >= 16.0f
			&& m_textPct > m_s.textSkipPct;
		if (m_textSkipped) {
			target = 0.0f;
			dark = 0.0f;
		}
		// Hot spots: small, very bright spots in a dark frame (a headlamp, a
		// flashlight, headlights) get a darker exposure in a soft patch. The patch
		// comes in fast and goes out slowly.
		// Hot spot below: hot spots only in dark pictures (a bright light in a dark
		// scene); medium and bright scenes are left to the main fix.
		{
			const float lim = m_s.hotBelow;
			float g = 0.0f;
			if (lim > 0.0f && m_haveMeasure) {
				const float full = kHotBelowFull * lim;
				const float u = std::clamp((lim - m_picture) / std::max(lim - full, 0.01f), 0.0f, 1.0f);
				g = u * u * (3.0f - 2.0f * u);
			}
			m_gateShut = lim > 0.0f && m_haveMeasure && m_picture > lim;
			m_hotGate += (m_gateShut ? kGateFast : kGateRate) * (g - m_hotGate);
			if (std::fabs(g - m_hotGate) < 0.001f) m_hotGate = g;
		}
		const bool hot = dark > 0.0f && m_s.hotSpot > 0.0f && m_hotGate > 0.0f && m_tracksLive >= 1;
		const float hotTarget = hot ? 1.0f : 0.0f; // the patch fades in and out
		{
			const float a = (hotTarget > m_hotSmooth) ? 1.0f : 0.10f; // on at once (patches fade in), out slowly
			m_hotSmooth = a * hotTarget + (1.0f - a) * m_hotSmooth;
		}
		const bool cut = m_cut;
		m_cutFrame = cut;
		m_targetRaw = target;
		m_smooth = (m_smooth < 0.0f || cut) ? target : 0.10f * target + 0.90f * m_smooth;
		m_cut = false;
		const float kneeUser = std::clamp(m_s.knee, 0.20f, 0.95f);
		m_ceil = 1.0f - 0.5f * m_smooth;
		m_kneeEff = std::min(kneeUser, m_ceil - 0.05f);
		// Graded spots: peak white inside their patches, by Graded spot, the
		// scene's darkness and Hot spot below (dark scenes only); follows slowly.
		{
			const float want = 1.0f - 0.5f * std::clamp(m_s.gradedSpot, 0.0f, 1.0f) * dark * m_hotGate;
			m_gradeCeil = cut ? want : m_gradeCeil + kGradeRate * (want - m_gradeCeil);
		}
		// Inside a hot spot: exposure lowered by Hot spot x kSpotStops stops (scaled
		// by how dark the frame is), applied to the signal (gamma 2.2). Set at once
		// when a spot appears or the scene cuts; while a light is tracked it follows
		// only slowly, so someone walking past doesn't make the light pulse.
		if (hot) {
			const float stops = kSpotStops * std::clamp(m_s.hotSpot, 0.0f, 1.0f) * dark * m_hotGate;
			const float want = std::pow(2.0f, -stops / 2.2f);
			m_spotScale = (m_hotSmooth < 0.05f || m_spotFresh || cut || m_s.hotSpot != m_lastHotSet) ? want : (m_gateShut ? kGateFast : kLockRate) * want + (1.0f - (m_gateShut ? kGateFast : kLockRate)) * m_spotScale;
		}
		m_spotFresh = !hot;
		m_lastHotSet = m_s.hotSpot;   // a changed setting applies at once
		m_hotActive = m_hotSmooth > 0.02f && m_spotScale < 0.999f;

		UpdateRamp();
		PushSnap(hot);
		WriteStatus();
		LogFrame();
		CpuStop();
	}

	// ---- Projector: Titan Noir Max (DBLE High Measured), 2.46.0 / 2.47.0 -----------
	// Measured with ClipGuard Meter on 2026-11-17, firmware V1.3.38, DBLE High:
	// - DBLE goes by the bright AREA: lights adding up to less than about 0.05%
	//   of the screen are ignored (0.03% ignored, 0.1% counted, many small
	//   lights add up; position and colour make no difference).
	// - With nothing counted the laser drops to about 23% of full, and every
	//   level above what that can show (signal about 0.505, level 128) is one
	//   flat level (diffuser clip test: 160-255 all read the same).
	// - When something bright enough appears the laser climbs back over about
	//   2 s (transitions test). 2.51.0: about 1.3 s from 23% (histogram test
	//   flares, 2026-11-18: 32% in the first 0.3 s, 55% by 0.5 s, 85-98% by 1 s,
	//   full by 1.3 s); the threshold (0.05%, whatever the background) and the
	//   level curve were confirmed by the same tests.
	// So the laser DBLE aims for is the level of the brightest 0.05% of the
	// picture (m_dbleP), but never below 23%; the squeeze ceiling is the signal
	// the modelled laser can show (gamma 2.15, measured). Films: 24 frames a second.
	// 2.60.0: Titan Noir Max DBLE Low (Predictive), measured 2026-11-19 (firmware V1.3.38,
	// meter at the screen, against DBLE Off): the same 0.05% bright area and the same
	// small-highlight clip as High (laser about 23.5% with nothing counted), but the
	// laser stays up much more with mid-level content: after a full mid-grey screen it
	// was at about 47% where High was at about 27% (fall test), so its target follows
	// the DBLE level with exponent 1.08 instead of 2.15. A tiny pure white at 255 is
	// not clipped at all on Low (High: about 88%). Rise: about 2.3 s to 90% from 25%.
	// Fall (both modes, fall test): the laser comes down steadily, reaching its new
	// level about 2.3 s after a bright scene cuts to a dark one (it was modelled as
	// 0.8 s), whether it starts from full or from mid level.
	static constexpr float kRampDt = 1.0f / 24.0f;
	static constexpr float kRampFallDur = 2.3f;  // 2.60.0: s for the laser to come down to its new level (measured, both modes)
	static constexpr float kRampGamma = 2.15f;   // the projector's measured gamma
	static constexpr float kRampKnee = 0.75f;    // the squeeze starts at this share of its ceiling
	static constexpr float kLaserFloor = 0.23f;  // the laser with nothing counted (measured)
	static constexpr float kDbleArea = 0.0005f;  // bright area DBLE counts (measured: between 0.03% and 0.1%; 0.05% on every background, 2.51.0)
	static constexpr float kRampRise = 1.3f;     // 2.51.0: s for the laser to climb from 25% to full (was 2.0)
	bool  LowMode() const { return m_s.projector == 3; }   // 2.60.0: Titan Noir Max DBLE Low (Predictive)
	float RampTarget(float p) const             // the laser DBLE settles at for this brightest-0.05% level
	{
		return std::clamp(std::pow(std::max(p, 0.0f), LowMode() ? 1.08f : kRampGamma), LowMode() ? 0.235f : kLaserFloor, 1.0f);
	}
	float RampRise() const { return LowMode() ? 2.6f : kRampRise; }   // Low: 90% at about 2.3 s
	float DbleWhiteP() const { return LowMode() ? 1.0f : kDbleWhiteP; }
	static float RampShape(float u)              // laser rise from its start (0) to full (1), u = time / rise time
	{
		static const float tu[] = { 0.0f, 0.12f, 0.375f, 0.625f, 0.875f, 1.0f };
		static const float ts[] = { 0.0f, 0.0f, 0.27f, 0.59f, 0.95f, 1.0f };
		if (u <= 0.0f) return 0.0f;
		for (int i = 1; i < 6; i++)
			if (u <= tu[i]) return ts[i - 1] + (ts[i] - ts[i - 1]) * (u - tu[i - 1]) / (tu[i] - tu[i - 1]);
		return 1.0f;
	}
	// 2.49.0: a tiny PURE WHITE light at full level is counted after all (diffuser
	// clip test on 2026-11-18: a 0.03% white light at 255 read 88% of Off, at 224
	// it was clipped; red and orange at 255 were not counted). So near-white
	// (not warm) cells at 250 or more adding up to kDbleWhiteArea lift the DBLE
	// level to kDbleWhiteP (laser about 88%).
	static constexpr float kDbleWhiteArea = 0.0002f;
	static constexpr float kDbleWhiteP = 0.94f;
	static constexpr float kDbleWhiteMin = 250.0f / 255.0f;
	static constexpr float kDbleWhiteWarm = 0.1f;

	float m_rampL = 1.0f, m_rampL0 = 1.0f, m_rampT = 0.0f, m_rampDur = 0.0f;
	bool  m_rampRising = false;
	bool  m_rampFalling = false;   // 2.60.0: the fall in progress, and the laser it started from
	float m_rampFallL0 = 1.0f;
	float m_rampCeil = 1.0f, m_rampKnee = 1.0f, m_rampKeep = 2.0f;
	float m_dbleP = 1.0f;   // the level of the brightest 0.05% of the picture (what DBLE goes by)
	float m_dblePs = 1.0f;  // 2.47.1: the same, smoothed: wobbles under kDbleHold are ignored, real changes followed over about half a second
	static constexpr float kDbleHold = 0.03f;
	static constexpr float kDbleRate = 0.15f;   // per frame
	ULONGLONG m_rampTick = 0;   // 2.48.1: when the model last saw a new frame
	void UpdateRamp()
	{
		m_rampTick = GetTickCount64();
		if (!MeasuredMode() || !m_haveMeasure || m_outputPQ) {
			m_rampL = 1.0f; m_rampRising = false; m_rampFalling = false; m_dblePs = 1.0f;
			m_rampCeil = m_rampKnee = 1.0f;
			m_rampKeep = 2.0f;
			return;
		}
		// 2.47.2: a scene cut jumps straight to the new shot's level; smoothing is only for wobbles within a shot.
		if (m_cutFrame) m_dblePs = m_dbleP;
		else if (std::fabs(m_dbleP - m_dblePs) > kDbleHold) m_dblePs += kDbleRate * (m_dbleP - m_dblePs);
		const float tgt = RampTarget(m_dblePs);
		if (tgt < m_rampL - 0.001f) {
			m_rampRising = false;
			// 2.60.0: a steady fall that reaches the new level in kRampFallDur (measured).
			if (!m_rampFalling) { m_rampFalling = true; m_rampFallL0 = m_rampL; }
			const float rate = std::max(0.02f, (m_rampFallL0 - tgt) / kRampFallDur);
			m_rampL = std::max(tgt, m_rampL - rate * kRampDt);
		} else if (tgt > m_rampL + 0.001f) {
			m_rampFalling = false;
			if (!m_rampRising) {
				m_rampRising = true;
				m_rampL0 = m_rampL;
				m_rampT = 0.0f;
				const float a = (1.0f - m_rampL0) / 0.75f;
				m_rampDur = std::max(0.1f, RampRise() * a * a);   // 2.51.0: 1.3 s from 25% (was 2 s), measured; 2.60.0: Low 2.6 s
			}
			m_rampT += kRampDt;
			m_rampL = std::min(tgt, m_rampL0 + (1.0f - m_rampL0) * RampShape(m_rampT / m_rampDur));
			if (m_rampL >= tgt - 0.001f) m_rampRising = false;
		} else {
			m_rampRising = false;
			m_rampFalling = false;
		}
		const bool on = m_s.enabled && m_rampL < 0.995f;
		m_rampCeil = on ? std::pow(m_rampL, 1.0f / kRampGamma) : 1.0f;
		m_rampKnee = on ? kRampKnee * m_rampCeil : 1.0f;
		m_rampKeep = (on && m_rampRising) ? m_dblePs : 2.0f;
	}
	bool TitanMode() const { return m_s.projector == 1 || m_s.projector == 3; }
	// 2.49.0: a measured projector model replaces the Guard and Hot spot rules (2.51.2: the Titan only; the Valerion model was removed).
	bool MeasuredMode() const { return m_s.projector == 1 || m_s.projector == 3; }
	// 2.60.0: the projector row's names (3: Titan Noir Max DBLE Low; 2 was the Valerion, retired).
	const wchar_t* ProjectorName() const
	{
		return m_s.projector == 1 ? L"Titan Noir Max DBLE High (Predictive)"
			: m_s.projector == 3 ? L"Titan Noir Max DBLE Low (Predictive)" : L"Universal (Reactive)";
	}
	// 2.48.1: no new video frames for half a second (paused, menu): the prediction is not current.
	bool RampStale() const { return m_rampTick == 0 || GetTickCount64() - m_rampTick > 500; }

	// ---- 2.48.0: calibration LUT (2.50.0: chosen in its own LUT row) ------------------
	// titan.cube / valerion.cube next to clipguard.ini (made by ClipGuard Meter's
	// auto-cal), with a one-line description in titan.txt / valerion.txt. Checked
	// about every 2 s so a new auto-cal is picked up while playing. Universal, or
	// no file: no LUT from ClipGuard (one ClipGuard loaded earlier is taken off;
	// a LUT Unplugged loaded itself is left alone).
	int  m_lutCheck = 0;
	std::wstring m_lutPath;            // what ClipGuard has loaded ("" = none)
	FILETIME m_lutTime = {};
	bool m_lutOurs = false;            // ClipGuard loaded the current LUT
	int  m_lutState = 0;               // 0 = none, 1 = loaded, 2 = failed
	std::wstring m_lutDesc;
	bool m_lutOffWas = false;
	std::wstring LutBase() const
	{
		if (m_s.lutOff) return L"";   // F5: off for comparing, treated like no LUT
		const size_t sl = m_iniPath.find_last_of(L"\\/");
		const std::wstring dir = sl == std::wstring::npos ? L"." : m_iniPath.substr(0, sl);
		if (m_s.lut == 1) return dir + L"\\titan";
		if (m_s.lut == 2) return dir + L"\\valerion";
		return L"";
	}
	// True when the renderer should load (path) or clear (empty path) a LUT now.
	bool LutWanted(std::wstring& path)
	{
		if (m_s.lutOff != m_lutOffWas) { m_lutOffWas = m_s.lutOff; m_lutCheck = 0; }   // F5 acts at once
		if (++m_lutCheck < 48 && m_lutCheck > 1) return false;
		m_lutCheck = 2;
		const std::wstring base = LutBase();
		std::wstring want;
		FILETIME ft = {};
		if (!base.empty()) {
			WIN32_FILE_ATTRIBUTE_DATA fa = {};
			if (GetFileAttributesExW((base + L".cube").c_str(), GetFileExInfoStandard, &fa)) { want = base + L".cube"; ft = fa.ftLastWriteTime; }
		}
		if (want.empty()) {
			m_lutState = 0;
			m_lutDesc.clear();
			if (m_lutOurs) { m_lutOurs = false; m_lutPath.clear(); path.clear(); return true; }
			return false;
		}
		if (want == m_lutPath && CompareFileTime(&ft, &m_lutTime) == 0) return false;
		m_lutPath = want;
		m_lutTime = ft;
		m_lutDesc.clear();
		FILE* f = nullptr;
		if (_wfopen_s(&f, (base + L".txt").c_str(), L"rt, ccs=UTF-8") == 0 && f) {
			wchar_t line[200] = {};
			if (fgetws(line, 200, f)) { m_lutDesc = line; while (!m_lutDesc.empty() && (m_lutDesc.back() == L'\n' || m_lutDesc.back() == L'\r')) m_lutDesc.pop_back(); }
			fclose(f);
		}
		path = want;
		return true;
	}
	void LutForget() { m_lutPath.clear(); m_lutCheck = 0; }
	int Projector() const { return m_s.projector; }
	int Lut() const { return m_s.lut; }
	void LutLoaded(bool ok, bool cleared) { m_lutOurs = !cleared; m_lutState = cleared ? 0 : (ok ? 1 : 2); if (!ok) m_lutPath.clear(); }
	// Banner: 0 = none, 1 = loaded (desc), 2 = failed.
	int LutState() const { return m_s.lutOff ? 3 : m_lutState; }   // 3 = off with F5
	const std::wstring& LutDesc() const { return m_lutDesc; }
	// The shown frame: the level DBLE goes by and the level highlights clip at (0-255; 255 = no squeeze).
	float DbleLevel() const { return ShownSnap().dbleP * 255.0f; }
	float ClipLevel() const { const float c = ShownSnap().rampCeil; return c < 0.999f ? c * 255.0f : 255.0f; }
	// The shown frame's ramp: the laser the squeeze follows, % of full (100 = no squeeze).
	float RampPct() const { const float c = ShownSnap().rampCeil; return c < 0.999f ? std::pow(std::max(c, 0.0f), kRampGamma) * 100.0f : 100.0f; }

	// ---- look-ahead state ------------------------------------------------------------
	// The state worked out from each measured frame is queued, and the main pass
	// uses the one that belongs to the frame being shown. With look-ahead beyond
	// 2 frames the queue also lets a new spot's patch be faded in on the frames
	// before the light appears.

	struct Track { float cx = 0, cy = 0, r = 0; int miss = 0; float alpha = 0; float fade = 1.0f; float ease = 1.0f; float pkw = 1.0f; float flw = 1.0f; float rgw = 1.0f; int fail = 0; float vx = 0, vy = 0; float cov = 0; int grp = 0; int seen = 0; int pend = 0; int age = 0; float rE = 1.0f, rP = 1.0f, rF = 1.0f, rR = 1.0f; int lamp = 0; int id = 0; int lm = 0; float lpk = 1.0f; };   // lm: lost while moving (2.42.2)  // vx, vy: cells per frame; cov, grp: a group of close lights under one patch

	struct Snap
	{
		float ceil = 1.0f, knee = 0.3f, spotScale = 1.0f, hotSmooth = 0.0f, peak = 1.0f;
		float rampCeil = 1.0f, rampKnee = 1.0f, rampPeak = 1.0f, rampKeep = 2.0f, dbleP = 1.0f;   // Projector Titan Noir Max: the ramp squeeze for this frame
		float spotCenter[kMaxSpots][2] = {};
		float spotEase[kMaxSpots] = {};
		int   spotDrawn = 0;
		int   grid = 0;
		Track tracks[2 * kMaxTracks]; // second half: patches faded in early
		// Graded spot and Anchor maps of this frame, so they follow the
		// look-ahead like everything else.
		float gradeCeil = 1.0f;
		float grade[kMaxCellsX * kMaxCellsY] = {};
		float anchorMap[kMaxCellsX * kMaxCellsY] = {};
		float anchorGain[kMaxCellsX * kMaxCellsY] = {};   // anchorMap 0 = no Anchor, so a 0 gain is never used
		// For High Performance: the frame's own Guard amount (before and after
		// smoothing), whether it starts a scene, and its graded spot target.
		float target = 0.0f, smooth = 0.0f;
		bool  cut = false;
		float gradeTarget[kMaxCellsX * kMaxCellsY] = {};
	};

	void PushSnap(bool hot)
	{
		const int lead = Lead();
		if (lead != m_snapLead || m_grid != m_snapGrid) {
			m_snapLead = lead;
			m_snapGrid = m_grid;
			m_snapCount = 0;
		}
		if (m_snapCount == 0) m_tgCount = 0;
		m_tgHead = (m_tgHead + 1) % kTgHist;
		m_tgVal[m_tgHead] = m_targetRaw;
		m_tgCut[m_tgHead] = m_cutFrame;
		m_tgCount = std::min(m_tgCount + 1, kTgHist);
		m_snapHead = (m_snapHead + 1) % (kMaxLead + 1);
		Snap& sn = m_snap[m_snapHead];
		sn.ceil = m_ceil; sn.knee = m_kneeEff; sn.peak = m_anchorLv;
		sn.spotScale = m_spotScale; sn.hotSmooth = m_hotSmooth;
		memcpy(sn.spotCenter, m_spotCenter, sizeof(sn.spotCenter));
		memcpy(sn.spotEase, m_spotEase, sizeof(sn.spotEase));
		sn.spotDrawn = m_spotDrawn;
		sn.grid = m_grid;
		sn.gradeCeil = m_gradeCeil;
		sn.rampCeil = m_rampCeil; sn.rampKnee = m_rampKnee; sn.rampPeak = 1.0f;   // 2.47.1: a fixed curve up to full white, so a level always comes out the same (the frame's peak made lights flicker)
		// 2.49.0: these two had ended up inside the comment above since 2.47.1, so the
		// keep was never used and the banner's DBLE level always read 255. The level is
		// shown again; the keep stays off, as in every build tested since 2.47.1.
		sn.rampKeep = 2.0f;
		sn.dbleP = m_dblePs;
		sn.target = m_targetRaw; sn.smooth = std::max(m_smooth, 0.0f); sn.cut = m_cutFrame;
		{
			const size_t nc = (size_t)m_cx * m_cy;
			memcpy(sn.grade, m_grade, nc * sizeof(float));
			memcpy(sn.gradeTarget, m_gradeTarget, nc * sizeof(float));
			memcpy(sn.anchorMap, m_anchorMap, nc * sizeof(float));
			memcpy(sn.anchorGain, m_anchorGain, nc * sizeof(float));
		}
		for (int k = 0; k < kMaxTracks; k++) {
			sn.tracks[k] = m_tracks[k];
			sn.tracks[kMaxTracks + k] = Track();
		}
		m_snapCount = std::min(m_snapCount + 1, kMaxLead + 1);
		// Spots just confirmed (found on the kConfirm frames after their first):
		// fade their patch in on the queued frames before their first one.
		const int conf = std::min(lead, Confirm());
		const int pre = HP() ? 0 : std::min(lead - conf, kMaxPre);   // High Performance: starts on the light's first frame
		for (int i = 0; i < m_nNew; i++) {
			if (!hot || conf >= m_snapCount) continue;
			// The patch as it was on the light's first frame.
			Track t = m_snap[(m_snapHead + kMaxLead + 1 - conf) % (kMaxLead + 1)].tracks[m_newSlots[i]];
			if (t.alpha <= 0.0f) t = m_tracks[m_newSlots[i]];
			t.pend = 0;
			for (int k = 1; k <= pre && conf + k < m_snapCount; k++) {
				Snap& e = m_snap[(m_snapHead + kMaxLead + 1 - conf - k) % (kMaxLead + 1)];
				for (int j = kMaxTracks; j < 2 * kMaxTracks; j++) {
					if (e.tracks[j].alpha > 0.0f) continue;
					e.tracks[j] = t;
					e.tracks[j].fade = t.fade * (1.0f - (float)k / (float)(pre + 1));   // t.fade < 1: a light entering at the edge
					break;
				}
				if (e.hotSmooth < 0.05f) { e.hotSmooth = 1.0f; e.spotScale = m_spotScale; }
			}
		}
		m_nNew = 0;
	}

	const Snap& ShownSnap() const
	{
		const int back = std::min(m_snapLead, m_snapCount - 1);
		return m_snap[(m_snapHead + kMaxLead + 1 - std::max(back, 0)) % (kMaxLead + 1)];
	}

	// High Performance: the queued frames after the shown one that belong to
	// the same scene (up to the next cut). 0 in every other mode.
	int AheadFrames() const
	{
		if (!HP()) return 0;
		const int back = std::max(std::min(m_snapLead, m_snapCount - 1), 0);
		int n = 0;
		for (int f = 1; f <= back; f++) {
			if (AheadSnap(f).cut) break;
			n++;
		}
		return n;
	}
	const Snap& AheadSnap(int f) const   // f frames after the shown one
	{
		const int back = std::max(std::min(m_snapLead, m_snapCount - 1), 0);
		return m_snap[(m_snapHead + kMaxLead + 1 - std::max(back - f, 0)) % (kMaxLead + 1)];
	}
	// The Guard amount for the shown frame. High Performance: centred on that
	// frame instead of trailing it.
	float ShownSmooth() const
	{
		const Snap& sn = ShownSnap();
		if (!HP() || m_tgCount <= 0) return sn.smooth;
		if (!HPFull()) {
			// Look-ahead 6: the trailing value, drawn toward the frames ahead.
			const int na = AheadFrames();
			if (na <= 0) return sn.smooth;
			float ahead = 0.0f;
			for (int f = 1; f <= na; f++) ahead += AheadSnap(f).target;
			const float w = kAheadWeight * (float)na / (float)kMaxLead;
			return (1.0f - w) * sn.smooth + w * ahead / (float)na;
		}
		// The frames from Lead() before to Lead() after the shown one, as far
		// as they belong to its scene.
		const int back = std::max(std::min(m_snapLead, m_snapCount - 1), 0);
		auto at = [&](int off) { return (m_tgHead + 2 * kTgHist - back + off) % kTgHist; };   // off frames after the shown one
		float sum = m_tgVal[at(0)];
		int n = 1;
		for (int f = 1; f <= back; f++) {
			if (m_tgCut[at(f)]) break;
			sum += m_tgVal[at(f)]; n++;
		}
		const int past = std::min(m_snapLead, m_tgCount - 1 - back);
		for (int p = 1; p <= past; p++) {
			if (m_tgCut[at(1 - p)]) break;   // the frame after it starts a scene
			sum += m_tgVal[at(-p)]; n++;
		}
		return sum / (float)n;
	}
	// How strongly a patch of the shown frame is drawn. High Performance:
	// its strength follows the light down without the trailing lag, from the
	// light's own weights on the frames ahead (same patch, same scene).
	float ShownWeight(const Snap& sn, int k) const
	{
		const Track& t = sn.tracks[k];
		const float trail = t.ease * t.pkw * t.flw * t.rgw;
		float wt = trail;
		if (k < kMaxTracks && &sn == &ShownSnap()) {
			const int na = AheadFrames();
			float sum = 0.0f, sumUp = 0.0f;
			int n = 0;
			for (int f = 1; f <= na; f++) {
				const Track& e = AheadSnap(f).tracks[k];
				if (e.alpha <= 0.0f || e.id != t.id) break;
				const float up = std::min(e.rE, t.ease) * std::max(e.rP, t.pkw) * e.rF;   // the peak weight never pulls a patch down (2.41.1)
				sumUp += up;
				sum += up * e.rR;
				n++;
			}
			if (n > 0) {
				const float w = kAheadWeight * (float)n / (float)kMaxLead;
				const float m = sum / (float)n;
				const float mUp = sumUp / (float)n * t.rgw;   // up: peak and flat weights ahead, ring weight as it trails
				if (m < trail) wt = trail - w * (trail - m);
				else if (mUp > trail) wt = trail + w * (mUp - trail);
			}
		}
		return t.alpha * t.fade * wt;
	}

	// Spot maps (r = hot, g = graded). Hot: from a state's patches, 1 over each
	// spot, fading to 0 over its glow and a soft edge (only the cells near each
	// patch are visited). Graded: the slowly following graded spot map.
	void BuildMask(const Snap& sn)
	{
		const int cx = (int)m_cx, cy = (int)m_cy;
		for (size_t i = 0; i < (size_t)cx * cy; i++) {
			m_mask[i * 4] = 0.0f;
			m_mask[i * 4 + 1] = sn.grade[i];
			m_mask[i * 4 + 2] = sn.anchorMap[i];
			m_mask[i * 4 + 3] = sn.anchorGain[i];
		}
		// High Performance: the graded map is centred on the shown frame too.
		const int na = AheadFrames();
		if (na > 0) {
			const size_t nc = (size_t)cx * cy;
			static float sum[kMaxCellsX * kMaxCellsY];
			std::fill(sum, sum + nc, 0.0f);
			for (int f = 1; f <= na; f++) {
				const float* g = AheadSnap(f).gradeTarget;
				for (size_t i = 0; i < nc; i++) sum[i] += g[i];
			}
			const float w = kAheadWeight * (float)na / (float)kMaxLead;
			const float k = w / (float)na;
			for (size_t i = 0; i < nc; i++) m_mask[i * 4 + 1] = (1.0f - w) * sn.grade[i] + k * sum[i];
		}
		{
			float gm = 0.0f;
			for (size_t i = 0; i < (size_t)cx * cy; i++) gm = std::max(gm, m_mask[i * 4 + 1]);
			m_shownGradeMax = gm;
		}
		for (int ti = 0; ti < 2 * kMaxTracks; ti++) {
			const Track& t = sn.tracks[ti];
			if (t.alpha <= 0.0f) continue;
			// A patch over a group of close lights is full out to the farthest of
			// them (cov); its soft edge is as wide as ever.
			const float rIn = std::max(t.r, t.cov);
			const float rOut = 2.0f * t.r + 4.0f * gsF() + (rIn - t.r);
			const float wt = ShownWeight(sn, ti);
			const int xa = std::max(0, (int)std::floor(t.cx - rOut)), xb = std::min(cx - 1, (int)std::ceil(t.cx + rOut));
			const int ya = std::max(0, (int)std::floor(t.cy - rOut)), yb = std::min(cy - 1, (int)std::ceil(t.cy + rOut));
			for (int y = ya; y <= yb; y++) {
				for (int x = xa; x <= xb; x++) {
					const float dx = (float)x - t.cx, dy = (float)y - t.cy;
					const float d = std::sqrt(dx * dx + dy * dy);
					float u = std::clamp((d - rIn) / (rOut - rIn), 0.0f, 1.0f);
					u = u * u * (3.0f - 2.0f * u);
					float& m = m_mask[((size_t)y * cx + x) * 4];
					m = std::max(m, (1.0f - u) * wt);
				}
			}
		}
	}

	// Queue this frame's measurement for reading two frames from now.
	void EndMeasure()
	{
		m_pContext->CopyResource(m_pStaging[m_grid][m_frame % kStaging], m_pMeasureTex[m_grid]);
		m_frame++;
	}

	// The same for the picture as sent (after the fix).
	void EndMeasureOut()
	{
		m_pContext->CopyResource(m_pStagingOut[m_grid][m_outFrame % kStaging], m_pMeasureOut[m_grid]);
		m_outFrame++;
	}

	void FillMeasureCB(float cellU, float cellV, float offX, float offY, float scX, float scY)
	{
		ClipGuardMeasureCB cb = { cellU, cellV, 0, 0, offX, offY, scX, scY, 0, 0,
			std::clamp(m_s.knee, 0.20f, 0.95f), 0 };
		m_pContext->UpdateSubresource(m_pCBMeasure, 0, nullptr, &cb, 0, 0);
	}

	void FillMainCB(bool bypass, float offX, float offY, float scX, float scY, float texW)
	{
		CpuStart();
		const Snap& sn = ShownSnap();
		ClipGuardMainCB cb = {};
		cb.ceil = sn.ceil;
		cb.knee = sn.knee;
		if (HP()) {
			cb.ceil = 1.0f - 0.5f * ShownSmooth();
			cb.knee = std::min(std::clamp(m_s.knee, 0.20f, 0.95f), cb.ceil - 0.05f);
		}
		cb.bypass = bypass ? 1.0f : 0.0f;
		cb.view = (float)std::clamp(m_s.view, 0, 3);
		cb.srcOffX = offX; cb.srcOffY = offY; cb.srcScX = scX; cb.srcScY = scY;
		cb.texW = texW;
		cb.colorProtect = std::clamp(m_s.colorProtect, 0.0f, 1.0f);
		cb.spotScale = sn.spotScale;
		cb.spotFade = std::clamp(sn.hotSmooth, 0.0f, 1.0f);
		cb.spotOn = (!bypass && m_s.enabled && sn.hotSmooth > 0.001f) ? 1.0f : 0.0f;
		for (int i = 0; i < kMaxSpots; i++) {
			cb.spotCenter[i / 2][(i % 2) * 2 + 0] = sn.spotCenter[i][0];
			cb.spotCenter[i / 2][(i % 2) * 2 + 1] = sn.spotCenter[i][1];
		}
		cb.spotCount = (float)sn.spotDrawn;
		cb.anchor = (m_s.fixShape == 1) ? std::clamp(sn.peak, 0.05f, 1.0f) : 0.0f;
		cb.gradeCeil = sn.gradeCeil;
		cb.gradeOn = (!bypass && m_s.enabled && sn.gradeCeil < 0.999f) ? 1.0f : 0.0f;
		for (int i = 0; i < kMaxSpots; i++) {
			cb.spotEase[i / 4][i % 4] = sn.spotEase[i];
		}
		cb.spotPixel = (m_s.spotMask == 1) ? 1.0f : 0.0f;
		cb.spotRad = kMaskFeather * scY;
		cb.rampCeil = bypass ? 1.0f : sn.rampCeil;
		cb.rampKnee = sn.rampKnee;
		cb.rampPeak = sn.rampPeak;
		cb.rampKeep = sn.rampKeep;
		if (MeasuredMode()) {
			// Titan Noir Max / Valerion Max: the measured model replaces the Guard and Hot spot rules.
			cb.ceil = 1.0f;
			cb.spotOn = 0.0f;
			cb.gradeOn = 0.0f;
			cb.anchor = 0.0f;
		}
		m_pContext->UpdateSubresource(m_pCBMain, 0, nullptr, &cb, 0, 0);
		BuildMask(sn);
		m_pContext->UpdateSubresource(m_pMaskTex[m_grid], 0, nullptr, m_mask, m_cx * 4 * sizeof(float), 0);
		m_pContext->PSSetShaderResources(2, 1, &m_pMaskSRV[m_grid].p);
		m_pContext->PSSetSamplers(2, 1, &m_pMaskSampler.p);
		CpuStop();
	}

	// After the main pass: take the spot map off its slot again.
	void UnbindMask()
	{
		ID3D11ShaderResourceView* nul = nullptr;
		m_pContext->PSSetShaderResources(2, 1, &nul);
	}

	// ---- cost readout ------------------------------------------------------------------
	// GPU: timestamps around ClipGuard's own passes (read back a few frames later).
	// CPU: time spent in BeginFrame and FillMainCB. Both averaged over a second,
	// with the worst frame, against the time between frames.

	void GpuBegin()
	{
		const int i = m_qIdx;
		if (!m_qDisjoint[i]) {
			D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			if (FAILED(m_pDevice->CreateQuery(&qd, &m_qDisjoint[i]))) return;
			qd.Query = D3D11_QUERY_TIMESTAMP;
			if (FAILED(m_pDevice->CreateQuery(&qd, &m_qT0[i])) || FAILED(m_pDevice->CreateQuery(&qd, &m_qT1[i]))) {
				m_qDisjoint[i].Release(); m_qT0[i].Release(); m_qT1[i].Release();
				return;
			}
		}
		if (m_qPending[i]) {
			// Reusing this set: collect what it measured 4 frames ago.
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
			UINT64 t0 = 0, t1 = 0;
			if (m_pContext->GetData(m_qDisjoint[i], &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
				&& m_pContext->GetData(m_qT0[i], &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
				&& m_pContext->GetData(m_qT1[i], &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
				&& !dj.Disjoint && dj.Frequency > 0 && t1 >= t0) {
				const double ms = (double)(t1 - t0) * 1000.0 / (double)dj.Frequency;
				m_gpuSum += ms; m_gpuN++; m_gpuMax = std::max(m_gpuMax, ms);
			}
			m_qPending[i] = false;
		}
		m_pContext->Begin(m_qDisjoint[i]);
		m_pContext->End(m_qT0[i]);
		m_qOpen = true;
	}

	void GpuEnd()
	{
		if (!m_qOpen) return;
		const int i = m_qIdx;
		m_pContext->End(m_qT1[i]);
		m_pContext->End(m_qDisjoint[i]);
		m_qPending[i] = true;
		m_qOpen = false;
		m_qIdx = (m_qIdx + 1) % kQuerySets;
	}

	// ---- overlay -------------------------------------------------------------------

	int Upscaler() const { return m_s.upscaler; }
	bool TemporalDither() const { return m_s.dither == 1; }

	bool OverlayVisible() const
	{
		if (!m_s.overlayOn) return false;
		if (m_s.pinned) return true;
		return (DWORD)(GetTickCount() - m_s.lastKeyTick) < kOverlayHideMs;
	}

	float BottomBarFrac() const { return m_bottomBarFrac; }
	// Top letterbox bar, fraction of the video height (0 when unknown).
	float TopBarFrac() const { return (m_haveMeasure && m_by0 <= m_by1) ? (float)m_by0 / (float)m_cy : 0.0f; }
	// The bars around the largest picture area seen so far, for placing the
	// overlay: in a very dark shot the picture's edge rows read as bar on some
	// frames, and the overlay must not move with that.
	float StableTopBarFrac() const { return (m_stickY0 <= m_stickY1) ? (float)m_stickY0 / (float)m_cy : 0.0f; }
	float StableBottomBarFrac() const { return (m_stickY0 <= m_stickY1) ? (float)((int)m_cy - 1 - m_stickY1) / (float)m_cy : 0.0f; }
	int View() const { return m_s.view; }
	// Split screen and Side by side change the picture the projector gets, so
	// its dimming reacts differently: a note is shown above the picture.
	bool ShowViewNote() const { return !m_outputPQ && (m_s.view == 2 || m_s.view == 3); }

	// Fonts follow the window: the main one about 1/96 of its height (the whole
	// overlay scales with it), a small one for hints and the cost line, a bold
	// small one for headings, a bold title and a large bold one for the info
	// banner's badges. Inter (the Unplugged menu's font) when it is installed,
	// else Segoe UI.
	static const wchar_t* FontFamily()
	{
		static int have = -1;   // -1 = not checked yet
		if (have < 0) {
			have = 0;
			CComPtr<IDWriteFactory> pFactory;
			if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&pFactory))) && pFactory) {
				CComPtr<IDWriteFontCollection> pFonts;
				if (SUCCEEDED(pFactory->GetSystemFontCollection(&pFonts, FALSE)) && pFonts) {
					UINT32 index = 0;
					BOOL exists = FALSE;
					if (SUCCEEDED(pFonts->FindFamilyName(L"Inter", &index, &exists)) && exists) have = 1;
				}
			}
		}
		return have == 1 ? L"Inter" : L"Segoe UI";
	}
	void EnsureFont(int windowH)
	{
		const UINT h = (UINT)std::max(14, windowH / 96);
		const wchar_t* family = FontFamily();
		if (h != m_fontH
			&& SUCCEEDED(m_Font.CreateFontBitmap(family, h, 0))
			&& SUCCEEDED(m_FontS.CreateFontBitmap(family, std::max(9u, h * 4 / 5), 0))
			&& SUCCEEDED(m_FontH.CreateFontBitmap(family, std::max(9u, h * 4 / 5), D3DFONT_BOLD))
			&& SUCCEEDED(m_FontT.CreateFontBitmap(family, h * 3 / 2, D3DFONT_BOLD))
			&& SUCCEEDED(m_FontB.CreateFontBitmap(family, h * 3, D3DFONT_BOLD))
			&& SUCCEEDED(m_FontL.CreateFontBitmap(family, h * 31 / 20, 0))
			&& SUCCEEDED(m_FontG.CreateFontBitmap(family, h * 13 / 10, D3DFONT_BOLD))) {
			m_fontH = h;
		}
	}
	UINT FontH() const { return m_fontH; }
	CD3D11Font& FontS() { return m_FontS; }
	CD3D11Font& FontT() { return m_FontT; }
	CD3D11Font& FontB() { return m_FontB; }
	CD3D11Font& FontHd() { return m_FontH; }
	CD3D11Font& FontL() { return m_FontL; }
	CD3D11Font& FontG() { return m_FontG; }

	// ---- info banner (F6) --------------------------------------------------------
	bool InfoVisible() const { return m_s.infoOn; }
	// The graphics card's load in % as Windows reports it (Task Manager's 3D
	// figure), read by ClipGuard.exe while the banner shows; -1 = not known.
	float GpuLoad() const { return (ExeAlive() && m_pShared->gpuLoad >= 1.0f) ? m_pShared->gpuLoad - 1.0f : -1.0f; }
	const wchar_t* VersionText() const { return L"0.9.0"; }
	bool Measured() const { return m_haveMeasure && !m_outputPQ; }
	bool FixOn() const { return m_s.enabled && !m_outputPQ; }
	float PicturePct() const { return m_picture; }
	float FixPct() const { return std::max(m_smooth, 0.0f) * 100.0f; }
	int SpotsDimmed() const { return m_hotActive ? m_spots : 0; }
	bool PixelMask() const { return m_s.spotMask == 1; }
	double GpuMs() const { return m_costGpuAvg; }
	double CpuMs() const { return m_costCpuAvg; }
	// What the info banner shows of the fix and the hot spots.
	struct InfoStats {
		float picture, peak, fix, white, strength, hotSpot, hotBelow, gate, gradeCeil;
		int lights, spots, lamps, strips, graded, lookAhead, grid, fixShape, fires;
		bool dimmed, spread, dimFire, kscape;
		int laSource;
	};
	InfoStats GetInfo() const
	{
		InfoStats i = {};
		i.picture = m_picture; i.peak = m_framePeak * 100.0f; i.fix = std::max(m_smooth, 0.0f) * 100.0f; i.white = m_ceil * 100.0f;
		i.strength = m_s.strength; i.hotSpot = m_s.hotSpot; i.hotBelow = m_s.hotBelow; i.gate = m_hotGate; i.gradeCeil = m_gradeCeil * 100.0f;
		i.lights = m_spreadLights; i.spots = m_spots; i.lamps = m_litSpots; i.strips = m_stripPieces; i.graded = m_gradedSpots;
		i.lookAhead = m_s.lookAhead; i.grid = m_s.grid; i.fixShape = m_s.fixShape;
		i.dimmed = m_hotActive; i.spread = m_spread;
		i.fires = m_fireSpots; i.dimFire = m_s.dimFire; i.kscape = m_s.kscape; i.laSource = m_s.laSource;
		return i;
	}
	// The picture inside the black bars as a share of the frame (the stable
	// area, see AreaPush); false until something has been measured.
	// A new file: forget the picture shape remembered from the last one, so
	// the menu and banner land inside the new picture at once (2.42.1).
	void AreaReset() { AreaClear(); }
	bool PictureArea(float& wFrac, float& hFrac) const
	{
		if (m_stickY0 > m_stickY1 || m_stickX0 > m_stickX1 || !m_cx || !m_cy) return false;
		wFrac = (float)(m_stickX1 - m_stickX0 + 1) / (float)m_cx;
		hFrac = (float)(m_stickY1 - m_stickY0 + 1) / (float)m_cy;
		return true;
	}

	// The overlay as data: rows (headings and settings) and readout lines.
	// style: 0 = normal, 1 = blue (values, what is active), 2 = dim, 3 = yellow (Off, Skipped)
	struct OvRow { bool heading; std::wstring name, value; bool selected; int style; };
	struct OvLine { std::wstring text; int style; };
	struct OvKey { std::wstring key, what; };
	struct OvModel { std::wstring title; std::wstring version; std::vector<OvRow> rows; std::vector<OvKey> keys; std::vector<OvLine> lines; std::wstring help; };   // help: what the selected setting does and how to set it

	OvModel Overlay() const
	{
		OvModel m;
		m.title = m_s.plus ? L"ClipGuard+" : L"ClipGuard";
		m.version = m_s.testBuild ? L"0.9.0 Test" : L"0.9.0 beta";
		const auto vals = Lines();
		static const wchar_t* names[kRows] = { L"Guard", L"Strength", L"Guard shape", L"Guard starts", L"Full effect", L"Highlight start", L"Color protect", L"Text skip", L"Hot spot", L"Hot spot below", L"Hot spot mask", L"Graded spot", L"Dim fire as hot spot", L"Grid", L"Look-ahead", L"Upscaler", L"Dither", L"Projector", L"LUT", L"View", L"Overlay", L"Restart & update" };
		int vis = 0;   // ClipGuard (not +) has no Upscaler row: menu rows are counted without it
		for (int i = 0; i < kRows; i++) {
			if (i == kRowUpscaler && !m_s.plus) continue;
			if (i == 0) m.rows.push_back({ true, L"GUARD", L"", false, 2 });
			if (i == 8) m.rows.push_back({ true, L"SPOTS", L"", false, 2 });
			if (i == 13) m.rows.push_back({ true, L"DISPLAY", L"", false, 2 });
			std::wstring v = vals[i];
			int st = (v == L"Off") ? 3 : (i == kRows - 1 ? 2 : 1);
			const bool sel = (vis++ == m_s.selected);
			m.rows.push_back({ false, (i == kRows - 1 && !m_s.testBuild) ? L"Restart" : names[i], v, sel, st });
		}
		// What the selected setting does, and how to set it (2.41.0).
		static const wchar_t* help[kRows] = {
			L"Turns the whole fix on or off. Same as F8. Leave on; turn off to compare.",
			L"How far highlights are pulled down in dark scenes. Start at 0.35. Raise it if highlights still clip, lower it if dark scenes look dull.",
			L"Anchor keeps the brightest parts and eases the areas around them; Normal lowers all highlights together. Anchor suits most films.",
			L"The Guard begins when the picture is darker than this. 10% is a good start. Higher makes it act in brighter scenes too.",
			L"The Guard is at full Strength when the picture is darker than this. 3% is a good start. Keep it below Guard starts.",
			L"Only parts brighter than this are touched. 0.40 is a good start. Lower if mid-bright areas clip, higher to leave more of the picture alone.",
			L"Higher leaves strongly coloured highlights alone. 0.90 is a good start. Lower it if bright reds or blues clip.",
			L"Leaves titles, credits and star fields alone. 25% works for most films. 0 turns it off.",
			L"Dims small, very bright lights in dark scenes, such as headlights. Start at 0.60. Raise it if lights still clip, lower it if they look dull. F10 turns it on and off.",
			L"Hot spot only works when the picture is darker than this. 3% is a good start. Raise it if lights clip in slightly brighter scenes.",
			L"Pixel dims only the light itself; Patch dims a soft area around it. Pixel suits most films. Try Patch if a dimmed light looks cut out.",
			L"Gently dims small soft highlights that aren't hot spots, such as a lit cheek. 0.50 is a good start. 0 turns it off.",
			L"No leaves fire and other orange or red lights alone, except very small flames such as a candle. Choose Yes if fire clips on your projector.",
			L"Fine finds small lights better, for a little more work. Use Fine unless frames are being skipped.",
			L"Holds the picture back so the fix lands on time. Add the same audio delay. 4 suits most setups. High Performance works best but needs half a second of delay and a capable graphics card.",
			L"FSRCNNX sharpens 1080p on a 4K screen with a small neural network; Normal uses the usual scaler. Try FSRCNNX; go back to Normal if frames are skipped or edges look harsh.",
			L"Static keeps the fine dither pattern still; Temporal moves it every frame so dark gradients look smoother. Compare on a dark, smooth scene; Static if you see a fine shimmer.",
			L"Universal (Reactive) works with any laser projector, reacting to what is on screen with the Guard and Hot spot settings. Titan Noir Max DBLE High / DBLE Low (Predictive) predict DBLE from meter measurements (firmware V1.3.38) and squeeze only the highlights it would clip; they replace the Guard and Hot spot settings, shown as Predictive. Set the projector's DBLE to the same mode. On a Valerion Max use Universal (Reactive).",
			L"The calibration LUT from ClipGuard Meter's auto-cal: Titan loads titan.cube, Valerion loads valerion.cube; None uses no LUT. F5 turns it off and on to compare.",
			L"Normal for watching. The other views show where the Guard is working, or compare it on and off.",
			L"Auto-hide closes this menu after a few seconds. Always keeps it up while you adjust.",
			L"Press Right to restart Unplugged and ClipGuard." };
		{
			const int sel = RowOf(m_s.selected);
			m.help = (sel == kRows - 1 && m_s.testBuild) ? L"Press Right to restart Unplugged and ClipGuard and update to the newest build." : help[sel];
		}
		m.keys = {
			{ L"Up / Down", L"select" }, { L"Left / Right", L"change" },
			{ L"F6", L"info banner" }, { L"F7", L"show / hide menu" }, { L"F8", L"guard on / off" },
			{ L"F9", L"guard areas view" }, { L"F10", L"hot spot on / off" },
			{ L"F11", m_s.testBuild ? L"send log" : L"save log" } };
		if (m_s.testBuild) m.keys.push_back({ L"Ctrl+Alt+U", L"restart & update" });
		m.lines = Readout();
		return m;
	}

	// The settings' values, as shown.
	std::vector<std::wstring> Lines() const
	{
		static const wchar_t* views[] = { L"Normal", L"Guard areas", L"Split screen", L"Side by side" };
		wchar_t v[kRows][48];
		swprintf_s(v[0], L"%ls", m_s.enabled ? L"On" : L"Off");
		swprintf_s(v[1], L"%.2f", m_s.strength);
		swprintf_s(v[2], L"%ls", m_s.fixShape == 1 ? L"Anchor" : L"Normal");
		swprintf_s(v[3], L"%.1f %%", m_s.triggerPct);
		swprintf_s(v[4], L"%.1f %%", m_s.fullPct);
		swprintf_s(v[5], L"%.2f", m_s.knee);
		swprintf_s(v[6], L"%.2f", m_s.colorProtect);
		if (m_s.textSkipPct > 0.0f) swprintf_s(v[7], L"%.0f %%", m_s.textSkipPct);
		else swprintf_s(v[7], L"Off");
		if (m_s.hotSpot > 0.0f) swprintf_s(v[8], L"%.2f", m_s.hotSpot);
		else swprintf_s(v[8], L"Off");
		if (m_s.hotBelow > 0.0f) swprintf_s(v[9], L"%.1f %%", m_s.hotBelow);
		else swprintf_s(v[9], L"Off");
		swprintf_s(v[10], L"%ls", m_s.spotMask == 1 ? L"Pixel" : L"Patch");
		if (m_s.gradedSpot > 0.0f) swprintf_s(v[11], L"%.2f", m_s.gradedSpot);
		else swprintf_s(v[11], L"Off");
		swprintf_s(v[12], L"%ls", m_s.dimFire ? L"Yes" : L"No");
		swprintf_s(v[13], L"%ls", m_s.grid ? L"Fine" : L"Normal");
		{
			// 2.68.0: the player says whose look-ahead this is (Media Player / Video Processor).
			const wchar_t* who = m_s.laSource == 1 ? L" (Kaleidescape)" : m_s.laSource == 2 ? L" (Video Processor)" : m_s.laSource == 3 ? L" (Media Player)" : L"";
			if (m_s.lookAhead >= kHPFrames) swprintf_s(v[14], L"High Performance%ls", who);
			else if (m_s.lookAhead > 0) swprintf_s(v[14], L"%d frames%ls", m_s.lookAhead, who);
			else swprintf_s(v[14], L"Off%ls", who);
		}
		swprintf_s(v[15], L"%ls", m_s.upscaler == 1 ? L"FSRCNNX" : L"Normal");
		swprintf_s(v[16], L"%ls", m_s.dither == 1 ? L"Temporal" : L"Static");
		swprintf_s(v[17], L"%ls", ProjectorName());
		swprintf_s(v[18], L"%ls", m_s.lut == 1 ? L"Titan" : (m_s.lut == 2 ? L"Valerion" : L"None"));
		if (MeasuredMode())
			for (int i = 1; i <= 12; i++) swprintf_s(v[i], L"Predictive");   // the measured model replaces these
		swprintf_s(v[19], L"%ls", views[std::clamp(m_s.view, 0, 3)]);
		swprintf_s(v[20], L"%ls", m_s.pinned ? L"Always" : L"Auto-hide");
		swprintf_s(v[21], L"Press Right");
		std::vector<std::wstring> out;
		for (int i = 0; i < kRows; i++) out.push_back(v[i]);
		return out;
	}

	// Readout lines (bottom of the overlay).
	std::vector<OvLine> Readout() const
	{
		std::vector<OvLine> out;
		wchar_t st[384];
		if (m_outputPQ) {
			out.push_back({ L"HDR output: the guard needs SDR output", 3 });
		} else if (!m_haveMeasure) {
			out.push_back({ L"Measuring...", 2 });
		} else {
			wchar_t text[32];
			if (m_brightSamples >= 16.0f) swprintf_s(text, L"%.0f%%", m_textPct);
			else swprintf_s(text, L"-");
			swprintf_s(st, L"Picture %.1f%%   Text %ls%ls   Guard %.0f%%   White %.0f%%   Peak %.0f%%",
				m_picture, text, m_textSkipped ? L" Skipped" : L"", m_smooth * 100.0f, m_ceil * 100.0f, m_framePeak * 100.0f);
			out.push_back({ st, 0 });
			wchar_t hotTxt[192] = L"Hot -";
			if (m_hotCells >= 1) {
				wchar_t what[32];
				if (m_spots == 0 && m_spread && m_spreadLights > kSpreadLights) swprintf_s(what, L" spread %d lights", m_spreadLights);
				else if (m_spots == 0 && m_spread && m_spreadArea) swprintf_s(what, L" spread area");
				else if (m_spots == 0) swprintf_s(what, m_gradedSpots > 0 ? L" graded" : (m_litSpots > 0 ? L" lamp" : L" spread"));
				else if (m_spreadLights > m_spots) swprintf_s(what, L" %d of %d lights", m_spots, m_spreadLights);
				else swprintf_s(what, L" %d spot%ls", m_spots, m_spots == 1 ? L"" : L"s");
				wchar_t flatTxt[16] = L"";
				if (m_hotFlat >= 0.0f) swprintf_s(flatTxt, L" flat %.0f%%", m_hotFlat * 100.0f);
				wchar_t ringTxt[16] = L"";
				if (m_hotRing >= 0.0f) swprintf_s(ringTxt, L" ring %.0f%%", m_hotRing * 100.0f);
				wchar_t nearTxt[16] = L"";
				if (m_hotNear >= 0.0f) swprintf_s(nearTxt, L" near %.0f%%", m_hotNear * 100.0f);
				wchar_t sizeTxt[40] = L"";
				if (m_largest > 0.0f) {
					if (m_largeSpots > 0) swprintf_s(sizeTxt, L" largest %.2f%% %d large", m_largest * 100.0f, m_largeSpots);
					else swprintf_s(sizeTxt, L" largest %.2f%%", m_largest * 100.0f);
				}
				wchar_t gateTxt[40] = L"";
				if (m_hotGate <= 0.001f) swprintf_s(gateTxt, L" off (scene above %.1f%%)", m_s.hotBelow);
				else if (m_hotGate < 0.999f) swprintf_s(gateTxt, L" eased %.0f%%", (1.0f - m_hotGate) * 100.0f);
				swprintf_s(hotTxt, L"Hot %.1f%%%ls%ls%ls%ls%ls%ls%ls", m_hotFrac * 100.0f, what, sizeTxt, flatTxt, ringTxt, nearTxt,
					gateTxt, m_hotActive ? L" dimmed" : L"");
			}
			out.push_back({ hotTxt, 1 });
			wchar_t gradeTxt[64] = L"Graded -";
			if (m_gradeMax > 0.02f && m_gradeCeil < 0.999f) swprintf_s(gradeTxt, L"Graded %d dimmed to %.0f%%", m_gradeLive, m_gradeCeil * 100.0f);
			wchar_t anchTxt[48] = L"";
			if (m_s.fixShape == 1) swprintf_s(anchTxt, L"   Anchor at %.0f%%", m_anchorLv * 100.0f);
			swprintf_s(st, L"%ls%ls", gradeTxt, anchTxt);
			out.push_back({ st, 1 });
		}
		if (m_haveAfter && m_haveMeasure && !m_outputPQ) {
			// Before = the frame being shown, as it came in (look-ahead shows an older frame).
			const int back = std::clamp(LookAheadFrames(), 0, kHist - 1);
			const int h = (m_histHead + kHist - back) % kHist;
			swprintf_s(st, L"ADL %.1f%% -> %.1f%%   Peak %.0f%% -> %.0f%%   (before -> after)",
				m_histPic[h], m_picAfter, m_histPeak[h] * 100.0f, m_peakAfter * 100.0f);
			out.push_back({ st, 0 });
		}
		{
			wchar_t gpu[48];
			if (m_costGpuAvg >= 0.0) swprintf_s(gpu, L"GPU %.2f ms (max %.2f)", m_costGpuAvg, m_costGpuMax);
			else swprintf_s(gpu, L"GPU -");
			swprintf_s(st, L"%ls   CPU %.2f ms (max %.2f)   Frame %.1f ms", gpu, m_costCpuAvg, m_costCpuMax, m_costFrame);
			out.push_back({ st, 2 });
		}
		{
			const std::wstring note = LogNote();
			if (!note.empty()) out.push_back({ note, 3 });
		}
		if (m_s.view == 1) {
			out.push_back({ L"Yellow = lowered 3-10%   Red = 10-25%   Blue = 25%+   White ring = hot spot", 2 });
		}
		return out;
	}

	// ---- log (F11) -------------------------------------------------------------------
	// ClipGuard.exe asks for a log through the shared memory; the renderer saves
	// the last 30 s of readout numbers (CSV) and a screenshot of the window
	// (BMP) to %LOCALAPPDATA%\ClipGuard\logs, and ClipGuard.exe sends them.

	bool LogPending() const { return m_logWant != 0; }

	// Short note on what the log is doing (empty when there is nothing to say).
	std::wstring LogNote() const
	{
		if (!ExeAlive()) return L"";
		const int st = (int)m_pShared->logState;
		if (st <= 0 || (DWORD)(GetTickCount() - m_pShared->logStateTick) > 10000) return L"";
		// 2.45.1: sending and sent are shown again (asked for, so a second F11
		// is not pressed while the first log is still being sent).
		if (st == 2) return L"Log: sending...";
		if (st == 3) return L"Log sent";
		if (st == 5) return L"Log: saved, not sent (no GitHub token in clipguard.ini)";
		if (st == 4) return L"Log: saved, sending failed (kept in %LOCALAPPDATA%\\ClipGuard\\logs)";
		return L"";
	}
	bool HasLogNote() const { return !LogNote().empty(); }

	// Called after the frame is drawn (overlay included), before Present.
	void SaveLog(ID3D11Texture2D* pBackBuffer)
	{
		const uint32_t id = m_logWant;
		m_logWant = 0;
		if (!id) return;
		std::wstring dir;
		{
			wchar_t buf[MAX_PATH] = {};
			const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
			if (n == 0 || n >= MAX_PATH) return;
			dir = buf;
			dir += L"\\ClipGuard";
			CreateDirectoryW(dir.c_str(), nullptr);
			dir += L"\\logs";
			CreateDirectoryW(dir.c_str(), nullptr);
		}
		wchar_t stem[64];
		swprintf_s(stem, L"\\log_%u", id);
		WriteLogCsv(dir + stem + L".csv");
		if (pBackBuffer) WriteScreenshot(pBackBuffer, dir + stem + L".bmp");
		if (m_pShared) m_pShared->logDone = id;
	}

private:
	struct LogSpot { float x, y, size, pk, fl, rg, nr, ease; };
	// Every tracked light of the frame: where it is (cells), how strongly it is
	// dimmed (w, the product of the weights after it) and why. miss = frames
	// its light was not found; hold = frames it is held while its light reads
	// graded, as a lamp or as a strip.
	struct LogTrack { float cx, cy, w, fade, ease, pkw, flw, rgw; short miss, hold; };
	static constexpr int kLogSpots = 4;
	struct LogRec {
		double t;
		float picture, peak, fix, white, hotFrac, flat, ring, nearSh, largest, text, sceneNits;
		int spots, lights, lamps, graded, hotActive, spread, skipped, strips, marker;
		float gradeMax, gradeCeil;
		float gate;
		int nSpot;
		LogSpot spot[kLogSpots];
		int nTrk;
		LogTrack trk[kMaxTracks];
		// What was put on screen with this row: the state of the frame
		// Look-ahead - 2 frames earlier (its patches, Guard amount, graded map).
		float shFix, shGrade, warm, warmCore, warmLow, fireSize;
		float laser, rampCeil, dbleP;
		int fires;
		int nSh;
		float sh[kMaxTracks][3];
	};
	static constexpr int kLogRecs = 4096;   // 30 s at up to ~120 frames a second
	std::vector<LogRec> m_log;
	int m_logHead = 0, m_logCount = 0;
	uint32_t m_logSeen = 0, m_logWant = 0;
	LogSpot m_logSpot[kLogSpots] = {};
	int m_logSpotN = 0;

	void LogFrame()
	{
		if (!m_haveMeasure || m_outputPQ) return;
		if (m_log.empty()) m_log.resize(kLogRecs);
		LogRec& r = m_log[m_logHead];
		r.t = NowMs();
		r.picture = m_picture; r.peak = m_framePeak; r.fix = m_smooth; r.white = m_ceil;
		r.hotFrac = m_hotFrac; r.flat = m_hotFlat; r.ring = m_hotRing; r.nearSh = m_hotNear;
		r.largest = m_largest; r.text = m_textPct; r.sceneNits = m_sceneNits;
		r.spots = m_spots; r.lights = m_spreadLights; r.lamps = m_litSpots; r.strips = m_stripPieces; r.marker = m_markPart; r.gate = m_hotGate;
		r.graded = m_gradedSpots; r.gradeMax = m_gradeMax; r.gradeCeil = m_gradeCeil; r.hotActive = m_hotActive ? 1 : 0;
		r.spread = m_spread ? 1 : 0; r.skipped = m_textSkipped ? 1 : 0;
		r.nSpot = m_logSpotN;
		for (int i = 0; i < kLogSpots; i++) r.spot[i] = m_logSpot[i];
		r.nTrk = 0;
		for (int k = 0; k < kMaxTracks; k++) {
			const Track& t = m_tracks[k];
			if (t.alpha <= 0.0f) continue;
			LogTrack& lt = r.trk[r.nTrk++];
			lt.cx = t.cx; lt.cy = t.cy;
			lt.w = t.alpha * t.fade * t.ease * t.pkw * t.flw * t.rgw;
			lt.fade = t.alpha * t.fade; lt.ease = t.ease; lt.pkw = t.pkw; lt.flw = t.flw; lt.rgw = t.rgw;
			lt.miss = t.pend ? (short)-1 : (short)std::min(t.miss, 9999);   // -1: not yet confirmed (not shown if it is lost) lt.hold = (short)std::min(t.fail, 9999);
		}
		{
			const Snap& sn = ShownSnap();
			r.laser = m_rampL; r.rampCeil = sn.rampCeil; r.dbleP = m_dbleP;
			r.fires = m_fireSpots; r.warm = m_warmMax; r.warmCore = m_warmCore; r.warmLow = m_warmLow; r.fireSize = m_fireSize;
			r.shFix = ShownSmooth();
			r.shGrade = m_shownGradeMax;
			r.nSh = 0;
			for (int k = 0; k < 2 * kMaxTracks; k++) {
				const Track& t = sn.tracks[k];
				if (t.alpha <= 0.0f || r.nSh >= kMaxTracks) continue;
				r.sh[r.nSh][0] = t.cx; r.sh[r.nSh][1] = t.cy;
				r.sh[r.nSh][2] = ShownWeight(sn, k);
				r.nSh++;
			}
		}
		m_logHead = (m_logHead + 1) % kLogRecs;
		m_logCount = std::min(m_logCount + 1, kLogRecs);
	}

	void WriteLogCsv(const std::wstring& path)
	{
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
		fprintf(f, "# ClipGuard 2.60.0 log, last 30 s\n");
		fprintf(f, "# settings: fix=%d strength=%.2f fixStarts=%.1f fullEffect=%.1f highlightStart=%.2f colorProtect=%.2f textSkip=%.0f hotSpot=%.2f hotSpotBelow=%.1f fixShape=%d gradedSpot=%.2f hotSpotMask=%d dimFire=%d grid=%d lookAhead=%d view=%d upscaler=%d plus=%d kscape=%d dither=%d projector=%d lut=%d\n",
			m_s.enabled ? 1 : 0, m_s.strength, m_s.triggerPct, m_s.fullPct, m_s.knee, m_s.colorProtect, m_s.textSkipPct,
			m_s.hotSpot, m_s.hotBelow, m_s.fixShape, m_s.gradedSpot, m_s.spotMask, m_s.dimFire ? 1 : 0, m_s.grid, m_s.lookAhead, m_s.view, m_s.upscaler, m_s.plus ? 1 : 0, m_s.kscape ? 1 : 0, m_s.dither, m_s.projector, m_s.lut);
		{
			// 2.48.3: the calibration LUT as the renderer has it.
			const int ls = LutState();
			const char* st = ls == 1 ? "on" : (ls == 2 ? "failed" : (ls == 3 ? "off (F5)" : "none"));
			char desc[256] = {};
			WideCharToMultiByte(CP_UTF8, 0, m_lutDesc.c_str(), -1, desc, (int)sizeof(desc), nullptr, nullptr);
			fprintf(f, "# lut: %s%s%s\n", st, desc[0] && ls != 0 ? " - " : "", ls != 0 ? desc : "");
		}
		fprintf(f, "# limits: ring %.2f-%.2f flat %.2f-%.2f (or peak %.2f-%.2f) hot spot full up to %.2f x Hot spot below, release after %d frames\n", kHotRingFull, kHotLitRing, kHotFlatMin, kHotFlatFull, kBrightStart, kBrightFull, kHotBelowFull, kReleaseFrames);
		fprintf(f, "t_s,picture,peak,fix,white,text,hot_pct,spots,lights,lamps,gate,graded,spread,dimmed,flat,ring,near,largest,skipped,grade_map,grade_white,strips,marker");
		for (int i = 0; i < kLogSpots; i++) fprintf(f, ",s%d_x,s%d_y,s%d_size,s%d_peak,s%d_flat,s%d_ring,s%d_near,s%d_ease", i, i, i, i, i, i, i, i);
		// Every tracked light: position (0-1 of the frame), dimming strength w
		// (0-1) = fade x size weight x peak weight x flat weight x ring (lamp)
		// weight, frames its light was missed and frames it is held.
		fprintf(f, ",tracks");
		for (int i = 0; i < kMaxTracks; i++) fprintf(f, ",t%d_x,t%d_y,t%d_w,t%d_fade,t%d_sizew,t%d_peakw,t%d_flatw,t%d_ringw,t%d_miss,t%d_hold", i, i, i, i, i, i, i, i, i, i);
		// Shown: what was on screen with this row (the frame Look-ahead - 2 rows
		// up): Guard amount, graded map peak and every patch drawn.
		fprintf(f, ",shown_fix,shown_grade,shown");
		for (int i = 0; i < kMaxTracks; i++) fprintf(f, ",u%d_x,u%d_y,u%d_w", i, i, i);
		// fires: lights left alone as fire (Dim fire off); warm: the warmest
		// glow of the lights checked, (red - blue) / red (-1 = none had a glow);
		// warm_core: that light's core; warm_low: the palest glow among the
		// lights reading 0.20 or warmer (-1 = none).
		fprintf(f, ",fires,warm,warm_core,warm_low,fire_size");
		fprintf(f, ",laser,ramp_ceil,dble_p");   // 2.46.0: Projector Titan Noir Max: modelled laser (0-1) and the squeeze's signal ceiling (1 = none)
		fprintf(f, "\n");
		const double now = NowMs();
		for (int k = m_logCount; k >= 1; k--) {
			const LogRec& r = m_log[(m_logHead - k + kLogRecs) % kLogRecs];
			const double age = (now - r.t) / 1000.0;
			if (age > 30.0) continue;
			fprintf(f, "%.3f,%.2f,%.3f,%.3f,%.3f,%.1f,%.2f,%d,%d,%d,%.3f,%d,%d,%d,%.3f,%.3f,%.3f,%.4f,%d,%.3f,%.3f,%d,%d",
				-age, r.picture, r.peak, r.fix, r.white, r.text, r.hotFrac * 100.0f, r.spots, r.lights, r.lamps, r.gate,
				r.graded, r.spread, r.hotActive, r.flat, r.ring, r.nearSh, r.largest, r.skipped, r.gradeMax, r.gradeCeil, r.strips, r.marker);
			for (int i = 0; i < kLogSpots; i++) {
				if (i < r.nSpot) {
					const LogSpot& sp = r.spot[i];
					fprintf(f, ",%.3f,%.3f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f", sp.x, sp.y, sp.size, sp.pk, sp.fl, sp.rg, sp.nr, sp.ease);
				} else {
					fprintf(f, ",,,,,,,,");
				}
			}
			fprintf(f, ",%d", r.nTrk);
			for (int i = 0; i < kMaxTracks; i++) {
				if (i < r.nTrk) {
					const LogTrack& lt = r.trk[i];
					fprintf(f, ",%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d",
						(lt.cx + 0.5f) / (float)std::max(m_cx, 1u), (lt.cy + 0.5f) / (float)std::max(m_cy, 1u),
						lt.w, lt.fade, lt.ease, lt.pkw, lt.flw, lt.rgw, (int)lt.miss, (int)lt.hold);
				} else {
					fprintf(f, ",,,,,,,,,,");
				}
			}
			fprintf(f, ",%.3f,%.3f,%d", r.shFix, r.shGrade, r.nSh);
			for (int i = 0; i < kMaxTracks; i++) {
				if (i < r.nSh) fprintf(f, ",%.3f,%.3f,%.3f", (r.sh[i][0] + 0.5f) / (float)std::max(m_cx, 1u), (r.sh[i][1] + 0.5f) / (float)std::max(m_cy, 1u), r.sh[i][2]);
				else fprintf(f, ",,,");
			}
			fprintf(f, ",%d,%.2f,%.2f,%.2f,%.4f", r.fires, r.warm, r.warmCore, r.warmLow, r.fireSize);
			fprintf(f, ",%.3f,%.3f,%.3f", r.laser, r.rampCeil, r.dbleP);
			fprintf(f, "\n");
		}
		fclose(f);
	}

	// The window as shown, 24-bit BMP, full resolution.
	void WriteScreenshot(ID3D11Texture2D* pSrc, const std::wstring& path)
	{
		D3D11_TEXTURE2D_DESC d = {};
		pSrc->GetDesc(&d);
		if (d.SampleDesc.Count != 1 || d.Width == 0 || d.Height == 0) return;
		const DXGI_FORMAT fmt = d.Format;
		const bool bgra = fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || fmt == DXGI_FORMAT_B8G8R8X8_UNORM;
		const bool rgba = fmt == DXGI_FORMAT_R8G8B8A8_UNORM || fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		const bool r10 = fmt == DXGI_FORMAT_R10G10B10A2_UNORM;
		const bool f16 = fmt == DXGI_FORMAT_R16G16B16A16_FLOAT;
		if (!bgra && !rgba && !r10 && !f16) return;
		D3D11_TEXTURE2D_DESC sd = d;
		sd.MipLevels = 1;
		sd.ArraySize = 1;
		sd.Usage = D3D11_USAGE_STAGING;
		sd.BindFlags = 0;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		sd.MiscFlags = 0;
		CComPtr<ID3D11Texture2D> pStage;
		if (FAILED(m_pDevice->CreateTexture2D(&sd, nullptr, &pStage))) return;
		m_pContext->CopyResource(pStage, pSrc);
		D3D11_MAPPED_SUBRESOURCE mr = {};
		if (FAILED(m_pContext->Map(pStage, 0, D3D11_MAP_READ, 0, &mr))) return;
		const int step = 1;   // full resolution, never scaled down
		const int w = (int)d.Width / step, h = (int)d.Height / step;
		const int rowB = (w * 3 + 3) & ~3;
		std::vector<uint8_t> img((size_t)rowB * h, 0);
		auto to8 = [](float v) { return (uint8_t)std::clamp((int)(v * 255.0f + 0.5f), 0, 255); };
		for (int y = 0; y < h; y++) {
			const uint8_t* src = (const uint8_t*)mr.pData + (size_t)mr.RowPitch * (size_t)(y * step);
			uint8_t* dst = &img[(size_t)rowB * (h - 1 - y)];  // BMP rows go bottom-up
			for (int x = 0; x < w; x++) {
				const int sx = x * step;
				uint8_t r, g, b;
				if (bgra) { const uint8_t* p = src + sx * 4; b = p[0]; g = p[1]; r = p[2]; }
				else if (rgba) { const uint8_t* p = src + sx * 4; r = p[0]; g = p[1]; b = p[2]; }
				else if (r10) {
					uint32_t v; memcpy(&v, src + sx * 4, 4);
					r = (uint8_t)(((v) & 0x3FF) >> 2); g = (uint8_t)(((v >> 10) & 0x3FF) >> 2); b = (uint8_t)(((v >> 20) & 0x3FF) >> 2);
				} else {
					uint16_t c[3]; memcpy(c, src + sx * 8, 6);
					r = to8(HalfToFloat(c[0])); g = to8(HalfToFloat(c[1])); b = to8(HalfToFloat(c[2]));
				}
				dst[x * 3 + 0] = b; dst[x * 3 + 1] = g; dst[x * 3 + 2] = r;
			}
		}
		m_pContext->Unmap(pStage, 0);
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
		const uint32_t dataB = (uint32_t)img.size();
		uint8_t hdr[54] = { 'B', 'M' };
		auto put32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
		auto put16 = [&](int off, uint16_t v) { memcpy(hdr + off, &v, 2); };
		put32(2, 54 + dataB); put32(10, 54); put32(14, 40); put32(18, (uint32_t)w); put32(22, (uint32_t)h);
		put16(26, 1); put16(28, 24); put32(34, dataB); put32(38, 2835); put32(42, 2835);
		fwrite(hdr, 1, sizeof(hdr), f);
		fwrite(img.data(), 1, img.size(), f);
		fclose(f);
	}

	static int NormLookAhead(int v)
	{
		if (v <= 0) return 0;
		if (v <= 2) return 2;   // 1 = "on" from 2.12.0
		if (v <= 4) return 4;
		if (v <= 6) return 6;
		return 12;              // High Performance
	}

	static double NowMs()
	{
		static double freq = 0.0;
		if (freq == 0.0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = (double)f.QuadPart / 1000.0; }
		LARGE_INTEGER c; QueryPerformanceCounter(&c);
		return (double)c.QuadPart / freq;
	}
	void CpuStart() { m_cpuT0 = NowMs(); }
	void CpuStop() { m_cpuFrameMs += NowMs() - m_cpuT0; }
	// Called at the start of each frame: closes the last frame's numbers and,
	// once a second, updates what the overlay shows.
	void CpuFrame()
	{
		const double now = NowMs();
		if (m_lastFrameT > 0.0) {
			m_frameSum += now - m_lastFrameT;
			m_cpuSum += m_cpuFrameMs; m_cpuN++; m_cpuMax = std::max(m_cpuMax, m_cpuFrameMs);
		}
		m_lastFrameT = now;
		m_cpuFrameMs = 0.0;
		if (m_costT0 <= 0.0) m_costT0 = now;
		if (now - m_costT0 >= 1000.0 && m_cpuN > 0) {
			m_costCpuAvg = m_cpuSum / m_cpuN; m_costCpuMax = m_cpuMax;
			m_costFrame = m_frameSum / m_cpuN;
			if (m_gpuN > 0) { m_costGpuAvg = m_gpuSum / m_gpuN; m_costGpuMax = m_gpuMax; }
			m_cpuSum = m_cpuMax = m_frameSum = m_gpuSum = m_gpuMax = 0.0;
			m_cpuN = m_gpuN = 0;
			m_costT0 = now;
		}
	}

	HRESULT CreatePS(const char* src, size_t len, ID3D11PixelShader** pps)
	{
		ID3DBlob* pBlob = nullptr;
		HRESULT hr = CompileShader(std::string(src, len), nullptr, "ps_4_0", &pBlob);
		if (SUCCEEDED(hr) && pBlob) {
			hr = m_pDevice->CreatePixelShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, pps);
		}
		if (pBlob) pBlob->Release();
		return hr;
	}

	static float HalfToFloat(uint16_t h)
	{
		const uint32_t sign = (uint32_t)(h & 0x8000) << 16;
		uint32_t exp = (h >> 10) & 0x1F;
		uint32_t mant = h & 0x3FF;
		uint32_t f;
		if (exp == 0) {
			if (mant == 0) {
				f = sign;
			} else {
				exp = 127 - 15 + 1;
				while (!(mant & 0x400)) { mant <<= 1; exp--; }
				mant &= 0x3FF;
				f = sign | (exp << 23) | (mant << 13);
			}
		} else if (exp == 31) {
			f = sign | 0x7F800000 | (mant << 13);
		} else {
			f = sign | ((exp + 127 - 15) << 23) | (mant << 13);
		}
		float out;
		memcpy(&out, &f, sizeof(out));
		return out;
	}

	void ReadMeasurement(ID3D11Texture2D* pStaging, bool wait)
	{
		D3D11_MAPPED_SUBRESOURCE mr = {};
		// With look-ahead the measurement must belong to the frame being shown, so wait for it.
		if (FAILED(m_pContext->Map(pStaging, 0, D3D11_MAP_READ, wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &mr))) {
			return; // not finished yet (or failed): keep the last values
		}
		const float kContent = 0.04f; // signal; a cell brighter than this is picture, not bar
		const int cxN = (int)m_cx, cyN = (int)m_cy;
		const int gs = m_grid ? 2 : 1; // cell distances scale with the grid
		bool rowHas[kMaxCellsY] = {};
		bool colHas[kMaxCellsX] = {};
		auto cellRaw = [&](UINT x, UINT y, int ch) {
			const float* row = (const float*)((const uint8_t*)mr.pData + (size_t)y * mr.RowPitch);
			return row[x * 4 + ch];
		};
		// Channel 3 holds the highlight samples plus 100 x the flat-core samples.
		auto cell = [&](UINT x, UINT y, int ch) {
			const float v = cellRaw(x, y, ch);
			return (ch == 3 || ch == 2) ? std::fmod(v, 100.0f) : v;
		};
		auto warmN = [&](UINT x, UINT y) { return std::floor(cellRaw(x, y, 2) / 100.0f + 0.001f) / 99.0f; };   // 0..1, see kWarmStart
		auto flatN = [&](UINT x, UINT y) { return std::floor(cellRaw(x, y, 3) / 100.0f + 0.001f); };
		if (m_s.testBuild) {
			DetectTestMarker([&](float fx, float fy) {
				const UINT x = (UINT)std::clamp((int)(fx * (float)m_cx), 0, (int)m_cx - 1);
				const UINT y = (UINT)std::clamp((int)(fy * (float)m_cy), 0, (int)m_cy - 1);
				return cell(x, y, 1);
			});
		} else {
			m_markPart = 0;   // the test video's code is only looked for by ClipGuard Test
			m_markRun = 0;
		}
		for (UINT y = 0; y < m_cy; y++) {
			for (UINT x = 0; x < m_cx; x++) {
				if (cell(x, y, 1) > kContent) { rowHas[y] = true; colHas[x] = true; }
			}
		}
		int y0 = 0, y1 = cyN - 1, x0 = 0, x1 = cxN - 1;
		while (y0 < cyN && !rowHas[y0]) y0++;
		while (y1 >= 0 && !rowHas[y1]) y1--;
		while (x0 < cxN && !colHas[x0]) x0++;
		while (x1 >= 0 && !colHas[x1]) x1--;

		double sum = 0.0;
		double textSamples = 0.0, brightSamples = 0.0;
		int n = 0, clipped = 0, hotCells = 0;
		float framePeak = 0.0f;
		float nearPeak[256] = {};   // 2.47.0: near-peak area by level, in cells (what DBLE counts)
		float whiteArea = 0.0f;     // 2.49.0: near-white (not warm) area at kDbleWhiteMin or more, in cells
		struct Spot { int x0, y0, x1, y1; float lv; int core; float pk; float ease; float fl; float rg; float nr; float wm; };  // wm: Dim fire weight; lv, core, pk: a hot spot's core level, cells and peak; ease: 1 = full dimming; fl: how flat its core is; rg, nr: lit and near share of its ring
		Spot spots[kMaxSpots];
		static Spot cands[kMaxCands];
		int nCands = 0;
		int nSpots = 0;
		int litSpots = 0;
		int fireSpots = 0;       // lights left alone as fire (Dim fire off)
		float warmMax = -1.0f;   // warmest glow of the lights checked (readout; -1 = none had a glow)
		float warmCore = 0.0f;   // that light's core, for comparison (readout)
		float fireSize = 0.0f;   // 2.42.5: core size (share of the picture) of the largest warm light
		float warmLow = -1.0f;   // the palest glow among the warm lights (kWarmLog or more; -1 = none)
		int stripPieces = 0;
		Spot stripList[kMaxSpots];   // strip pieces: they can only hold a hot spot already there
		int nStrip = 0;
		int gradedSpots = 0;
		float bestFlat = -1.0f;
		Spot gradedList[kMaxSpots];  // graded candidates: they can only drain a hot spot already there
		int nGraded = 0;
		Spot gradeLow[kMaxSpots];    // graded spot candidates peaking below kHotMinPeak
		int nGradeLow = 0;
		Spot lampList[kMaxSpots];    // lamp candidates (lit surroundings): the same
		int nLamp = 0;
		bool spread = false;
		bool spreadArea = false;
		int spreadLights = 0;
		float largest = 0.0f;
		int largeSpots = 0;
		float ringMax = -1.0f;  // lit share of the ring around the lights that passed (readout)
		float nearMax = -1.0f;  // near share of the lit ring around the lights that passed (readout)
		// One group of touching cells at lv or more, starting from (sx, sy):
		// its cells go to gMembers (the count is returned, the start cell first)
		// and its peak to gp. Reach: touching cells on Normal (diagonals
		// included); up to 2 cells on Fine (the same distance), so a light with
		// a darker gap inside stays one light. gSeen is cleared by the caller
		// before each pass over the picture.
		static uint8_t gSeen[kMaxCellsY][kMaxCellsX];
		static int gStack[kMaxCellsX * kMaxCellsY];
		static int gMembers[kMaxCellsX * kMaxCellsY];
		auto flood = [&](int sx, int sy, float lv, float& gp) -> int {
			int top = 0, nm = 0;
			gp = 0.0f;
			gStack[top++] = sy * cxN + sx;
			gSeen[sy][sx] = 1;
			while (top > 0) {
				const int i = gStack[--top];
				const int cx = i % cxN, cy = i / cxN;
				gMembers[nm++] = i;
				gp = std::max(gp, cell(cx, cy, 1));
				for (int dy = -gs; dy <= gs; dy++) {
					for (int dx = -gs; dx <= gs; dx++) {
						const int nx = cx + dx, ny = cy + dy;
						if (nx < x0 || nx > x1 || ny < y0 || ny > y1 || gSeen[ny][nx]) continue;
						if (cell(nx, ny, 1) < lv) continue;
						gSeen[ny][nx] = 1;
						gStack[top++] = ny * cxN + nx;
					}
				}
			}
			return nm;
		};
		if (y0 <= y1 && x0 <= x1) {
			for (int y = y0; y <= y1; y++) {
				for (int x = x0; x <= x1; x++) {
					sum += cell(x, y, 0);
					textSamples += cell(x, y, 2);
					brightSamples += cell(x, y, 3);
					const float peak = cell(x, y, 1);
					if (peak >= 0.995f) clipped++;
					framePeak = std::max(framePeak, peak);
					nearPeak[std::clamp((int)(peak * 255.0f + 0.5f), 0, 255)] += flatN(x, y) / 64.0f;
					if (peak >= kDbleWhiteMin && warmN(x, y) <= kDbleWhiteWarm) whiteArea += flatN(x, y) / 64.0f;
					n++;
				}
			}
			// Hot spot candidates: every group of touching cells (diagonals included;
			// on Fine, cells up to 2 apart)
			// at kHotMinPeak - kHotMargin or more whose own peak reaches kHotMinPeak.
			// Each group's core is its cells within kHotMargin of that group's OWN
			// peak, so a brighter object elsewhere in the frame can't hide a light.
			// A group too big to be compact is not a spot (the main fix handles it). Of the lights that pass the checks below, more than
			// kSpreadLights, or cores over kHotMaxFrac of the picture, read "spread".
			if (framePeak >= kHotMinPeak) {
				const float grp = kHotMinPeak - kHotMargin;
				const float maxW = std::max(1.0f, kHotBoxW * (float)(x1 - x0 + 1));
				const float maxH = std::max(1.0f, kHotBoxH * (float)(y1 - y0 + 1));
				int* const members = gMembers;
				memset(gSeen, 0, sizeof(gSeen));
				// Lights grouped down to kStripLevel: each cell's group id, and whether
				// that group is larger than the compact limits (worked out once per group).
				static uint16_t wideId[kMaxCellsY][kMaxCellsX];
				static int wideStack[kMaxCellsX * kMaxCellsY];
				static bool wideBig[kMaxCellsX * kMaxCellsY / 2 + 2];
				memset(wideId, 0, sizeof(wideId));
				int wideN = 0;
				auto wideTooBig = [&](int sx, int sy) -> bool {
					if (wideId[sy][sx]) return wideBig[wideId[sy][sx]];
					if (wideN + 1 >= (int)(sizeof(wideBig) / sizeof(wideBig[0]))) return false;
					const uint16_t id = (uint16_t)(++wideN);
					int top = 0, bx0 = sx, bx1 = sx, by0 = sy, by1 = sy;
					wideStack[top++] = sy * cxN + sx;
					wideId[sy][sx] = id;
					while (top > 0) {
						const int i = wideStack[--top];
						const int cx = i % cxN, cy = i / cxN;
						bx0 = std::min(bx0, cx); bx1 = std::max(bx1, cx);
						by0 = std::min(by0, cy); by1 = std::max(by1, cy);
						for (int dy = -gs; dy <= gs; dy++) {
							for (int dx = -gs; dx <= gs; dx++) {
								const int nx = cx + dx, ny = cy + dy;
								if (nx < x0 || nx > x1 || ny < y0 || ny > y1 || wideId[ny][nx]) continue;
								if (cell(nx, ny, 1) < kStripLevel) continue;
								wideId[ny][nx] = id;
								wideStack[top++] = ny * cxN + nx;
							}
						}
					}
					wideBig[id] = (bx1 - bx0 + 1) > maxW || (by1 - by0 + 1) > maxH;
					return wideBig[id];
				};
				const float cellScale = m_grid == 0 ? 2.0f : 1.0f;  // Fine cells per cell
				for (int y = y0; y <= y1; y++) {
					for (int x = x0; x <= x1; x++) {
						if (gSeen[y][x] || cell(x, y, 1) < grp) continue;
						float gp = 0.0f;
						const int nm = flood(x, y, grp, gp);
						if (gp < kHotMinPeak) continue;
						Spot sp = { cxN, cyN, -1, -1, gp - kHotMargin };
						int core = 0;
						int gx0 = cxN, gy0 = cyN, gx1 = -1, gy1 = -1;
						for (int k = 0; k < nm; k++) {
							const int cx = members[k] % cxN, cy = members[k] / cxN;
							gx0 = std::min(gx0, cx); gx1 = std::max(gx1, cx);
							gy0 = std::min(gy0, cy); gy1 = std::max(gy1, cy);
							if (cell(cx, cy, 1) < sp.lv) continue;
							core++;
							sp.x0 = std::min(sp.x0, cx); sp.x1 = std::max(sp.x1, cx);
							sp.y0 = std::min(sp.y0, cy); sp.y1 = std::max(sp.y1, cy);
						}
						hotCells += core;
						const bool compact = (sp.x1 - sp.x0 + 1) <= maxW && (sp.y1 - sp.y0 + 1) <= maxH;
						if (!compact) continue;
						sp.core = core;
						sp.pk = gp;
						// Strip pieces (a mirror's light frame, a neon tube) never start a
						// hot spot; one already there is held (hysteresis), not dropped.
						{
							const int cw = sp.x1 - sp.x0 + 1, ch = sp.y1 - sp.y0 + 1;
							const int cs = std::min(cw, ch), cl = std::max(cw, ch);
							const bool thin = (float)cs * cellScale <= kStripThin && (float)cl >= kStripLong * (float)cs;
							const int s0 = members[0] % cxN, s1 = members[0] / cxN;
							if (thin || (gx1 - gx0 + 1) > maxW || (gy1 - gy0 + 1) > maxH || wideTooBig(s0, s1)) {
								stripPieces++;
								if (nStrip < kMaxSpots) stripList[nStrip++] = sp;
								continue;
							}
						}
						if (nCands < kMaxCands) cands[nCands++] = sp;
					}
				}
				// Brightest first (then biggest), so the real lights are always among those
				// checked and the same ones are dimmed from frame to frame.
				std::sort(cands, cands + nCands, [](const Spot& a, const Spot& b) {
					if (a.pk != b.pk) return a.pk > b.pk;
					if (a.core != b.core) return a.core > b.core;
					return (a.y0 != b.y0) ? a.y0 < b.y0 : a.x0 < b.x0;
				});
				// Each candidate must be a light: a flat core against dark
				// surroundings. Only the ones that pass count toward kMaxSpots and
				// kHotMaxFrac, so lamps and graded highlights don't make it "spread".
				const float lit = std::clamp(m_s.knee, 0.20f, 0.95f);
				int kept = 0, keptCells = 0, lights = 0;
				for (int i = 0; i < nCands; i++) {
					const Spot& sp = cands[i];
					// A light has a flat core (many samples at its top level); a face,
					// skin or a lit surface is graded, so it is not a hot spot.
					float fl = 0.0f;
					{
						double fs = 0.0;
						int fc = 0;
						for (int y = sp.y0; y <= sp.y1; y++) {
							for (int x = sp.x0; x <= sp.x1; x++) {
								if (cell(x, y, 1) < sp.lv) continue;
								fs += flatN(x, y);
								fc++;
							}
						}
						fl = fc > 0 ? (float)(fs / (64.0 * fc)) : 0.0f;
						bestFlat = std::max(bestFlat, fl);
						fl = std::max(fl, kHotFlatFull * Smooth01(sp.pk, kBrightStart, kBrightFull));
						if (fl < kHotFlatMin) {
							gradedSpots++;
							if (nGraded < kMaxSpots) {
								gradedList[nGraded] = sp;
								gradedList[nGraded].fl = fl;
								nGraded++;
							}
							continue;
						}
					}
					// Dim fire off: a clearly orange or red light is not a hot spot; a
					// slightly warm one is eased.
					// Judged by the glow: the cells around the core (out to half the
					// light's size, at least 2 cells) that are below the core level but
					// bright enough to have a colour measured. No glow: not fire.
					float warmW = 1.0f;
					{
						const float glowMin = std::min(lit, 0.75f);   // the measuring pass reads colour above this
						const int pad = std::max(2, std::max(sp.x1 - sp.x0 + 1, sp.y1 - sp.y0 + 1) / 2);
						double ws = 0.0, cs = 0.0;
						int wc = 0, cc = 0;
						for (int y = std::max(y0, sp.y0 - pad); y <= std::min(y1, sp.y1 + pad); y++) {
							for (int x = std::max(x0, sp.x0 - pad); x <= std::min(x1, sp.x1 + pad); x++) {
								const float c = cell(x, y, 1);
								if (c >= sp.lv) { cs += warmN(x, y); cc++; continue; }
								if (c <= glowMin) continue;
								ws += warmN(x, y);
								wc++;
							}
						}
						if (wc >= kGlowMinCells) {
							const float warm = (float)(ws / wc);
							if (warm > warmMax) { warmMax = warm; warmCore = cc > 0 ? (float)(cs / cc) : 0.0f; }
							if (warm >= kWarmLog && (warmLow < 0.0f || warm < warmLow)) warmLow = warm;
							const float ffrac = n > 0 ? (float)sp.core / (float)n : 0.0f;
							if (warm >= kWarmStart) fireSize = std::max(fireSize, ffrac);
							if (!m_s.dimFire) {
								warmW = std::max(1.0f - Smooth01(warm, kWarmStart, kWarmFull), 1.0f - Smooth01(ffrac, kFireSmallStart, kFireSmallFull));
								if (warmW <= 0.0f) { fireSpots++; continue; }
							}
						}
					}
					const float scx = 0.5f * (float)(sp.x0 + sp.x1);
					const float scy = 0.5f * (float)(sp.y0 + sp.y1);
					const float sr = 0.5f * (float)std::max(sp.x1 - sp.x0 + 1, sp.y1 - sp.y0 + 1) + (float)gs;
					const float rIn = sr + (float)gs, rOut = 2.0f * sr + 2.0f * (float)gs;
					int ring = 0, bright = 0, nearC = 0;
					const float nearLv = sp.pk - kHotNearBand;
					for (int y = std::max(y0, (int)std::floor(scy - rOut)); y <= std::min(y1, (int)std::ceil(scy + rOut)); y++) {
						for (int x = std::max(x0, (int)std::floor(scx - rOut)); x <= std::min(x1, (int)std::ceil(scx + rOut)); x++) {
							const float dx = (float)x - scx, dy = (float)y - scy;
							const float d2 = dx * dx + dy * dy;
							if (d2 < rIn * rIn || d2 > rOut * rOut) continue;
							ring++;
							const float c = cell(x, y, 1);
							if (c >= lit) {
								bright++;
								if (c >= nearLv) nearC++;
							}
						}
					}
					const float ringLit = ring > 0 ? (float)bright / (float)ring : 0.0f;
					if (ringLit > kHotLitRing) {
						litSpots++;
						if (nLamp < kMaxSpots) {
							lampList[nLamp] = sp;
							lampList[nLamp].fl = fl;
							lampList[nLamp].rg = ringLit;
							nLamp++;
						}
						continue;
					}
					const float nearSh = bright >= kHotNearMinCells ? (float)nearC / (float)bright : 0.0f;
					ringMax = std::max(ringMax, ringLit);
					nearMax = std::max(nearMax, nearSh);
					keptCells += sp.core;
					lights++;
					if (kept < kMaxSpots) {
						spots[kept] = sp;
						spots[kept].fl = fl;
						spots[kept].wm = warmW;
						spots[kept].rg = ringLit;
						spots[kept].nr = nearSh;
						kept++;
					}
				}
				spreadLights = lights;
				// Every light is dimmed, up to kMaxSpots (= kSpreadLights); more than
				// that is a scattered frame (neon, strings of lights): none are.
				if (lights > kSpreadLights) {
					spread = true;
				}
				if (n > 0 && (float)keptCells / (float)n > kHotMaxFrac) {
					spread = true;
					spreadArea = true;
				}
				nSpots = spread ? 0 : kept;
				// Large lights and clipped discs are eased: less hot spot dimming.
				auto ramp = [](float v, float a, float b) {
					const float u = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
					return u * u * (3.0f - 2.0f * u);
				};
				for (int i = 0; i < kept; i++) {
					const float frac = n > 0 ? (float)spots[i].core / (float)n : 0.0f;
					const float bySize = 1.0f - ramp(frac, kLargeStart, kLargeFull);
					const float byClip = 1.0f - ramp(spots[i].fl, kClipFlatStart, kClipFlatFull) * ramp(frac, kClipSizeStart, kClipSizeFull);
					spots[i].ease = std::min(bySize, byClip) * spots[i].wm;
					largest = std::max(largest, frac);
					if (i < nSpots && spots[i].ease < 0.99f) largeSpots++;
				}
			}
			// Graded spot candidates below the hot spot level: compact areas peaking
			// from Highlight start + kGradeAboveKnee up to kHotMinPeak (those at
			// kHotMinPeak or more are found above). Grouped the same way.
			{
				const float gMin = std::clamp(m_s.knee, 0.20f, 0.95f) + kGradeAboveKnee;
				if (m_s.gradedSpot > 0.0f && gMin < kHotMinPeak && framePeak >= gMin) {
					const float grp = gMin - kHotMargin;
					const float maxW = std::max(1.0f, kHotBoxW * (float)(x1 - x0 + 1));
					const float maxH = std::max(1.0f, kHotBoxH * (float)(y1 - y0 + 1));
					int* const membersG = gMembers;
					memset(gSeen, 0, sizeof(gSeen));
					for (int y = y0; y <= y1 && nGradeLow < kMaxSpots; y++) {
						for (int x = x0; x <= x1 && nGradeLow < kMaxSpots; x++) {
							if (gSeen[y][x] || cell(x, y, 1) < grp) continue;
							float gp = 0.0f;
							const int nm = flood(x, y, grp, gp);
							if (gp < gMin || gp >= kHotMinPeak) continue;
							Spot sp = { cxN, cyN, -1, -1, gp - kHotMargin };
							int core = 0;
							for (int k = 0; k < nm; k++) {
								const int cx = membersG[k] % cxN, cy = membersG[k] / cxN;
								if (cell(cx, cy, 1) < sp.lv) continue;
								core++;
								sp.x0 = std::min(sp.x0, cx); sp.x1 = std::max(sp.x1, cx);
								sp.y0 = std::min(sp.y0, cy); sp.y1 = std::max(sp.y1, cy);
							}
							if ((sp.x1 - sp.x0 + 1) > maxW || (sp.y1 - sp.y0 + 1) > maxH) continue;
							sp.core = core;
							sp.pk = gp;
							gradeLow[nGradeLow++] = sp;
						}
					}
				}
			}
			// Fix shape Anchor: anchor level and large bright areas.
			if (m_s.fixShape == 1 && n > 0) {
				static float pks[kMaxCellsX * kMaxCellsY];
				int np = 0;
				for (int y = y0; y <= y1; y++)
					for (int x = x0; x <= x1; x++) pks[np++] = cell(x, y, 1);
				const int kth = std::clamp((int)((1.0f - kAnchorPct) * (float)np), 0, np - 1);
				std::nth_element(pks, pks + kth, pks + np);
				const float lv = pks[kth];
				m_anchorLv = m_cut ? lv : m_anchorLv + 0.3f * (lv - m_anchorLv);
				static float target[kMaxCellsX * kMaxCellsY];
				int* const membersA = gMembers;
				std::fill(target, target + (size_t)cxN * cyN, 0.0f);
				memset(gSeen, 0, sizeof(gSeen));
				const float grp = std::max(m_anchorLv - kAnchorGroup, std::clamp(m_s.knee, 0.20f, 0.95f));
				const int minCells = std::max(1, (int)(kAnchorMinFrac * (float)n));
				for (int y = y0; y <= y1; y++) {
					for (int x = x0; x <= x1; x++) {
						if (gSeen[y][x] || cell(x, y, 1) < grp) continue;
						float gpA = 0.0f;
						const int nm = flood(x, y, grp, gpA);
						if (nm < minCells) continue;
						// Mark the area and one ring of cells around it (soft edge).
						for (int k = 0; k < nm; k++) {
							const int cx = membersA[k] % cxN, cy = membersA[k] / cxN;
							for (int dy = -gs; dy <= gs; dy++) {
								for (int dx = -gs; dx <= gs; dx++) {
									const int nx = cx + dx, ny = cy + dy;
									if (nx < 0 || nx >= cxN || ny < 0 || ny >= cyN) continue;
									float& t = target[(size_t)ny * cxN + nx];
									t = std::max(t, (dx == 0 && dy == 0) ? 1.0f : 0.5f);
								}
							}
						}
					}
				}
				const float rate = m_cut ? 1.0f : kAnchorRate;
				for (size_t i = 0; i < (size_t)cxN * cyN; i++) m_anchorMap[i] += rate * (target[i] - m_anchorMap[i]);
				// Anchor gain: the cells' peaks blurred (box of kAnchorBlur cells each
				// way), then a gentle dip below the anchor level: none at Highlight
				// start and at the anchor level, deepest half way, scaled by the fix
				// amount. A smooth gain keeps each pixel's ratio to its neighbours.
				static double satA[kMaxCellsY + 1][kMaxCellsX + 1];
				for (int y = 0; y <= cyN; y++) satA[y][0] = 0.0;
				for (int x = 0; x <= cxN; x++) satA[0][x] = 0.0;
				for (int y = 0; y < cyN; y++) {
					double row = 0.0;
					for (int x = 0; x < cxN; x++) {
						const bool in = x >= x0 && x <= x1 && y >= y0 && y <= y1;
						row += in ? cell(x, y, 1) : 0.0f;
						satA[y + 1][x + 1] = satA[y][x + 1] + row;
					}
				}
				const int rb = kAnchorBlur * gs;
				const float kn = std::clamp(m_s.knee, 0.20f, 0.95f);
				const float L = m_anchorLv;
				const float amt = kAnchorDip * std::max(m_smooth, 0.0f);   // the fix amount (Strength x darkness)
				for (int y = 0; y < cyN; y++) {
					for (int x = 0; x < cxN; x++) {
						const size_t i = (size_t)y * cxN + x;
						float g = 1.0f;
						if (m_anchorMap[i] > 0.001f && L > kn + 0.02f) {
							const int xa = std::max(x - rb, x0), xb = std::min(x + rb, x1);
							const int ya = std::max(y - rb, y0), yb = std::min(y + rb, y1);
							if (xa <= xb && ya <= yb) {
								const int cnt = (xb - xa + 1) * (yb - ya + 1);
								const float B = (float)((satA[yb + 1][xb + 1] - satA[ya][xb + 1] - satA[yb + 1][xa] + satA[ya][xa]) / cnt);
								if (B > kn && B < L) {
									const float u = (B - kn) / (L - kn);
									const float dip = amt * (L - kn) * 4.0f * u * (1.0f - u) * 0.25f;
									g = (B - dip) / B;
								}
							}
						}
						m_anchorGain[i] += rate * (g - m_anchorGain[i]);
					}
				}
			}
			m_bottomBarFrac = (float)(cyN - 1 - y1) / (float)cyN;
		} else {
			m_bottomBarFrac = 0.0f; // black frame: no picture to measure
		}
		m_pContext->Unmap(pStaging, 0);

		const float pic = n ? (float)(sum / n * 100.0) : 0.0f;
		// Scene cut: brightness jumps by kCutRatio or more. Snap instead of easing
		// in. (Something dark passing the camera halves the brightness; that is
		// not a cut.)
		if (m_haveMeasure && std::max(pic, m_picture) > 0.2f
			&& (pic > m_picture * kCutRatio || pic < m_picture / kCutRatio)) {
			m_cut = true;
		}
		m_picture = pic;
		m_bx0 = x0; m_bx1 = x1; m_by0 = y0; m_by1 = y1;
		AreaPush(x0, x1, y0, y1);
		m_histHead = (m_histHead + 1) % kHist;
		m_histPic[m_histHead] = pic;
		m_histPeak[m_histHead] = framePeak;
		m_brightSamples = (float)brightSamples;
		m_textPct = brightSamples > 0.0 ? (float)(textSamples / brightSamples * 100.0) : 0.0f;
		m_clipFrac = n ? (float)clipped / (float)n : 0.0f;
		m_hotCells = hotCells;
		m_spots = nSpots;
		m_litSpots = litSpots;
		m_fireSpots = fireSpots;
		m_warmMax = warmMax;
		m_warmCore = warmCore;
		m_warmLow = warmLow;
		m_fireSize = fireSize;
		m_stripPieces = stripPieces;
		m_gradedSpots = gradedSpots;
		m_spread = spread;
		m_spreadArea = spreadArea;
		m_spreadLights = spreadLights;
		m_largest = largest;
		m_largeSpots = largeSpots;
		m_hotFlat = bestFlat;
		m_hotRing = ringMax;
		m_hotNear = nearMax;
		m_framePeak = framePeak;
		{
			// The level of the brightest kDbleArea of the picture: walk down from the
			// top until the cells' near-peak area adds up to it.
			const float want = kDbleArea * (float)std::max(n, 1);
			float acc = 0.0f;
			int b = 255;
			for (; b > 0; b--) { acc += nearPeak[b]; if (acc >= want) break; }
			m_dbleP = (float)b / 255.0f;
			if (whiteArea >= kDbleWhiteArea * (float)std::max(n, 1)) m_dbleP = std::max(m_dbleP, DbleWhiteP());
		}
		// The lights kept, for the log (F11).
		m_logSpotN = 0;
		for (int i = 0; i < nSpots && m_logSpotN < kLogSpots; i++) {
			LogSpot& ls = m_logSpot[m_logSpotN++];
			ls.x = (0.5f * (float)(spots[i].x0 + spots[i].x1) + 0.5f) / (float)cxN;
			ls.y = (0.5f * (float)(spots[i].y0 + spots[i].y1) + 0.5f) / (float)cyN;
			ls.size = n > 0 ? (float)spots[i].core / (float)n : 0.0f;
			ls.pk = spots[i].pk; ls.fl = spots[i].fl; ls.rg = spots[i].rg; ls.nr = spots[i].nr; ls.ease = spots[i].ease;
		}
		UpdateTracks(spots, nSpots, gradedList, spread ? 0 : nGraded, lampList, spread ? 0 : nLamp, stripList, spread ? 0 : nStrip);
		{
			Spot gradeAll[2 * kMaxSpots];
			int nAll = 0;
			for (int i = 0; i < nGraded && !spread; i++) gradeAll[nAll++] = gradedList[i];
			for (int i = 0; i < nGradeLow && !spread; i++) gradeAll[nAll++] = gradeLow[i];
			UpdateGrade(gradeAll, nAll, n);
		}
		m_hotFrac = n ? (float)hotCells / (float)n : 0.0f;
		m_haveMeasure = true;
	}

	// The picture as sent: average light and peak over the same picture area as
	// the incoming measurement (black bars drawn by a test view count as picture).
	void ReadAfter(ID3D11Texture2D* pStaging)
	{
		D3D11_MAPPED_SUBRESOURCE mr = {};
		if (FAILED(m_pContext->Map(pStaging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mr))) {
			return; // not finished yet: keep the last values
		}
		double sum = 0.0;
		float peak = 0.0f;
		int n = 0;
		for (int y = std::max(m_by0, 0); y <= std::min(m_by1, (int)m_cy - 1); y++) {
			const float* row = (const float*)((const uint8_t*)mr.pData + (size_t)y * mr.RowPitch);
			for (int x = std::max(m_bx0, 0); x <= std::min(m_bx1, (int)m_cx - 1); x++) {
				sum += row[x * 4 + 0];
				peak = std::max(peak, row[x * 4 + 1]);
				n++;
			}
		}
		m_pContext->Unmap(pStaging, 0);
		if (n > 0) {
			m_picAfter = (float)(sum / n * 100.0);
			m_peakAfter = peak;
			m_haveAfter = true;
		}
	}

	// ---- hot spot tracking ----------------------------------------------------------

	float gsF() const { return m_grid ? 2.0f : 1.0f; }

	// Hot spot dimming by how lit a light's surroundings are: full up to
	// kHotRingFull, none at kHotLitRing.
	static float RingWeight(float rg)
	{
		const float u = std::clamp((rg - kHotRingFull) / (kHotLitRing - kHotRingFull), 0.0f, 1.0f);
		return 1.0f - u * u * (3.0f - 2.0f * u);
	}

	static float Smooth01(float v, float a, float b)
	{
		const float u = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
	}

	// Hot spot dimming by a light's own peak (0 at kPeakStart, 1 from kPeakFull)
	// or its flat core (0 at kPeakFlatStart, 1 from kPeakFlatFull), the larger.
	static float PeakWeight(float pk, float fl)
	{
		return std::max(Smooth01(pk, kPeakStart, kPeakFull), Smooth01(fl, kPeakFlatStart, kPeakFlatFull));
	}

	// Hot spot dimming by how flat a light's core is: none at kHotFlatMin, full
	// from kHotFlatFull. Kept apart from PeakWeight because it follows slowly
	// both ways: a light whose flatness wanders around kHotFlatMin (cut off by
	// the frame edge) gets a steady amount instead of pulses.
	static float FlatWeight(float fl)
	{
		return Smooth01(fl, kHotFlatMin, kHotFlatFull);
	}

	// How far from its predicted place a patch looks for its light: its own size
	// plus a margin, plus its speed (a fast light can land off the prediction).
	float Reach(const Track& t) const
	{
		return 2.0f * t.r + 4.0f * gsF() + 1.5f * std::hypot(t.vx, t.vy);
	}
	// Move a patch to its light: halfway from where it should be now to where
	// the light was found, and update its speed from the step it took.
	void Follow(Track& t, float cx, float cy)
	{
		const float px = t.cx + t.vx, py = t.cy + t.vy;
		const float nx = px + 0.5f * (cx - px), ny = py + 0.5f * (cy - py);
		t.vx += kSpeedRate * ((nx - t.cx) - t.vx);
		t.vy += kSpeedRate * ((ny - t.cy) - t.vy);
		const float sp = std::hypot(t.vx, t.vy), cap = kMaxSpeed * gsF();
		if (sp > cap) { t.vx *= cap / sp; t.vy *= cap / sp; }
		t.cx = nx; t.cy = ny;
	}

	// A spot found inside an existing patch moves that patch; otherwise it
	// starts a new one. When several lights sit within one patch's reach (a
	// tight group of small lights), the patch follows the one nearest to it and
	// grows to cover the others (cov), and once it is such a group it stays
	// where it is instead of jumping to a neighbour on a frame its own light
	// is not found: the patch used to hop between the lights, which then
	// flickered.
	template <class SpotT>
	void MatchSpots(const SpotT* spots, int nSpots, int maxCount)
	{
		auto centre = [&](int i, float& cx, float& cy, float& rr) {
			cx = 0.5f * (float)(spots[i].x0 + spots[i].x1);
			cy = 0.5f * (float)(spots[i].y0 + spots[i].y1);
			rr = 0.5f * (float)std::max(spots[i].x1 - spots[i].x0 + 1, spots[i].y1 - spots[i].y0 + 1) + gsF();
		};
		auto nearestTrack = [&](float cx, float cy, float rr, float pk, float& dOut) {
			int best = -1;
			float bestD = 1e9f;
			for (int k = 0; k < kMaxTracks; k++) {
				const Track& t = m_tracks[k];
				if (t.alpha <= 0.0f) continue;
				const float d = std::hypot(t.cx + t.vx - cx, t.cy + t.vy - cy);
				// 2.42.3: a patch that lost its light while moving is only picked
				// up by a light like the one it lost: close to where it was
				// heading, as bright and about the same size. After a cut the
				// brightness test missed, the old shot's patch was picked up by
				// small dimmer lamps of the new shot and slid across it.
				if (t.lm && (d > t.r + gsF() + 0.5f * std::hypot(t.vx, t.vy)
					|| pk < t.lpk - kRefindPeak || rr < 0.5f * t.r || rr > 2.0f * t.r)) continue;
				if (d <= Reach(t) && d < bestD) { bestD = d; best = k; }
			}
			dOut = bestD;
			return best;
		};
		// Which patch each spot belongs to, and each patch's nearest spot.
		static int spotTrack[kMaxSpots * 4];
		int primary[kMaxTracks], cnt[kMaxTracks];
		float primD[kMaxTracks], needMax[kMaxTracks];
		for (int k = 0; k < kMaxTracks; k++) { primary[k] = -1; cnt[k] = 0; primD[k] = 1e9f; needMax[k] = 0.0f; }
		const int nS = std::min(nSpots, (int)std::size(spotTrack));
		for (int i = 0; i < nS; i++) {
			float cx, cy, rr, d;
			centre(i, cx, cy, rr);
			const int k = nearestTrack(cx, cy, rr, spots[i].pk, d);
			spotTrack[i] = k;
			if (k >= 0) {
				cnt[k]++;
				if (d < primD[k]) { primD[k] = d; primary[k] = i; }
			}
		}
		// Another light under the same patch: the patch reaches out to it.
		auto cover = [&](int k, float cx, float cy, float rr) {
			Track& t = m_tracks[k];
			needMax[k] = std::max(needMax[k], std::hypot(t.cx - cx, t.cy - cy) + rr);
			t.miss = 0;
			t.fail = 0;
			t.lamp = 0; t.lm = 0;
			t.alpha = 1.0f;
		};
		for (int i = 0; i < nS; i++) {
			float cx, cy, rr;
			centre(i, cx, cy, rr);
			int best = spotTrack[i];
			if (best < 0) {
				// Not near any patch of the last frame; one started by an earlier
				// spot of this frame may be next to it.
				float d;
				best = nearestTrack(cx, cy, rr, spots[i].pk, d);
				if (best >= 0) {
					cover(best, cx, cy, rr);
					cnt[best]++;
					m_used[best] = true;
					continue;
				}
			}
			if (best >= 0) {
				Track& t = m_tracks[best];
				if (primary[best] != i) {
					cover(best, cx, cy, rr);
					m_used[best] = true;
					continue;
				}
				const bool piece = rr < kHotPiece * t.r;
				if (!piece) {
					// Easing only ever increases while the light is tracked, so a large
					// light stays eased when its core shrinks (it turns away, the frame
					// peak drops); a new light starts afresh.
					t.ease += kEaseRate * (std::min(spots[i].ease, t.ease) - t.ease);
					const float pw = PeakWeight(spots[i].pk, spots[i].fl);
					// 2.41.1: the peak weight only rises while the light is tracked. A
					// light on the borderline (a moon peaking at 80-82% with a 13-22%
					// flat core) had its patch swing between 10% and 100% as the
					// reading wobbled; now it comes up once and stays.
					if (pw > t.pkw) t.pkw += kPeakRateUp * (pw - t.pkw);
					t.flw += kEaseRate * (FlatWeight(spots[i].fl) - t.flw);
					// Ring weight follows slowly both ways, so a flickering flame
					// lighting more or less of its surroundings can't pulse the patch.
					t.rgw += kEaseRate * (RingWeight(spots[i].rg) - t.rgw);
					t.lpk = spots[i].pk; t.rE = spots[i].ease; t.rP = pw; t.rF = FlatWeight(spots[i].fl); t.rR = RingWeight(spots[i].rg);
				}
				if (!piece) {
					// A group's patch that is not moving stays put when its nearest
					// light is a neighbour (its own was not found this frame).
					const bool still = std::hypot(t.vx, t.vy) < kMovingSpeed * gsF();
					if (t.grp > 0 && still && primD[best] > std::max(1.5f * gsF(), 0.5f * t.r)) {
						cover(best, cx, cy, rr);
					} else {
						Follow(t, cx, cy);            // position and speed follow the light
						t.r += ((rr > t.r) ? 0.30f : 0.05f) * (rr - t.r); // grows fast, shrinks slowly
					}
				}
				t.miss = 0;
				t.fail = 0;
				t.lamp = 0; t.lm = 0;
				t.alpha = 1.0f;
				m_used[best] = true;
				continue;
			}
			int count = 0, slot = -1;
			for (int k = 0; k < kMaxTracks; k++) {
				if (m_tracks[k].alpha > 0.0f) count++;
				else if (slot < 0) slot = k;
			}
			if (count >= maxCount || slot < 0) continue;
			if (!m_cut && StripRecent(cx, cy)) continue;   // a strip was here a moment ago
			Track& t = m_tracks[slot];
			t.cx = cx; t.cy = cy; t.r = rr; t.miss = 0; t.fail = 0; t.alpha = 1.0f; t.vx = 0.0f; t.vy = 0.0f; t.cov = 0.0f; t.grp = 0; t.seen = 1; t.pend = 0; t.age = 0; t.lamp = 0; t.lm = 0; t.lpk = spots[i].pk; t.id = ++m_nextId;
			t.ease = spots[i].ease;
			t.pkw = PeakWeight(spots[i].pk, spots[i].fl);
			t.flw = FlatWeight(spots[i].fl);
			t.rgw = RingWeight(spots[i].rg);
			t.rE = t.ease; t.rP = t.pkw; t.rF = t.flw; t.rR = t.rgw;
			// Touching the edge of the picture: the light is entering the frame.
			const bool atEdge = cx - rr <= (float)m_bx0 + 0.5f || cx + rr >= (float)m_bx1 - 0.5f
				|| cy - rr <= (float)m_by0 + 0.5f || cy + rr >= (float)m_by1 - 0.5f;
			if (m_cut) {
				t.fade = 1.0f;                     // scene cut: the change is hidden
			} else if (Confirm() > 0) {
				t.fade = atEdge ? kFadeEdge : 1.0f;   // shown only once confirmed (see kConfirm)
				t.pend = 1;
			} else {
				t.fade = atEdge ? kFadeEdge : kFadeStart;   // mid-scene: start strong, finish in a few frames
			}
			m_used[slot] = true;
		}
		// A patch with two or more lights under it is a group for the next two
		// seconds; the reach to lights no longer there shrinks back slowly.
		for (int k = 0; k < kMaxTracks; k++) {
			Track& t = m_tracks[k];
			if (t.alpha <= 0.0f) continue;
			const bool settled = t.seen >= kGroupSeen && std::hypot(t.vx, t.vy) < kMovingSpeed * gsF();
			if (cnt[k] >= 2 && (settled || t.grp > 0)) t.grp = kGroupFrames;
			else if (t.grp > 0) t.grp--;
			if (cnt[k] >= 1 && t.seen < 100000) t.seen++;   // frames its light was found
			// The reach grows fast to the farthest light under the patch and
			// shrinks back slowly once that light is gone.
			t.cov += ((needMax[k] > t.cov) ? 0.30f : 0.03f) * (needMax[k] - t.cov);
			if (t.cov < 0.05f) t.cov = 0.0f;
		}
	}

	// Hysteresis: a hot spot whose light now narrowly fails a check (reads
	// graded, or as a lamp) keeps its patch at full strength while that light is
	// still there, for up to kReleaseFrames frames in a row; after that it is
	// let go and fades out as usual. A graded light or a lamp never starts one.
	template <class SpotT>
	void HoldLost(const SpotT* g, int n, bool lamp = false, bool strip = false)
	{
		for (int i = 0; i < n; i++) {
			const float cx = 0.5f * (float)(g[i].x0 + g[i].x1);
			const float cy = 0.5f * (float)(g[i].y0 + g[i].y1);
			int best = -1;
			float bestD = 1e9f;
			for (int k = 0; k < kMaxTracks; k++) {
				const Track& t = m_tracks[k];
				if (t.alpha <= 0.0f || m_used[k]) continue;
				// 2.42.2: a patch that is moving, or lost its light while moving,
				// is not held by a dimmer reading it drifts over. After a cut the
				// brightness test missed, a moving patch from the old shot coasted
				// onto a glow in the new one and sat there at 80% (a dark blotch
				// beside the new shot's headlight). Only a real light picks it up.
				if (t.lm || std::hypot(t.vx, t.vy) >= kMovingSpeed * gsF()) continue;
				const float d = std::hypot(t.cx + t.vx - cx, t.cy + t.vy - cy);
				if (d <= Reach(t) && d < bestD) { bestD = d; best = k; }
			}
			if (best < 0) continue;
			Track& t = m_tracks[best];
			if (lamp && HP()) {
				// High Performance: a lamp reading eases the patch out, and a light
				// reading eases it back in, so a flame whose glow comes and goes
				// keeps one steady patch instead of full / let go / found again.
				if (t.pend || t.seen < kProbation) continue;
				// A lamp for Lead() frames in a row: it has become one, and the
				// patch ends where that began, on the frames not yet shown.
				if (++t.lamp >= kLampRun) { EndTrack(best, t.lamp, true); continue; }
				t.rgw += kEaseRate * (0.0f - t.rgw);
				if (t.rgw < kLampGone) { t = Track(); continue; }
				if (std::hypot(t.vx, t.vy) >= kMovingSpeed * gsF()) Follow(t, cx, cy);
				t.miss = 0;
				t.alpha = 1.0f;
				m_used[best] = true;
				continue;
			}
			if (strip && HP() && !t.pend && t.seen >= kProbation) {
				// High Performance: part of a strip for Lead() frames in a row
				// (counted together with lamp readings) ends the patch the same way.
				if (++t.lamp >= kLampRun) { EndTrack(best, t.lamp, true); continue; }
			}
			if (t.fail >= kReleaseFrames) continue;
			// Only a patch whose light was really found for kProbation frames is
			// held; one not yet confirmed must be found as a light itself. A tiny
			// glint reading light on one frame and graded on the next was
			// confirmed, and later revived, by its graded readings.
			if (t.pend || t.seen < kProbation) continue;
			if (std::hypot(t.vx, t.vy) >= kMovingSpeed * gsF()) {
				Follow(t, cx, cy);   // a moving hot spot's held patch moves with its light
			}
			t.fail++;
			t.miss = 0;
			t.alpha = 1.0f;
			m_used[best] = true;
		}
	}

	// Graded spot map: target 1 over each small graded highlight, fading to 0
	// over a soft edge; the map follows it slowly both ways (at once after a cut).
	template <class SpotT>
	void UpdateGrade(const SpotT* g, int ng, int nCells)
	{
		const int cx = (int)m_cx, cy = (int)m_cy;
		float* target = m_gradeTarget;
		std::fill(target, target + (size_t)cx * cy, 0.0f);
		m_gradeLive = 0;
		for (int i = 0; i < ng; i++) {
			if (nCells <= 0 || (float)g[i].core / (float)nCells > kGradeMaxFrac) continue;
			m_gradeLive++;
			const float gx = 0.5f * (float)(g[i].x0 + g[i].x1);
			const float gy = 0.5f * (float)(g[i].y0 + g[i].y1);
			const float r = 0.5f * (float)std::max(g[i].x1 - g[i].x0 + 1, g[i].y1 - g[i].y0 + 1) + gsF();
			const float rIn = r, rOut = 2.0f * r + 4.0f * gsF();
			const int xa = std::max(0, (int)std::floor(gx - rOut)), xb = std::min(cx - 1, (int)std::ceil(gx + rOut));
			const int ya = std::max(0, (int)std::floor(gy - rOut)), yb = std::min(cy - 1, (int)std::ceil(gy + rOut));
			for (int y = ya; y <= yb; y++) {
				for (int x = xa; x <= xb; x++) {
					const float dx = (float)x - gx, dy = (float)y - gy;
					float u = std::clamp((std::sqrt(dx * dx + dy * dy) - rIn) / (rOut - rIn), 0.0f, 1.0f);
					u = u * u * (3.0f - 2.0f * u);
					float& t = target[(size_t)y * cx + x];
					t = std::max(t, 1.0f - u);
				}
			}
		}
		const float rate = m_cut ? 1.0f : (HPFull() ? kGradeRateHP : kGradeRate);
		m_gradeMax = 0.0f;
		for (size_t i = 0; i < (size_t)cx * cy; i++) {
			m_grade[i] += rate * (target[i] - m_grade[i]);
			if (m_grade[i] < 0.002f) m_grade[i] = 0.0f;
			m_gradeMax = std::max(m_gradeMax, m_grade[i]);
		}
	}

	template <class SpotT>
	void UpdateTracks(const SpotT* hot, int nHot, const SpotT* graded, int nGraded,
		const SpotT* lamps, int nLamps, const SpotT* strips, int nStrips)
	{
		std::fill(std::begin(m_used), std::end(m_used), false);
		m_nNew = 0;
		// Patches still fading in take their next step.
		for (Track& t : m_tracks) {
			if (t.alpha > 0.0f && t.fade < 1.0f) t.fade = std::min(1.0f, t.fade + kFadeStepHot);
			if (t.alpha > 0.0f && t.pend) t.age++;
		}
		if (m_cut || m_stripMemCx != (int)m_cx || m_stripMemCy != (int)m_cy) {
			for (StripMem& s : m_stripMem) s.ttl = 0;
			m_stripMemCx = (int)m_cx; m_stripMemCy = (int)m_cy;
		}
		MatchSpots(hot, nHot, kMaxSpots);
		// Remember where strips are (after matching: this frame's strips and
		// lights are different areas anyway).
		for (StripMem& s : m_stripMem) if (s.ttl > 0) s.ttl--;
		for (int i = 0; i < nStrips; i++) {
			const float cx = 0.5f * (float)(strips[i].x0 + strips[i].x1);
			const float cy = 0.5f * (float)(strips[i].y0 + strips[i].y1);
			StripMem* slot = nullptr;
			for (StripMem& s : m_stripMem) {
				if (s.ttl > 0 && cx >= s.x0 && cx <= s.x1 && cy >= s.y0 && cy <= s.y1) { slot = &s; break; }
				if (s.ttl <= 0 && !slot) slot = &s;
			}
			if (!slot) continue;
			const float pad = gsF();
			const float x0 = (float)strips[i].x0 - pad, x1 = (float)strips[i].x1 + pad;
			const float y0 = (float)strips[i].y0 - pad, y1 = (float)strips[i].y1 + pad;
			if (slot->ttl > 0) {
				slot->x0 = std::min(slot->x0, x0); slot->x1 = std::max(slot->x1, x1);
				slot->y0 = std::min(slot->y0, y0); slot->y1 = std::max(slot->y1, y1);
			} else {
				slot->x0 = x0; slot->x1 = x1; slot->y0 = y0; slot->y1 = y1;
			}
			slot->ttl = kStripMemFrames;
		}
		HoldLost(graded, nGraded);
		HoldLost(lamps, nLamps, true);
		HoldLost(strips, nStrips, false, true);
		// Patches not found this frame are held, then fade out (at once after a cut).
		for (int k = 0; k < kMaxTracks; k++) {
			Track& t = m_tracks[k];
			if (t.alpha <= 0.0f || m_used[k]) continue;
			if (t.pend) {
				// High Performance: judged over kConfirmHP frames, so it may be
				// missed on a few of them.
				if (HP() && !m_cut && t.age + 1 - t.seen <= (HPFull() ? kPendMissHP : kPendMissLite)) { t.miss++; continue; }
				// Gone before it was confirmed: never shown.
				ErasePending(k, t.age);
				t = Track();
				continue;
			}
			// Not found: carry on in its direction while held, slowing down.
			if (std::hypot(t.vx, t.vy) >= kMovingSpeed * gsF()) {
				t.lm = 1;   // lost while moving: from here only a real light picks it up
				t.cx = std::clamp(t.cx + t.vx, 0.0f, (float)m_cx - 1.0f);
				t.cy = std::clamp(t.cy + t.vy, 0.0f, (float)m_cy - 1.0f);
			}
			t.vx *= 0.8f; t.vy *= 0.8f;
			if (HP()) {
				// High Performance: held over a gap of up to Lead() frames; a
				// light gone for longer (or lost at a cut) has its patch ended
				// where the light ended, on the frames not yet shown.
				t.miss++;
				// 2.41.2: at a cut the patch runs up to the cut. It was only lost
				// for a moment (under Lead() frames), and taking it back to the
				// last frame it was found on made the light step brighter for
				// the last frames of the shot.
				if (m_cut) { t = Track(); continue; }
				if (t.miss >= Lead()) EndTrack(k, t.miss, false);
				continue;
			}
			t.miss = m_cut ? std::max(t.miss + 1, kHoldFrames + 1) : t.miss + 1;
			if (t.miss > kHoldFrames) {
				t.alpha *= kTrackFadeOut;
				if (t.alpha < 0.02f) t.alpha = 0.0f;
			} else if (t.seen < kProbation && t.miss > 1) {
				t.alpha *= kYoungFadeOut;         // on probation: not held
				if (t.alpha < 0.02f) t.alpha = 0.0f;
			}
		}
		// A new light found on its confirmation frames too: its patch stays, and
		// PushSnap fades it in early where the look-ahead leaves room.
		for (int k = 0; k < kMaxTracks; k++) {
			Track& t = m_tracks[k];
			if (t.alpha <= 0.0f || !t.pend || t.age < Confirm()) continue;
			if (HP()) {
				// Judged: it starts with the weights the light has now.
				t.ease = std::min(t.ease, t.rE); t.pkw = std::max(t.pkw, t.rP); t.flw = t.rF; t.rgw = t.rR;
				for (int j = 0; j < t.age && j < m_snapCount; j++) {
					Track& e = m_snap[(m_snapHead + kMaxLead + 1 - j) % (kMaxLead + 1)].tracks[k];
					if (e.alpha <= 0.0f || !e.pend) continue;
					e.ease = t.ease; e.pkw = t.pkw; e.flw = t.flw; e.rgw = t.rgw;
				}
			}
			t.pend = 0;
			if (m_nNew < kMaxTracks) m_newSlots[m_nNew++] = k;
		}
		// Overlapping patches merge into the larger one.
		for (int a = 0; a < kMaxTracks; a++) {
			for (int b = a + 1; b < kMaxTracks; b++) {
				Track& ta = m_tracks[a];
				Track& tb = m_tracks[b];
				if (ta.alpha <= 0.0f || tb.alpha <= 0.0f) continue;
				if (std::hypot(ta.cx - tb.cx, ta.cy - tb.cy) >= std::max(ta.r, tb.r) + gsF()) continue;
				Track& keep = (ta.r >= tb.r) ? ta : tb;
				Track& drop = (ta.r >= tb.r) ? tb : ta;
				keep.alpha = std::max(keep.alpha, drop.alpha);
				keep.miss = std::min(keep.miss, drop.miss);
				if (drop.pend) ErasePending((int)(&drop - m_tracks), drop.age);
				else keep.pend = 0;
				drop.alpha = 0.0f;
				drop.pend = 0;
			}
		}
		m_tracksLive = 0;
		m_spotDrawn = 0;
		for (const Track& t : m_tracks) {
			if (t.alpha <= 0.0f) continue;
			if (t.miss <= kHoldFrames) m_tracksLive++;
			if (m_spotDrawn < kMaxSpots) {
				m_spotCenter[m_spotDrawn][0] = (t.cx + 0.5f) / (float)m_cx;
				m_spotCenter[m_spotDrawn][1] = (t.cy + 0.5f) / (float)m_cy;
				m_spotEase[m_spotDrawn] = t.ease * t.pkw * t.flw * t.rgw;
				m_spotDrawn++;
			}
		}
	}

	// ---- settings ------------------------------------------------------------------

	void CloseShared()
	{
		if (m_pShared) { UnmapViewOfFile((void*)m_pShared); m_pShared = nullptr; }
		if (m_hMap) { CloseHandle(m_hMap); m_hMap = nullptr; }
	}

	bool ExeAlive() const
	{
		return m_pShared && m_pShared->magic == kClipGuardMagic
			&& (DWORD)(GetTickCount() - m_pShared->exeTick) < 5000;
	}

	void PollSettings()
	{
		const ULONGLONG now = GetTickCount64();
		if (!m_pShared && (m_openTick == 0 || now - m_openTick >= 2000)) {
			m_openTick = now;
			HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kClipGuardMapName);
			if (h) {
				void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ClipGuardShared));
				if (p) {
					m_hMap = h;
					m_pShared = (volatile ClipGuardShared*)p;
				} else {
					CloseHandle(h);
				}
			}
		}

		if (ExeAlive()) {
			volatile ClipGuardShared* s = m_pShared;
			m_s.enabled     = s->enabled != 0;
			m_s.strength    = std::clamp((float)s->strength, 0.0f, 1.0f);
			m_s.triggerPct  = std::clamp((float)s->triggerPct, 0.2f, 100.0f);
			m_s.fullPct     = (s->fullPct > 0.0f) ? std::clamp((float)s->fullPct, 0.0f, 100.0f) : 0.25f * m_s.triggerPct;
			m_s.knee        = std::clamp((float)s->knee, 0.20f, 0.95f);
			m_s.view        = std::clamp((int)s->view, 0, 3);
			m_s.colorProtect = std::clamp((float)s->colorProtect, 0.0f, 1.0f);
			m_s.textSkipPct = std::clamp((float)s->textSkipPct, 0.0f, 100.0f);
			m_s.hotSpot     = std::clamp((float)s->hotSpot, 0.0f, 1.0f);
			m_s.hotBelow    = std::clamp((float)s->hotBelowPct, 0.0f, 100.0f);
			m_s.fixShape    = std::clamp((int)s->fixShape, 0, 1);
			m_s.gradedSpot  = std::clamp((float)s->gradedSpot, 0.0f, 1.0f);
			m_s.spotMask    = std::clamp((int)s->spotMask, 0, 1);
			m_s.dimFire     = s->dimFire != 0;
			m_s.upscaler    = std::clamp((int)s->upscaler, 0, 1);
			m_s.dither      = std::clamp((int)s->dither, 0, 1);
			m_s.projector   = (s->projector == 1 || s->projector == 3) ? (int)s->projector : 0;   // 2.60.0: 3 = Titan DBLE Low
			m_s.lut         = std::clamp((int)s->lut, 0, 2);
			m_s.lutOff      = s->lutOff != 0;
			if (s->edition != 1) m_s.upscaler = 0;   // Upscaler is ClipGuard+ only
			m_s.grid        = std::clamp((int)s->grid, 0, 1);
			m_s.plus        = s->edition == 1;
			m_s.kscape      = m_s.plus && s->kscape != 0;
			m_s.laSource    = m_s.plus ? std::clamp((int)s->kscape, 0, 3) : 0;
			m_s.lookAhead   = NormLookAhead((int)s->lookAhead);
			if (!m_s.plus) m_s.lookAhead = std::min(m_s.lookAhead, 6);   // High Performance is ClipGuard+ only
			m_s.overlayOn   = s->overlayOn != 0;
			m_s.infoOn      = s->infoOn != 0;
			m_s.pinned      = s->overlayPinned != 0;
			m_s.lastKeyTick = s->lastKeyTick;
			m_s.testBuild   = s->buildKind == 1;
			m_s.selected    = std::clamp((int)s->selected, 0, kRows - (s->edition == 1 ? 0 : 1) - 1);
			const uint32_t req = s->logRequest;
			if (req != 0 && req != m_logSeen) {
				m_logSeen = req;
				m_logWant = req;
			}
			return;
		}

		// No ClipGuard.exe: clipguard.ini next to the filter, re-read every two seconds.
		if (m_iniTick != 0 && now - m_iniTick < 2000) {
			return;
		}
		m_iniTick = now;
		auto readf = [&](const wchar_t* key, float def) -> float {
			wchar_t buf[64] = {};
			GetPrivateProfileStringW(L"ClipGuard", key, L"", buf, 64, m_iniPath.c_str());
			return buf[0] ? (float)_wtof(buf) : def;
		};
		m_s.enabled    = readf(L"Enabled", 0.0f) > 0.5f;
		m_s.strength   = std::clamp(readf(L"Strength", 0.35f), 0.0f, 1.0f);
		m_s.triggerPct = std::clamp(readf(L"FixStartPct", 10.0f), 0.2f, 100.0f);
		m_s.fullPct    = std::clamp(readf(L"FullEffectPct", 3.0f), 0.0f, 100.0f);
		m_s.knee       = std::clamp(readf(L"HighlightStart", 0.40f), 0.20f, 0.95f);
		m_s.view       = std::clamp((int)readf(L"View", 0.0f), 0, 3);
		m_s.colorProtect = std::clamp(readf(L"ColorProtect", 0.90f), 0.0f, 1.0f);
		m_s.textSkipPct = std::clamp(readf(L"TextSkipPct", 25.0f), 0.0f, 100.0f);
		m_s.hotSpot    = std::clamp(readf(L"HotSpot", 0.6f), 0.0f, 1.0f);
		m_s.hotBelow   = std::clamp(readf(L"HotSpotBelowPct", 3.0f), 0.0f, 100.0f);
		m_s.fixShape   = std::clamp((int)readf(L"FixShape", 1.0f), 0, 1);
		m_s.gradedSpot = std::clamp(readf(L"GradedSpot", 0.5f), 0.0f, 1.0f);
		m_s.spotMask   = std::clamp((int)readf(L"HotSpotMask", 1.0f), 0, 1);
		m_s.dimFire    = readf(L"DimFire", 0.0f) > 0.5f;
		m_s.upscaler   = std::clamp((int)readf(L"Upscaler", 0.0f), 0, 1);
		m_s.dither     = std::clamp((int)readf(L"Dither", 0.0f), 0, 1);
		{
			// 2.50.0: Projector 2 (Valerion, 2.48-2.49) became Universal with the Valerion LUT.
			const int pj = std::clamp((int)readf(L"Projector", 0.0f), 0, 3);
			m_s.projector = (pj == 1 || pj == 3) ? pj : 0;   // 2.51.2: Valerion (2) becomes Universal (Reactive) with the Valerion LUT; 2.60.0: 3 = Titan DBLE Low
			m_s.lut = std::clamp((int)readf(L"Lut", (float)(pj == 3 ? 1 : pj)), 0, 2);
		}
		m_s.grid       = std::clamp((int)readf(L"Grid", 1.0f), 0, 1);
		m_s.lookAhead  = NormLookAhead((int)readf(L"LookAhead", 4.0f));
		m_s.plus       = false;   // without ClipGuard.exe: ClipGuard (High Performance and Upscaler need ClipGuard+)
		m_s.kscape     = false;
		m_s.laSource   = 0;
		m_s.lookAhead  = std::min(m_s.lookAhead, 6);
		m_s.upscaler   = 0;
		m_s.overlayOn  = readf(L"Overlay", 0.0f) > 0.5f;
		m_s.infoOn     = readf(L"Info", 0.0f) > 0.5f;
		m_s.pinned     = m_s.overlayOn;
		m_s.selected   = 0;
	}

	// Hot spot test video: the end of each 30 s part shows a row of ten 40 x 40
	// grey (bit 1, signal 0.30) or black (bit 0) blocks along the bottom right of
	// the 1920 x 1080 frame: the code 10110010 and then the part number in two
	// bits. Seen on kMarkFrames frames in a row, it counts once; ClipGuard Test
	// then saves and sends the log (the last 30 s: that part).
	static constexpr int kMarkFrames = 4;
	template <class F>
	void DetectTestMarker(F sample)
	{
		static const int code[8] = { 1, 0, 1, 1, 0, 0, 1, 0 };
		int part = 0;
		bool ok = true;
		int bits[10];
		for (int i = 0; i < 10 && ok; i++) {
			const float fx = (1920.0f - 440.0f + 40.0f * (float)i + 20.0f) / 1920.0f;
			const float fy = (1080.0f - 60.0f) / 1080.0f;
			const float v = sample(fx, fy);
			if (v >= 0.18f && v <= 0.50f) bits[i] = 1;
			else if (v < 0.08f) bits[i] = 0;
			else ok = false;
		}
		if (ok) {
			for (int i = 0; i < 8; i++) if (bits[i] != code[i]) { ok = false; break; }
		}
		if (ok) part = bits[8] * 2 + bits[9];
		if (!ok || part == 0) {
			m_markRun = 0;
			m_markPart = 0;
			return;
		}
		if (part != m_markPart) { m_markPart = part; m_markRun = 0; }
		if (++m_markRun == kMarkFrames) {
			m_markSeq++;
			if (m_pShared) m_pShared->testMarker = (m_markSeq << 8) | (uint32_t)part;
		}
	}
	int      m_markPart = 0;   // test code part on screen now (0 = none)
	int      m_markRun = 0;    // frames in a row it has been seen
	uint32_t m_markSeq = 0;

	void WriteStatus()
	{
		if (!m_pShared) return;
		volatile ClipGuardShared* s = m_pShared;
		s->rendTick = GetTickCount();
		s->pictureBrightness = m_haveMeasure ? m_picture : -1.0f;
		s->sceneNits = m_sceneNits;
		s->amount = std::max(m_smooth, 0.0f);
		s->ceiling = m_ceil;
		s->kneeEff = m_kneeEff;
		s->outputPQ = m_outputPQ ? 1 : 0;
		s->srcMax = 0.0f;
		s->textPct = m_textPct;
		s->textSkipped = m_textSkipped ? 1 : 0;
		s->clipFrac = m_clipFrac;
		s->hotFrac = m_hotFrac;
		s->hotActive = m_hotActive ? m_spots : 0;
		s->bottomBarFrac = m_bottomBarFrac;
	}

	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pContext = nullptr;
	CComPtr<ID3D11PixelShader> m_pPSMeasure;
	CComPtr<ID3D11PixelShader> m_pPSMain;
	CComPtr<ID3D11Buffer> m_pCBMeasure;
	CComPtr<ID3D11Buffer> m_pCBMain;
	CComPtr<ID3D11Texture2D> m_pMeasureTex[2];
	CComPtr<ID3D11Texture2D> m_pStaging[2][kStaging];
	CComPtr<ID3D11Texture2D> m_pMeasureOut[2];           // the picture as sent (readout only)
	CComPtr<ID3D11Texture2D> m_pStagingOut[2][kStaging];
	UINT64 m_outFrame = 0;
	bool  m_haveAfter = false;
	float m_picAfter = 0.0f;
	float m_peakAfter = 0.0f;
	int   m_bx0 = 0, m_bx1 = -1, m_by0 = 0, m_by1 = -1;  // picture area of the last measurement
	int   m_stickY0 = (int)kMaxCellsY, m_stickY1 = -1;   // top and bottom rows of the stable picture area
	int   m_stickX0 = (int)kMaxCellsX, m_stickX1 = -1;   // and its left and right columns
	// The stable picture area (where the menu and the info banner go): the
	// largest area measured on at least kAreaMin of the last kAreaFrames frames.
	// It used to be the largest area ever seen, so one stray frame with
	// something in the black bars (a 16:9 logo before a scope film, a full
	// frame flash) put the banner in the bars for the rest of the session. Now a
	// stray frame doesn't count, and an old shape is forgotten after two
	// minutes; a dark shot whose edges read as bars still can't shrink it while
	// the full picture was seen in that time, so it doesn't hop about.
	// 2.56.0: 30 seconds, not two minutes. A studio logo in 16:9 before a scope
	// film kept the banner in the bars for two minutes after a new film; a dark
	// shot can now pull it in a little, which keeps it on the picture anyway.
	static constexpr int kAreaFrames = 720;    // 30 seconds at 24 fps
	static constexpr int kAreaMin = 24;
	int16_t  m_areaRing[kAreaFrames][4] = {};   // x0, x1, y0, y1 of each frame (x0 < 0: black frame)
	uint16_t m_areaHist[4][kMaxCellsX + 1] = {};
	int   m_areaHead = 0, m_areaCount = 0, m_areaValid = 0;
	int   m_areaCx = 0, m_areaCy = 0;
	void AreaClear()
	{
		memset(m_areaHist, 0, sizeof(m_areaHist));
		m_areaHead = m_areaCount = m_areaValid = 0;
		m_stickY0 = (int)kMaxCellsY; m_stickY1 = -1;
		m_stickX0 = (int)kMaxCellsX; m_stickX1 = -1;
	}
	void AreaPush(int x0, int x1, int y0, int y1)
	{
		if (m_areaCx != (int)m_cx || m_areaCy != (int)m_cy) {
			AreaClear();
			m_areaCx = (int)m_cx; m_areaCy = (int)m_cy;
		}
		const bool ok = x0 <= x1 && y0 <= y1 && x0 >= 0 && y0 >= 0 && x1 < (int)kMaxCellsX && y1 < (int)kMaxCellsY;
		if (m_areaCount == kAreaFrames) {
			const int16_t* o = m_areaRing[m_areaHead];
			if (o[0] >= 0) {
				for (int k = 0; k < 4; k++) m_areaHist[k][o[k]]--;
				m_areaValid--;
			}
		} else {
			m_areaCount++;
		}
		int16_t* e = m_areaRing[m_areaHead];
		if (ok) {
			e[0] = (int16_t)x0; e[1] = (int16_t)x1; e[2] = (int16_t)y0; e[3] = (int16_t)y1;
			for (int k = 0; k < 4; k++) m_areaHist[k][e[k]]++;
			m_areaValid++;
		} else {
			e[0] = -1;
		}
		m_areaHead = (m_areaHead + 1) % kAreaFrames;
		if (m_areaValid <= 0) return;   // nothing but black so far: keep what there is
		const int need = std::min(kAreaMin, m_areaValid);
		// The outermost edge reached on at least `need` frames.
		auto low = [&](int k, int n) { int c = 0; for (int v = 0; v < n; v++) { c += m_areaHist[k][v]; if (c >= need) return v; } return n - 1; };
		auto high = [&](int k, int n) { int c = 0; for (int v = n - 1; v >= 0; v--) { c += m_areaHist[k][v]; if (c >= need) return v; } return 0; };
		m_stickX0 = low(0, (int)kMaxCellsX);
		m_stickX1 = high(1, (int)kMaxCellsX);
		m_stickY0 = low(2, (int)kMaxCellsY);
		m_stickY1 = high(3, (int)kMaxCellsY);
	}
	static constexpr int kHist = 16;                     // incoming ADL and peak of the last frames (look-ahead up to 12)
	float m_histPic[kHist] = {};
	float m_histPeak[kHist] = {};
	int   m_histHead = 0;
	CD3D11Font m_Font;    // overlay: main text
	CD3D11Font m_FontS;   // small: headings, hints, cost
	CD3D11Font m_FontT;   // bold title
	CD3D11Font m_FontB;   // large bold: info banner badges
	CD3D11Font m_FontH;   // small bold: headings
	CD3D11Font m_FontL;   // info banner values
	CD3D11Font m_FontG;   // info banner group headings (bold)
	UINT m_fontH = 0;

	std::wstring m_iniPath;
	HANDLE m_hMap = nullptr;
	volatile ClipGuardShared* m_pShared = nullptr;
	ULONGLONG m_openTick = 0;
	ULONGLONG m_iniTick = 0;
	ClipGuardSettings m_s;

	UINT64 m_frame = 0;
	bool  m_haveMeasure = false;
	bool  m_cut = false;
	struct StripMem { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; int ttl = 0; };
	StripMem m_stripMem[kMaxStripMem];   // where strips were seen in the last kStripMemFrames frames
	int   m_stripMemCx = 0, m_stripMemCy = 0;
	// Takes an unconfirmed patch out of the queued frames it was put in (its
	// last `age` frames; none of them has been shown yet).
	void ErasePending(int slot, int age)
	{
		for (int j = 0; j < age && j < m_snapCount; j++) {
			Track& e = m_snap[(m_snapHead + kMaxLead + 1 - j) % (kMaxLead + 1)].tracks[slot];
			if (e.pend) e = Track();
		}
	}
	// High Performance: ends a patch whose light was last found `miss` frames
	// ago, on the queued frames after that one (none shown yet). fade: a
	// short fade-out first (a light that turned into a lamp); otherwise the
	// patch is gone with its light.
	void EndTrack(int slot, int miss, bool fade)
	{
		// The first of those frames that has not been shown yet (with a short
		// lead the earlier ones already have): the fade starts there.
		const int i0 = std::max(1, miss - Lead());
		for (int i = i0; i < miss; i++) {
			const int j = miss - 1 - i;
			if (j >= m_snapCount) continue;
			Track& e = m_snap[(m_snapHead + kMaxLead + 1 - j) % (kMaxLead + 1)].tracks[slot];
			if (fade && i - i0 < kEndSteps) e.alpha *= EndFade(i - i0 + 1);
			else e = Track();
		}
		m_tracks[slot] = Track();
	}
	bool StripRecent(float cx, float cy) const {
		for (const StripMem& s : m_stripMem) {
			if (s.ttl > 0 && cx >= s.x0 && cx <= s.x1 && cy >= s.y0 && cy <= s.y1) return true;
		}
		return false;
	}
	int   m_grid = 0;
	UINT  m_cx = 128;
	UINT  m_cy = 72;
	bool  m_spotFresh = true;
	float m_lastHotSet = -1.0f;
	bool  m_outputPQ = false;
	float m_picture = 0.0f;
	float m_textPct = 0.0f;       // bright-on-black share of highlights (text, stars)
	bool  m_textSkipped = false;
	float m_brightSamples = 0.0f;
	float m_clipFrac = 0.0f;
	int   m_hotCells = 0;
	float m_hotFrac = 0.0f;
	float m_hotSmooth = 0.0f;
	bool  m_hotActive = false;
	int   m_spots = 0;
	int   m_stripPieces = 0;  // bright pieces of long lights (strips), not hot spots
	int   m_fireSpots = 0;  // lights left alone as fire (Dim fire off)
	float m_warmMax = -1.0f; // warmest glow of the lights checked this frame (-1 = none had a glow)
	float m_warmCore = 0.0f; // that light's core
	float m_fireSize = 0.0f; // 2.42.5: core size of the largest warm light
	float m_warmLow = -1.0f; // palest glow among the warm lights
	int   m_litSpots = 0;   // bright cores turned down as hot spots (lit surroundings)
	int   m_gradedSpots = 0; // bright cores turned down as hot spots (graded, not flat: skin)
	bool  m_spread = false;  // more lights than kSpreadLights, or over kHotMaxFrac
	bool  m_spreadArea = false; // spread because the lights' cores were over kHotMaxFrac
	int   m_spreadLights = 0;   // lights that passed the checks (readout)
	float m_hotFlat = -1.0f; // flat share of the flattest candidate core (readout)
	bool m_gateShut = false; // 2.42.4: picture above Hot spot below, patches go fast
	float m_hotGate = 1.0f;  // Hot spot below: 1 = full, 0 = picture too bright for hot spots
	float m_hotNear = -1.0f; // highest near share of the lit ring around a light that passed (readout)
	float m_hotRing = -1.0f; // highest lit share of the ring around a light that passed (readout)
	float m_framePeak = 0.0f;
	float m_spotScale = 1.0f;
	float m_spotCenter[kMaxSpots][2] = {};
	float m_spotEase[kMaxSpots] = {};
	int   m_spotDrawn = 0;
	float m_largest = 0.0f;   // core share of the picture of the largest light (readout)
	int   m_largeSpots = 0;   // lights being eased as large (readout)
	Track m_tracks[kMaxTracks];
	bool  m_used[kMaxTracks] = {};
	int   m_tracksLive = 0;
	float m_mask[kMaxCellsX * kMaxCellsY * 4] = {};
	float m_anchorMap[kMaxCellsX * kMaxCellsY] = {};  // large bright areas for Fix shape Anchor (follows quickly)
	float m_anchorGain[kMaxCellsX * kMaxCellsY];        // anchor gain per cell (1 = untouched)
	float m_anchorLv = 1.0f;                           // anchor level: top kAnchorPct of picture cells
	float m_grade[kMaxCellsX * kMaxCellsY] = {};   // graded spot map (follows slowly)
	float m_gradeTarget[kMaxCellsX * kMaxCellsY] = {};   // this frame's target for it
	float m_shownGradeMax = 0.0f;
	float m_targetRaw = 0.0f;
	bool  m_cutFrame = false;
	float m_tgVal[kTgHist] = {};   // each frame's own Guard amount and scene start,
	bool  m_tgCut[kTgHist] = {};   // newest at m_tgHead (same order as the queue)
	int   m_tgHead = 0, m_tgCount = 0;
	int   m_nextId = 0;
	float m_gradeCeil = 1.0f;
	float m_gradeMax = 0.0f;
	int   m_gradeLive = 0;
	int   m_newSlots[kMaxTracks] = {};
	int   m_nNew = 0;
	Snap  m_snap[kMaxLead + 1];
	int   m_snapHead = 0;
	int   m_snapCount = 0;
	int   m_snapLead = 0;
	int   m_snapGrid = 0;
	static constexpr int kQuerySets = 4;
	CComPtr<ID3D11Query> m_qDisjoint[kQuerySets], m_qT0[kQuerySets], m_qT1[kQuerySets];
	bool   m_qPending[kQuerySets] = {};
	bool   m_qOpen = false;
	int    m_qIdx = 0;
	double m_cpuT0 = 0.0, m_cpuFrameMs = 0.0, m_lastFrameT = 0.0, m_costT0 = 0.0;
	double m_cpuSum = 0.0, m_cpuMax = 0.0, m_frameSum = 0.0, m_gpuSum = 0.0, m_gpuMax = 0.0;
	int    m_cpuN = 0, m_gpuN = 0;
	double m_costCpuAvg = 0.0, m_costCpuMax = 0.0, m_costFrame = 0.0, m_costGpuAvg = -1.0, m_costGpuMax = 0.0;
	CComPtr<ID3D11Texture2D> m_pMaskTex[2];
	CComPtr<ID3D11ShaderResourceView> m_pMaskSRV[2];
	CComPtr<ID3D11SamplerState> m_pMaskSampler;
	float m_bottomBarFrac = 0.0f;
	float m_sceneNits = -1.0f;
	float m_smooth = -1.0f;
	float m_ceil = 1.0f;
	float m_kneeEff = 0.30f;
};
