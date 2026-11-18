/*
 * ClipGuard 2.25.4 (formerly DBLEFix) -- highlight compensation for projectors with dynamic laser dimming
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
#include <d3dcommon.h>
#include "D3DUtil/D3D11Font.h"
#include "ClipGuardSettings.h"

// Defined in Shaders.cpp (runtime HLSL compile via d3dcompiler_47.dll).
HRESULT CompileShader(const std::string& srcCode, const D3D_SHADER_MACRO* pDefines, LPCSTR pTarget, ID3DBlob** ppCode);

// ---- shared memory with ClipGuard.exe ----------------------------------------
// Layout must match ClipGuard.cs (StructLayout Sequential, Pack = 4). 256 bytes.
#pragma pack(push, 4)
struct DbleShared
{
	uint32_t magic;          // kDbleMagic
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
	int32_t  lookAhead;         // frames the picture is held back: 0, 2, 4 or 6 (1 = 2, from 2.12.0)
	// log (F11): ClipGuard.exe asks, the renderer saves, ClipGuard.exe uploads
	uint32_t logRequest;        // written by ClipGuard.exe: id of the latest log asked for (0 = none)
	uint32_t logDone;           // written by the renderer: id of the last log saved
	int32_t  logState;          // written by ClipGuard.exe: 0 none, 1 saving, 2 sending, 3 sent, 4 failed, 5 no token
	uint32_t logStateTick;      // written by ClipGuard.exe: GetTickCount() when logState changed
	// written by ClipGuard.exe
	float    hotBelowPct;       // "Hot spot below": no hot spot in pictures brighter than this % (0 = off)
	float    gradedSpot;        // "Graded spot" 0..1: gentle local highlight dimming of small graded highlights (was Grain until 2.22.0)
	int32_t  buildKind;         // written by ClipGuard.exe: 0 = public build, 1 = test build (auto-update, log upload)
	uint32_t reserved[23];
};
#pragma pack(pop)
static_assert(sizeof(DbleShared) == 256, "DbleShared must stay 256 bytes (ClipGuard.cs mirrors it)");

constexpr uint32_t kDbleMagic   = 0x454C4244; // "DBLE"
constexpr wchar_t  kDbleMapName[] = L"Local\\ClipGuard_v1";

struct DbleSettings
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
	int   grid = 1;
	int   lookAhead = 4;  // frames: 0, 2, 4, 6
	int   view       = 0;
	bool  overlayOn  = false;
	bool  pinned     = false;
	bool  testBuild  = false;  // ClipGuard Test: Restart & update, logs sent
	int   selected   = 0;
	DWORD lastKeyTick = 0;
};

// ---- shaders (compiled at device creation with d3dcompiler_47) --------------

static const char kDbleMeasureHLSL[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
Texture2D srcTex : register(t1);
SamplerState sampSrc : register(s1);

cbuffer DbleMeasure : register(b0)
{
	float2 CellUV;
	float2 pad0;
	float4 SrcMap;
	float  HasSource;
	float  SrcIsPQ;
	float  MKnee;
	float  pad1;
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

float PqToNits(float e)
{
	const float m1 = 0.1593017578125;
	const float m2 = 78.84375;
	const float c1 = 0.8359375;
	const float c2 = 18.8515625;
	const float c3 = 18.6875;
	float p = pow(saturate(e), 1.0 / m2);
	return 10000.0 * pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

float SourceLight(float3 s)
{
	float m = max(s.r, max(s.g, s.b));
	return (SrcIsPQ > 0.5) ? PqToNits(m) : 100.0 * pow(saturate(m), 2.2);
}

// Per cell: R = mean linear luminance, G = brightest signal,
// B = highlight samples in a bright-on-black cell, A = highlight samples.
// A highlight is a sample above Highlight start. A cell is bright-on-black when
// it holds highlights AND near-black (at least 8 of 64 samples), with almost
// nothing in between: text, credits and stars. The inside of a lamp, a window
// or a lit wall has no near-black, and its edges have a glow of in-between
// levels, so those cells are not.
// A also carries, as 100 x n, the samples within 0.01 of the cell's brightest
// (a flat core: a bulb, a headlamp; skin and lit surfaces are graded).
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
				if (m > MKnee) bright += 1.0;
				else if (m > 0.08) mid += 1.0;
				else dark += 1.0;
			}
		}
		if (mid <= 4.0 && dark >= 8.0) textN = bright;
	}
	return float4(sumL / 64.0, maxS, textN, bright + 100.0 * flat);
}
)HLSL";

static const char kDbleMainHLSL[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
Texture2D srcTex : register(t1);
SamplerState sampSrc : register(s1);
Texture2D spotMask : register(t2);
SamplerState sampMask : register(s2);

cbuffer DbleMain : register(b0)
{
	float Ceil;
	float Knee;
	float Bypass;
	float View;
	float4 SrcMap;
	float HasSource;
	float SrcIsPQ;
	float SrcFrameMax;
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
float3 Fix(float3 c, float wRaw, float wGrade, float wAnchor, float gAnchor)
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
		// Hot spots: a soft patch over each small, very bright spot is exposed
		// darker instead of squeezed. Everything in it (the core, its falloff, the
		// glow) is scaled by the same factor, so the light keeps its shape and the
		// core stays the brightest point; the patch fades out to the normal fix.
		// Deep shadows in the patch are kept: the exposure cut eases in from
		// black up to Highlight start.
		const float w = wRaw * SpotFade;
		if (w > 0.001) {
			const float lift = smoothstep(0.0, max(Knee, 0.2), Max3(c));
			o = lerp(o, c * lerp(1.0, SpotScale, lift), saturate(w));
		}
	}
	return o;
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
		return float4(Fix(s.rgb, sm.r, sm.g, sm.b, sm.a), s.a);
	}

	// Hot spot map and pixel size, read here (outside any branch) for the
	// test view.
	const float4 wm = spotMask.SampleLevel(sampMask, mu, 0);
	const float wRaw = wm.r;
	const float wEdge = max(fwidth(wRaw), 1e-4);
	const float2 px = max(float2(abs(ddx(input.Tex.x)), abs(ddy(input.Tex.y))), 1e-7);

	const float3 o = Fix(c.rgb, wRaw, wm.g, wm.b, wm.a);

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
struct DbleMeasureCB
{
	float cellU, cellV, pad0, pad1;
	float srcOffX, srcOffY, srcScX, srcScY;
	float hasSource, srcIsPQ, knee, pad3;
};
struct DbleMainCB
{
	float ceil, knee, bypass, view;
	float srcOffX, srcOffY, srcScX, srcScY;
	float hasSource, srcIsPQ, srcFrameMax, texW;
	float colorProtect, spotScale, spotFade, spotOn;
	float spotCenter[8][4];
	float spotCount, anchor, gradeCeil, gradeOn;
	float spotEase[4][4];
};
static_assert(sizeof(DbleMeasureCB) == 48 && sizeof(DbleMainCB) == 272, "DBLE cbuffer size");

class CDbleFix
{
public:
	// Measuring grid: Normal 128x72 cells, Fine 256x144.
	static constexpr UINT  kMaxCellsX = 256;
	static constexpr UINT  kMaxCellsY = 144;
	static constexpr UINT  kStaging = 3;
	static constexpr DWORD kOverlayHideMs = 8000;
	static constexpr int   kRows = 16;
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
	// spotlight) has no detail to save: dimming it only turns it grey. Such a
	// light is eased too, by how flat its core is (kClipFlatStart..Full) times
	// how big it is (kClipSizeStart..Full), so small clipped bulbs are still
	// dimmed. Streetlights read up to about 50% flat, headlights about 10%.
	static constexpr float kClipFlatStart = 0.55f;
	static constexpr float kClipFlatFull = 0.75f;
	static constexpr float kClipSizeStart = 0.001f;
	static constexpr float kClipSizeFull = 0.003f;
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
	// A hot spot that turns graded or a lamp drains its dimming; below this much it is let go.
	static constexpr float kDrainDone = 0.02f;
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
	static constexpr int   kMaxTracks = kMaxSpots;
	// Spot tracking: a spot found inside an existing patch moves that patch
	// (eased) instead of starting a new one; overlapping patches merge. A spot
	// not found keeps its patch for kHoldFrames measurements, then fades out
	// (at once after a scene cut).
	static constexpr int   kHoldFrames = 15;
	// Scene cut: picture brightness changes by this factor or more.
	static constexpr float kCutRatio = 4.0f;
	// While a light is tracked, its dimming follows the scene's darkness this
	// slowly (per frame; about 2 seconds to settle at 24 fps).
	static constexpr float kLockRate = 0.02f;
	// A new spot mid-scene starts at kFadeStart of its dimming and reaches the full
	// amount in a few frames. At a scene cut it is full at
	// once. With look-ahead beyond 2 frames the fade is started early instead, so
	// it is complete on the light's first frame.
	static constexpr float kFadeStart = 0.5f;
	static constexpr float kFadeStepHot = 0.25f;   // 0.50, 0.75, 1.00
	static constexpr float kTrackFadeOut = 0.85f;  // per frame once a lost spot's hold ends
	static constexpr int   kMaxLead = 4;           // look-ahead 6 = 4 frames of lead

	~CDbleFix() { ReleaseDevice(); CloseShared(); }

	// ---- device ----------------------------------------------------------------

	HRESULT InitDevice(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, const std::wstring& moduleDir)
	{
		ReleaseDevice();
		m_pDevice = pDevice;
		m_pContext = pContext;
		m_iniPath = moduleDir + L"\\clipguard.ini";

		HRESULT hr = CreatePS(kDbleMeasureHLSL, sizeof(kDbleMeasureHLSL) - 1, &m_pPSMeasure);
		if (SUCCEEDED(hr)) hr = CreatePS(kDbleMainHLSL, sizeof(kDbleMainHLSL) - 1, &m_pPSMain);
		if (SUCCEEDED(hr)) {
			D3D11_BUFFER_DESC bd = { sizeof(DbleMeasureCB), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
			hr = pDevice->CreateBuffer(&bd, nullptr, &m_pCBMeasure);
			if (SUCCEEDED(hr)) {
				bd.ByteWidth = sizeof(DbleMainCB);
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
			m_maskDirty = true;
		}
		if (SUCCEEDED(hr)) {
			hr = m_Font.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontS.InitDeviceObjects(pDevice, pContext);
			if (SUCCEEDED(hr)) hr = m_FontT.InitDeviceObjects(pDevice, pContext);
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
		m_fontH = 0;
		m_pDevice = nullptr;
		m_pContext = nullptr;
	}

	bool Ready() const { return m_pPSMain && m_pPSMeasure && m_pCBMain && m_pCBMeasure && m_pMeasureTex[1] && m_pStaging[1][kStaging - 1] && m_pMaskSRV[1] && m_pMaskSampler; }
	UINT CellsX() const { return m_cx; }
	UINT CellsY() const { return m_cy; }
	int LookAheadFrames() const { return m_s.enabled ? m_s.lookAhead : 0; }
	float PicturePct() const { return m_picture; }
	float FixAmount() const { return std::max(m_smooth, 0.0f); }
	float WhiteCeil() const { return m_ceil; }
	bool  HotActive() const { return m_hotActive; }
	void RequestLog(unsigned id) { if (id) m_logWant = id; }
	// Frames the shown picture is behind the newest measurement (look-ahead beyond 2).
	int Lead() const { return std::max(0, LookAheadFrames() - 2); }

	ID3D11PixelShader* PSMeasure() const { return m_pPSMeasure; }
	ID3D11PixelShader* PSMain() const { return m_pPSMain; }
	ID3D11Buffer* CBMeasure() const { return m_pCBMeasure; }
	ID3D11Buffer* CBMain() const { return m_pCBMain; }
	ID3D11Texture2D* MeasureTex() const { return m_pMeasureTex[m_grid]; }
	ID3D11Texture2D* MeasureOutTex() const { return m_pMeasureOut[m_grid]; }
	CD3D11Font& Font() { return m_Font; }

	// Native Unplugged integration: the application owns the settings and pushes
	// them through Settings_t. No shared memory or clipguard.ini is consulted once
	// this has been called.
	void SetNativeSettings(const ClipGuardSettings& s)
	{
		m_native = true;
		m_s.enabled      = s.enabled;
		m_s.strength     = std::clamp(s.strength, 0.0f, 1.0f);
		m_s.fixShape     = std::clamp(s.fixShape, 0, 1);
		m_s.triggerPct   = std::clamp(s.fixStartPct, 1.0f, 50.0f);
		m_s.fullPct      = std::clamp(s.fullPct, 0.5f, 49.5f);
		if (m_s.fullPct > m_s.triggerPct - 0.5f) m_s.fullPct = m_s.triggerPct - 0.5f;
		m_s.knee         = std::clamp(s.knee, 0.20f, 0.95f);
		m_s.colorProtect = std::clamp(s.colorProtect, 0.0f, 1.0f);
		m_s.textSkipPct  = std::clamp(s.textSkipPct, 0.0f, 100.0f);
		m_s.hotSpot      = std::clamp(s.hotSpot, 0.0f, 1.0f);
		m_s.hotBelow     = std::clamp(s.hotBelowPct, 0.0f, 20.0f);
		m_s.gradedSpot   = std::clamp(s.gradedSpot, 0.0f, 1.0f);
		m_s.grid         = std::clamp(s.grid, 0, 1);
		m_s.lookAhead    = (s.lookAhead == 2 || s.lookAhead == 4 || s.lookAhead == 6) ? s.lookAhead : 0;
		m_s.view         = std::clamp(s.view, 0, 3);
		m_s.overlayOn    = s.osd;
		m_s.pinned       = s.osdPinned;
		m_s.selected     = std::clamp(s.osdSelected, 0, kRows - 1);
	}

	// ---- per frame ---------------------------------------------------------------

	// Settings, the latest finished measurement, and the fix amount for this frame.
	void BeginFrame(bool outputPQ, float sceneNits)
	{
		CpuFrame();
		CpuStart();
		if (!m_native) PollSettings();
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
			m_maskDirty = true;
			m_outFrame = 0;
			m_haveAfter = false;
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
			m_hotGate = g;
		}
		const bool hot = dark > 0.0f && m_s.hotSpot > 0.0f && m_hotGate > 0.0f && m_tracksLive >= 1;
		const float hotTarget = hot ? 1.0f : 0.0f; // the patch fades in and out
		{
			const float a = (hotTarget > m_hotSmooth) ? 1.0f : 0.10f; // on at once (patches fade in), out slowly
			m_hotSmooth = a * hotTarget + (1.0f - a) * m_hotSmooth;
		}
		const bool cut = m_cut;
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
			m_spotScale = (m_hotSmooth < 0.05f || m_spotFresh || cut || m_s.hotSpot != m_lastHotSet) ? want : kLockRate * want + (1.0f - kLockRate) * m_spotScale;
		}
		m_spotFresh = !hot;
		m_lastHotSet = m_s.hotSpot;   // a changed setting applies at once
		m_hotActive = m_hotSmooth > 0.02f && m_spotScale < 0.999f;

		PushSnap(hot);
		WriteStatus();
		LogFrame();
		CpuStop();
	}

	// ---- look-ahead state ------------------------------------------------------------
	// The state worked out from each measured frame is queued, and the main pass
	// uses the one that belongs to the frame being shown. With look-ahead beyond
	// 2 frames the queue also lets a new spot's patch be faded in on the frames
	// before the light appears.

	struct Track { float cx = 0, cy = 0, r = 0; int miss = 0; float alpha = 0; float fade = 1.0f; float ease = 1.0f; float pkw = 1.0f; float flw = 1.0f; float rgw = 1.0f; };

	struct Snap
	{
		float ceil = 1.0f, knee = 0.3f, spotScale = 1.0f, hotSmooth = 0.0f, peak = 1.0f;
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
	};

	void PushSnap(bool hot)
	{
		const int lead = Lead();
		if (lead != m_snapLead || m_grid != m_snapGrid) {
			m_snapLead = lead;
			m_snapGrid = m_grid;
			m_snapCount = 0;
		}
		m_snapHead = (m_snapHead + 1) % (kMaxLead + 1);
		Snap& sn = m_snap[m_snapHead];
		sn.ceil = m_ceil; sn.knee = m_kneeEff; sn.peak = m_anchorLv;
		sn.spotScale = m_spotScale; sn.hotSmooth = m_hotSmooth;
		memcpy(sn.spotCenter, m_spotCenter, sizeof(sn.spotCenter));
		memcpy(sn.spotEase, m_spotEase, sizeof(sn.spotEase));
		sn.spotDrawn = m_spotDrawn;
		sn.grid = m_grid;
		sn.gradeCeil = m_gradeCeil;
		{
			const size_t nc = (size_t)m_cx * m_cy;
			memcpy(sn.grade, m_grade, nc * sizeof(float));
			memcpy(sn.anchorMap, m_anchorMap, nc * sizeof(float));
			memcpy(sn.anchorGain, m_anchorGain, nc * sizeof(float));
		}
		for (int k = 0; k < kMaxTracks; k++) {
			sn.tracks[k] = m_tracks[k];
			sn.tracks[kMaxTracks + k] = Track();
		}
		m_snapCount = std::min(m_snapCount + 1, kMaxLead + 1);
		// New spots: fade their patch in on the queued frames before this one.
		for (int i = 0; i < m_nNew; i++) {
			const Track& t = m_tracks[m_newSlots[i]];
			if (!hot) continue;
			for (int k = 1; k <= lead && k < m_snapCount; k++) {
				Snap& e = m_snap[(m_snapHead + kMaxLead + 1 - k) % (kMaxLead + 1)];
				for (int j = kMaxTracks; j < 2 * kMaxTracks; j++) {
					if (e.tracks[j].alpha > 0.0f) continue;
					e.tracks[j] = t;
					e.tracks[j].fade = 1.0f - (float)k / (float)(lead + 1);
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
		for (const Track& t : sn.tracks) {
			if (t.alpha <= 0.0f) continue;
			const float rIn = t.r;
			const float rOut = 2.0f * t.r + 4.0f * gsF();
			const float wt = t.alpha * t.fade * t.ease * t.pkw * t.flw * t.rgw;
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

	void FillMeasureCB(float cellU, float cellV, float offX, float offY, float scX, float scY, bool hasSource, bool srcIsPQ)
	{
		DbleMeasureCB cb = { cellU, cellV, 0, 0, offX, offY, scX, scY, hasSource ? 1.0f : 0.0f, srcIsPQ ? 1.0f : 0.0f,
			std::clamp(m_s.knee, 0.20f, 0.95f), 0 };
		m_pContext->UpdateSubresource(m_pCBMeasure, 0, nullptr, &cb, 0, 0);
	}

	void FillMainCB(bool bypass, float offX, float offY, float scX, float scY, bool hasSource, bool srcIsPQ, float texW)
	{
		CpuStart();
		const Snap& sn = ShownSnap();
		DbleMainCB cb = {};
		cb.ceil = sn.ceil;
		cb.knee = sn.knee;
		cb.bypass = bypass ? 1.0f : 0.0f;
		cb.view = (float)std::clamp(m_s.view, 0, 3);
		cb.srcOffX = offX; cb.srcOffY = offY; cb.srcScX = scX; cb.srcScY = scY;
		cb.hasSource = hasSource ? 1.0f : 0.0f;
		cb.srcIsPQ = srcIsPQ ? 1.0f : 0.0f;
		cb.srcFrameMax = m_srcMax;
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

	bool OverlayVisible() const
	{
		if (!m_s.overlayOn) return false;
		if (m_s.pinned) return true;
		return (DWORD)(GetTickCount() - m_s.lastKeyTick) < kOverlayHideMs;
	}

	float BottomBarFrac() const { return m_bottomBarFrac; }
	// Top letterbox bar, fraction of the video height (0 when unknown).
	float TopBarFrac() const { return (m_haveMeasure && m_by0 <= m_by1) ? (float)m_by0 / (float)m_cy : 0.0f; }
	int View() const { return m_s.view; }
	// Split screen and Side by side change the picture the projector gets, so
	// its dimming reacts differently: a note is shown above the picture.
	bool ShowViewNote() const { return !m_outputPQ && (m_s.view == 2 || m_s.view == 3); }

	// Fonts follow the window: the main one about 1/120 of its height (the
	// whole overlay scales with it), a small one for headings, keys and the
	// cost line, and a bold title.
	void EnsureFont(int windowH)
	{
		const UINT h = (UINT)std::max(11, windowH / 120);
		if (h != m_fontH
			&& SUCCEEDED(m_Font.CreateFontBitmap(L"Segoe UI", h, 0))
			&& SUCCEEDED(m_FontS.CreateFontBitmap(L"Segoe UI", std::max(9u, h * 4 / 5), 0))
			&& SUCCEEDED(m_FontT.CreateFontBitmap(L"Segoe UI", h * 3 / 2, D3DFONT_BOLD))) {
			m_fontH = h;
		}
	}
	UINT FontH() const { return m_fontH; }
	CD3D11Font& FontS() { return m_FontS; }
	CD3D11Font& FontT() { return m_FontT; }

	// The overlay as data: rows (headings and settings) and readout lines.
	// style: 0 = normal, 1 = blue (values, what is active), 2 = dim, 3 = yellow (Off, Skipped)
	struct OvRow { bool heading; std::wstring name, value; bool selected; int style; };
	struct OvLine { std::wstring text; int style; };
	struct OvKey { std::wstring key, what; };
	struct OvModel { std::wstring version; std::vector<OvRow> rows; std::vector<OvKey> keys; std::vector<OvLine> lines; };

	OvModel Overlay() const
	{
		OvModel m;
		m.version = m_s.testBuild ? L"2.25.4 Test" : L"2.25.4";
		const auto vals = Lines();
		static const wchar_t* names[kRows] = { L"Fix", L"Strength", L"Fix shape", L"Fix starts", L"Full effect", L"Highlight start", L"Color protect", L"Text skip", L"Hot spot", L"Hot spot below", L"Graded spot", L"Grid", L"Look-ahead", L"View", L"Overlay", L"Restart & update" };
		for (int i = 0; i < kRows; i++) {
			if (i == 0) m.rows.push_back({ true, L"FIX", L"", false, 2 });
			if (i == 8) m.rows.push_back({ true, L"SPOTS", L"", false, 2 });
			if (i == 11) m.rows.push_back({ true, L"DISPLAY", L"", false, 2 });
			std::wstring v = vals[i];
			int st = (v == L"Off") ? 3 : (i == kRows - 1 ? 2 : 1);
			const bool sel = (i == m_s.selected);
			if (sel) v = L"< " + v + L" >";
			m.rows.push_back({ false, (i == kRows - 1 && !m_s.testBuild) ? L"Restart" : names[i], v, sel, st });
		}
		m.keys = {
			{ L"Up / Down", L"select" }, { L"Left / Right", L"change" },
			{ L"F7", L"show / hide menu" }, { L"F8", L"fix on / off" },
			{ L"F9", L"fix areas view" }, { L"F10", L"hot spot on / off" },
			{ L"F11", m_s.testBuild ? L"send log" : L"save log" } };
		if (m_s.testBuild) m.keys.push_back({ L"Ctrl+Alt+U", L"restart & update" });
		m.lines = Readout();
		return m;
	}

	// The settings' values, as shown.
	std::vector<std::wstring> Lines() const
	{
		static const wchar_t* views[] = { L"Normal", L"Fix areas", L"Split screen", L"Side by side" };
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
		if (m_s.gradedSpot > 0.0f) swprintf_s(v[10], L"%.2f", m_s.gradedSpot);
		else swprintf_s(v[10], L"Off");
		swprintf_s(v[11], L"%ls", m_s.grid ? L"Fine" : L"Normal");
		if (m_s.lookAhead > 0) swprintf_s(v[12], L"%d frames", m_s.lookAhead);
		else swprintf_s(v[12], L"Off");
		swprintf_s(v[13], L"%ls", views[std::clamp(m_s.view, 0, 3)]);
		swprintf_s(v[14], L"%ls", m_s.pinned ? L"Always" : L"Auto-hide");
		swprintf_s(v[15], L"Press Right");
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
			out.push_back({ L"HDR output: the fix needs SDR output", 3 });
		} else if (!m_haveMeasure) {
			out.push_back({ L"Measuring...", 2 });
		} else {
			wchar_t text[32];
			if (m_brightSamples >= 16.0f) swprintf_s(text, L"%.0f%%", m_textPct);
			else swprintf_s(text, L"-");
			swprintf_s(st, L"Picture %.1f%%   Text %ls%ls   Fix %.0f%%   White %.0f%%   Peak %.0f%%",
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
		switch (st) {
		case 1: return L"Log: saving...";
		case 2: return L"Log: sending...";
		case 3: return L"Log: sent";
		case 5: return L"Log: saved, not sent (no GitHub token in clipguard.ini)";
		case 6: return L"Log: saved to %LOCALAPPDATA%\\ClipGuard\\logs";
		default: return L"Log: saved, sending failed (kept in %LOCALAPPDATA%\\ClipGuard\\logs)";
		}
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
			dir += L"\\Unplugged";
			CreateDirectoryW(dir.c_str(), nullptr);
			dir += L"\\logs";
			CreateDirectoryW(dir.c_str(), nullptr);
			dir += L"\\clipguard";
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
	static constexpr int kLogSpots = 4;
	struct LogRec {
		double t;
		float picture, peak, fix, white, hotFrac, flat, ring, nearSh, largest, text, sceneNits;
		int spots, lights, lamps, graded, hotActive, spread, skipped;
		float gradeMax, gradeCeil;
		float gate;
		int nSpot;
		LogSpot spot[kLogSpots];
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
		r.spots = m_spots; r.lights = m_spreadLights; r.lamps = m_litSpots; r.gate = m_hotGate;
		r.graded = m_gradedSpots; r.gradeMax = m_gradeMax; r.gradeCeil = m_gradeCeil; r.hotActive = m_hotActive ? 1 : 0;
		r.spread = m_spread ? 1 : 0; r.skipped = m_textSkipped ? 1 : 0;
		r.nSpot = m_logSpotN;
		for (int i = 0; i < kLogSpots; i++) r.spot[i] = m_logSpot[i];
		m_logHead = (m_logHead + 1) % kLogRecs;
		m_logCount = std::min(m_logCount + 1, kLogRecs);
	}

	void WriteLogCsv(const std::wstring& path)
	{
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
		fprintf(f, "# ClipGuard 2.25.4 log, last 30 s\n");
		fprintf(f, "# settings: fix=%d strength=%.2f fixStarts=%.1f fullEffect=%.1f highlightStart=%.2f colorProtect=%.2f textSkip=%.0f hotSpot=%.2f hotSpotBelow=%.1f fixShape=%d gradedSpot=%.2f grid=%d lookAhead=%d view=%d\n",
			m_s.enabled ? 1 : 0, m_s.strength, m_s.triggerPct, m_s.fullPct, m_s.knee, m_s.colorProtect, m_s.textSkipPct,
			m_s.hotSpot, m_s.hotBelow, m_s.fixShape, m_s.gradedSpot, m_s.grid, m_s.lookAhead, m_s.view);
		fprintf(f, "# limits: ring %.2f-%.2f flat %.2f-%.2f hot spot full up to %.2f x Hot spot below\n", kHotRingFull, kHotLitRing, kHotFlatMin, kHotFlatFull, kHotBelowFull);
		fprintf(f, "t_s,picture,peak,fix,white,text,hot_pct,spots,lights,lamps,gate,graded,spread,dimmed,flat,ring,near,largest,skipped,grade_map,grade_white");
		for (int i = 0; i < kLogSpots; i++) fprintf(f, ",s%d_x,s%d_y,s%d_size,s%d_peak,s%d_flat,s%d_ring,s%d_near,s%d_ease", i, i, i, i, i, i, i, i);
		fprintf(f, "\n");
		const double now = NowMs();
		for (int k = m_logCount; k >= 1; k--) {
			const LogRec& r = m_log[(m_logHead - k + kLogRecs) % kLogRecs];
			const double age = (now - r.t) / 1000.0;
			if (age > 30.0) continue;
			fprintf(f, "%.3f,%.2f,%.3f,%.3f,%.3f,%.1f,%.2f,%d,%d,%d,%.3f,%d,%d,%d,%.3f,%.3f,%.3f,%.4f,%d,%.3f,%.3f",
				-age, r.picture, r.peak, r.fix, r.white, r.text, r.hotFrac * 100.0f, r.spots, r.lights, r.lamps, r.gate,
				r.graded, r.spread, r.hotActive, r.flat, r.ring, r.nearSh, r.largest, r.skipped, r.gradeMax, r.gradeCeil);
			for (int i = 0; i < kLogSpots; i++) {
				if (i < r.nSpot) {
					const LogSpot& sp = r.spot[i];
					fprintf(f, ",%.3f,%.3f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f", sp.x, sp.y, sp.size, sp.pk, sp.fl, sp.rg, sp.nr, sp.ease);
				} else {
					fprintf(f, ",,,,,,,,");
				}
			}
			fprintf(f, "\n");
		}
		fclose(f);
	}

	// The window as shown, 24-bit BMP, halved while wider than 1920.
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
		return 6;
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
			return (ch == 3) ? std::fmod(v, 100.0f) : v;
		};
		auto flatN = [&](UINT x, UINT y) { return std::floor(cellRaw(x, y, 3) / 100.0f + 0.001f); };
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
		struct Spot { int x0, y0, x1, y1; float lv; int core; float pk; float ease; float fl; float rg; float nr; };  // lv, core, pk: a hot spot's core level, cells and peak; ease: 1 = full dimming; fl: how flat its core is; rg, nr: lit and near share of its ring
		Spot spots[kMaxSpots];
		static Spot cands[kMaxCands];
		int nCands = 0;
		int nSpots = 0;
		int litSpots = 0;
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
		if (y0 <= y1 && x0 <= x1) {
			for (int y = y0; y <= y1; y++) {
				for (int x = x0; x <= x1; x++) {
					sum += cell(x, y, 0);
					textSamples += cell(x, y, 2);
					brightSamples += cell(x, y, 3);
					const float peak = cell(x, y, 1);
					if (peak >= 0.995f) clipped++;
					framePeak = std::max(framePeak, peak);
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
				static uint8_t seen[kMaxCellsY][kMaxCellsX];
				static int stack[kMaxCellsX * kMaxCellsY];
				static int members[kMaxCellsX * kMaxCellsY];
				memset(seen, 0, sizeof(seen));
				for (int y = y0; y <= y1; y++) {
					for (int x = x0; x <= x1; x++) {
						if (seen[y][x] || cell(x, y, 1) < grp) continue;
						int top = 0, nm = 0;
						float gp = 0.0f;
						stack[top++] = y * cxN + x;
						seen[y][x] = 1;
						while (top > 0) {
							const int i = stack[--top];
							const int cx = i % cxN, cy = i / cxN;
							members[nm++] = i;
							gp = std::max(gp, cell(cx, cy, 1));
							// Reach: touching cells on Normal; up to 2 cells on Fine (the same
							// distance), so a light with a darker gap inside stays one light.
							for (int dy = -gs; dy <= gs; dy++) {
								for (int dx = -gs; dx <= gs; dx++) {
									const int nx = cx + dx, ny = cy + dy;
									if (nx < x0 || nx > x1 || ny < y0 || ny > y1 || seen[ny][nx]) continue;
									if (cell(nx, ny, 1) < grp) continue;
									seen[ny][nx] = 1;
									stack[top++] = ny * cxN + nx;
								}
							}
						}
						if (gp < kHotMinPeak) continue;
						Spot sp = { cxN, cyN, -1, -1, gp - kHotMargin };
						int core = 0;
						for (int k = 0; k < nm; k++) {
							const int cx = members[k] % cxN, cy = members[k] / cxN;
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
					spots[i].ease = std::min(bySize, byClip);
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
					static uint8_t seenG[kMaxCellsY][kMaxCellsX];
					static int stackG[kMaxCellsX * kMaxCellsY];
					static int membersG[kMaxCellsX * kMaxCellsY];
					memset(seenG, 0, sizeof(seenG));
					for (int y = y0; y <= y1 && nGradeLow < kMaxSpots; y++) {
						for (int x = x0; x <= x1 && nGradeLow < kMaxSpots; x++) {
							if (seenG[y][x] || cell(x, y, 1) < grp) continue;
							int top = 0, nm = 0;
							float gp = 0.0f;
							stackG[top++] = y * cxN + x;
							seenG[y][x] = 1;
							while (top > 0) {
								const int i = stackG[--top];
								const int cx = i % cxN, cy = i / cxN;
								membersG[nm++] = i;
								gp = std::max(gp, cell(cx, cy, 1));
								for (int dy = -gs; dy <= gs; dy++) {
									for (int dx = -gs; dx <= gs; dx++) {
										const int nx = cx + dx, ny = cy + dy;
										if (nx < x0 || nx > x1 || ny < y0 || ny > y1 || seenG[ny][nx]) continue;
										if (cell(nx, ny, 1) < grp) continue;
										seenG[ny][nx] = 1;
										stackG[top++] = ny * cxN + nx;
									}
								}
							}
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
				static uint8_t seenA[kMaxCellsY][kMaxCellsX];
				static int stackA[kMaxCellsX * kMaxCellsY];
				static int membersA[kMaxCellsX * kMaxCellsY];
				std::fill(target, target + (size_t)cxN * cyN, 0.0f);
				memset(seenA, 0, sizeof(seenA));
				const float grp = std::max(m_anchorLv - kAnchorGroup, std::clamp(m_s.knee, 0.20f, 0.95f));
				const int minCells = std::max(1, (int)(kAnchorMinFrac * (float)n));
				for (int y = y0; y <= y1; y++) {
					for (int x = x0; x <= x1; x++) {
						if (seenA[y][x] || cell(x, y, 1) < grp) continue;
						int top = 0, nm = 0;
						stackA[top++] = y * cxN + x;
						seenA[y][x] = 1;
						while (top > 0) {
							const int i = stackA[--top];
							const int cx = i % cxN, cy = i / cxN;
							membersA[nm++] = i;
							for (int dy = -gs; dy <= gs; dy++) {
								for (int dx = -gs; dx <= gs; dx++) {
									const int nx = cx + dx, ny = cy + dy;
									if (nx < x0 || nx > x1 || ny < y0 || ny > y1 || seenA[ny][nx]) continue;
									if (cell(nx, ny, 1) < grp) continue;
									seenA[ny][nx] = 1;
									stackA[top++] = ny * cxN + nx;
								}
							}
						}
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
		m_histHead = (m_histHead + 1) % kHist;
		m_histPic[m_histHead] = pic;
		m_histPeak[m_histHead] = framePeak;
		m_brightSamples = (float)brightSamples;
		m_textPct = brightSamples > 0.0 ? (float)(textSamples / brightSamples * 100.0) : 0.0f;
		m_clipFrac = n ? (float)clipped / (float)n : 0.0f;
		m_hotCells = hotCells;
		m_spots = nSpots;
		m_litSpots = litSpots;
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
		// The lights kept, for the log (F11).
		m_logSpotN = 0;
		for (int i = 0; i < nSpots && m_logSpotN < kLogSpots; i++) {
			LogSpot& ls = m_logSpot[m_logSpotN++];
			ls.x = (0.5f * (float)(spots[i].x0 + spots[i].x1) + 0.5f) / (float)cxN;
			ls.y = (0.5f * (float)(spots[i].y0 + spots[i].y1) + 0.5f) / (float)cyN;
			ls.size = n > 0 ? (float)spots[i].core / (float)n : 0.0f;
			ls.pk = spots[i].pk; ls.fl = spots[i].fl; ls.rg = spots[i].rg; ls.nr = spots[i].nr; ls.ease = spots[i].ease;
		}
		UpdateTracks(spots, nSpots, gradedList, spread ? 0 : nGraded, lampList, spread ? 0 : nLamp);
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

	// A spot found inside an existing patch moves that patch; otherwise it
	// starts a new one.
	template <class SpotT>
	void MatchSpots(const SpotT* spots, int nSpots, int maxCount)
	{
		for (int i = 0; i < nSpots; i++) {
			const float cx = 0.5f * (float)(spots[i].x0 + spots[i].x1);
			const float cy = 0.5f * (float)(spots[i].y0 + spots[i].y1);
			const float rr = 0.5f * (float)std::max(spots[i].x1 - spots[i].x0 + 1, spots[i].y1 - spots[i].y0 + 1) + gsF();
			int best = -1;
			float bestD = 1e9f;
			for (int k = 0; k < kMaxTracks; k++) {
				const Track& t = m_tracks[k];
				if (t.alpha <= 0.0f) continue;
				const float d = std::hypot(t.cx - cx, t.cy - cy);
				if (d <= 2.0f * t.r + 4.0f * gsF() && d < bestD) { bestD = d; best = k; }
			}
			if (best >= 0) {
				if (m_used[best]) continue; // another part of the same light
				Track& t = m_tracks[best];
				const bool piece = rr < kHotPiece * t.r;
				if (!piece) {
					// Easing only ever increases while the light is tracked, so a large
					// light stays eased when its core shrinks (it turns away, the frame
					// peak drops); a new light starts afresh.
					t.ease += kEaseRate * (std::min(spots[i].ease, t.ease) - t.ease);
					const float pw = PeakWeight(spots[i].pk, spots[i].fl);
					t.pkw += ((pw > t.pkw) ? kPeakRateUp : kEaseRate) * (pw - t.pkw);
					t.flw += kEaseRate * (FlatWeight(spots[i].fl) - t.flw);
					// Ring weight follows slowly both ways, so a flickering flame
					// lighting more or less of its surroundings can't pulse the patch.
					t.rgw += kEaseRate * (RingWeight(spots[i].rg) - t.rgw);
				}
				if (!piece) {
					t.cx += 0.5f * (cx - t.cx);   // position follows quickly (moving lights)
					t.cy += 0.5f * (cy - t.cy);
					t.r += ((rr > t.r) ? 0.30f : 0.05f) * (rr - t.r); // grows fast, shrinks slowly
				}
				t.miss = 0;
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
			Track& t = m_tracks[slot];
			t.cx = cx; t.cy = cy; t.r = rr; t.miss = 0; t.alpha = 1.0f;
			t.ease = spots[i].ease;
			t.pkw = PeakWeight(spots[i].pk, spots[i].fl);
			t.flw = FlatWeight(spots[i].fl);
			t.rgw = RingWeight(spots[i].rg);
			if (m_cut) {
				t.fade = 1.0f;                     // scene cut: the change is hidden
			} else if (Lead() > 0) {
				t.fade = 1.0f;                     // faded in early on the frames before
				if (m_nNew < kMaxTracks) m_newSlots[m_nNew++] = slot;
			} else {
				t.fade = kFadeStart;               // mid-scene: start strong, finish in a few frames
			}
			m_used[slot] = true;
		}
	}

	// A hot spot whose light now reads graded (its flat core cut off by the
	// frame edge, or out of focus) or as a lamp (its surroundings lit past
	// kHotLitRing) is not dropped: its patch stays where it is and its dimming
	// drains (flatness or ring weight, about a second), instead of being held
	// and then faded out in a few frames. Only a patch that is already there;
	// a graded light or a lamp never starts one. Once drained, the patch is let
	// go as usual.
	template <class SpotT>
	void DrainLost(const SpotT* g, int n, bool lamp)
	{
		for (int i = 0; i < n; i++) {
			const float cx = 0.5f * (float)(g[i].x0 + g[i].x1);
			const float cy = 0.5f * (float)(g[i].y0 + g[i].y1);
			int best = -1;
			float bestD = 1e9f;
			for (int k = 0; k < kMaxTracks; k++) {
				const Track& t = m_tracks[k];
				if (t.alpha <= 0.0f || m_used[k]) continue;
				const float d = std::hypot(t.cx - cx, t.cy - cy);
				if (d <= 2.0f * t.r + 4.0f * gsF() && d < bestD) { bestD = d; best = k; }
			}
			if (best < 0) continue;
			Track& t = m_tracks[best];
			float& w = lamp ? t.rgw : t.flw;
			if (w < kDrainDone) continue;
			w += kEaseRate * ((lamp ? RingWeight(g[i].rg) : FlatWeight(g[i].fl)) - w);
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
		static float target[kMaxCellsX * kMaxCellsY];
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
		const float rate = m_cut ? 1.0f : kGradeRate;
		m_gradeMax = 0.0f;
		for (size_t i = 0; i < (size_t)cx * cy; i++) {
			m_grade[i] += rate * (target[i] - m_grade[i]);
			if (m_grade[i] < 0.002f) m_grade[i] = 0.0f;
			m_gradeMax = std::max(m_gradeMax, m_grade[i]);
		}
	}

	template <class SpotT>
	void UpdateTracks(const SpotT* hot, int nHot, const SpotT* graded, int nGraded,
		const SpotT* lamps, int nLamps)
	{
		std::fill(std::begin(m_used), std::end(m_used), false);
		m_nNew = 0;
		// Patches still fading in take their next step.
		for (Track& t : m_tracks) {
			if (t.alpha > 0.0f && t.fade < 1.0f) t.fade = std::min(1.0f, t.fade + kFadeStepHot);
		}
		MatchSpots(hot, nHot, kMaxSpots);
		DrainLost(graded, nGraded, false);
		DrainLost(lamps, nLamps, true);
		// Patches not found this frame are held, then fade out (at once after a cut).
		for (int k = 0; k < kMaxTracks; k++) {
			Track& t = m_tracks[k];
			if (t.alpha <= 0.0f || m_used[k]) continue;
			t.miss = m_cut ? std::max(t.miss + 1, kHoldFrames + 1) : t.miss + 1;
			if (t.miss > kHoldFrames) {
				t.alpha *= kTrackFadeOut;
				if (t.alpha < 0.02f) t.alpha = 0.0f;
			}
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
				drop.alpha = 0.0f;
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
		return m_pShared && m_pShared->magic == kDbleMagic
			&& (DWORD)(GetTickCount() - m_pShared->exeTick) < 5000;
	}

	void PollSettings()
	{
		const ULONGLONG now = GetTickCount64();
		if (!m_pShared && (m_openTick == 0 || now - m_openTick >= 2000)) {
			m_openTick = now;
			HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kDbleMapName);
			if (h) {
				void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DbleShared));
				if (p) {
					m_hMap = h;
					m_pShared = (volatile DbleShared*)p;
				} else {
					CloseHandle(h);
				}
			}
		}

		if (ExeAlive()) {
			volatile DbleShared* s = m_pShared;
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
			m_s.grid        = std::clamp((int)s->grid, 0, 1);
			m_s.lookAhead   = NormLookAhead((int)s->lookAhead);
			m_s.overlayOn   = s->overlayOn != 0;
			m_s.pinned      = s->overlayPinned != 0;
			m_s.selected    = std::clamp((int)s->selected, 0, kRows - 1);
			m_s.lastKeyTick = s->lastKeyTick;
			m_s.testBuild   = s->buildKind == 1;
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
		m_s.grid       = std::clamp((int)readf(L"Grid", 1.0f), 0, 1);
		m_s.lookAhead  = NormLookAhead((int)readf(L"LookAhead", 4.0f));
		m_s.overlayOn  = readf(L"Overlay", 0.0f) > 0.5f;
		m_s.pinned     = m_s.overlayOn;
		m_s.selected   = 0;
	}

	void WriteStatus()
	{
		if (!m_pShared) return;
		volatile DbleShared* s = m_pShared;
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
	static constexpr int kHist = 8;                      // incoming ADL and peak of the last frames
	float m_histPic[kHist] = {};
	float m_histPeak[kHist] = {};
	int   m_histHead = 0;
	CD3D11Font m_Font;    // overlay: main text
	CD3D11Font m_FontS;   // small: headings, hints, cost
	CD3D11Font m_FontT;   // bold title
	UINT m_fontH = 0;

	std::wstring m_iniPath;
	HANDLE m_hMap = nullptr;
	volatile DbleShared* m_pShared = nullptr;
	bool m_native = false;   // Unplugged owns the settings; no shared memory/INI
	ULONGLONG m_openTick = 0;
	ULONGLONG m_iniTick = 0;
	DbleSettings m_s;

	UINT64 m_frame = 0;
	bool  m_haveMeasure = false;
	bool  m_cut = false;
	int   m_grid = 0;
	UINT  m_cx = 128;
	UINT  m_cy = 72;
	bool  m_spotFresh = true;
	float m_lastHotSet = -1.0f;
	bool  m_outputPQ = false;
	float m_picture = 0.0f;
	float m_srcMax = 0.0f;     // unused since 2.1
	float m_textPct = 0.0f;       // bright-on-black share of highlights (text, stars)
	bool  m_textSkipped = false;
	float m_brightSamples = 0.0f;
	float m_clipFrac = 0.0f;
	int   m_hotCells = 0;
	float m_hotFrac = 0.0f;
	float m_hotSmooth = 0.0f;
	bool  m_hotActive = false;
	int   m_spots = 0;
	int   m_litSpots = 0;   // bright cores turned down as hot spots (lit surroundings)
	int   m_gradedSpots = 0; // bright cores turned down as hot spots (graded, not flat: skin)
	bool  m_spread = false;  // more lights than kSpreadLights, or over kHotMaxFrac
	bool  m_spreadArea = false; // spread because the lights' cores were over kHotMaxFrac
	int   m_spreadLights = 0;   // lights that passed the checks (readout)
	float m_hotFlat = -1.0f; // flat share of the flattest candidate core (readout)
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
	float m_gradeCeil = 1.0f;
	float m_gradeMax = 0.0f;
	int   m_gradeLive = 0;
	bool  m_maskDirty = true;
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
