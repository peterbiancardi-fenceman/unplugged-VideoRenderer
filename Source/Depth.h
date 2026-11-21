/*
 * ClipGuard 2.93.0: "Depth" -- a picture enhancement in the spirit of perceptual
 * depth processors: local brightness contrast in three bands, on brightness only.
 *
 *   Detail : fine texture (skin, hair, fabric), the picture minus a small blur
 *   Edges  : object outlines, the small blur minus a medium one
 *   Depth  : the wide layer, the medium blur minus a large one that is twice as wide
 *            as it is tall (the sideways weighting of a stereo pair's two views)
 *
 * Automatic guards, no settings:
 *   - texture is only lifted near structure, so grain in flat areas stays as it is,
 *     and the smallest differences (grain level) are cored away
 *   - an in-focus map: sharp areas get the effect, soft backgrounds less of it
 *   - Detail and Edges never go past the brightest / darkest neighbour (no halos)
 *   - tapers off near black (no hollow shadows) and, for brightening, near white
 *     (it can't add clipping for ClipGuard to fight)
 *   - colour kept: the change is in brightness, saturation follows it
 *
 * Runs after tone mapping and the ML grade, on the frame ClipGuard is about to show
 * (after its look-ahead), so a change shows on the next redraw, paused too.
 */
#pragma once

static const char kDepthLumaHLSL[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
cbuffer DepthLuma : register(b0) { float4 Lw; };
struct PS_INPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD; };
float4 main(PS_INPUT input) : SV_Target
{
	const float3 c = tex.Sample(samp, input.Tex).rgb;
	return float4(dot(max(c, 0.0), Lw.rgb), 0.0, 0.0, 1.0);
}
)HLSL";

// 9 taps, sigma 2 taps; Step = the tap spacing in UV (one direction)
static const char kDepthBlurHLSL[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
cbuffer DepthBlur : register(b0) { float4 Step; };
struct PS_INPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD; };
float4 main(PS_INPUT input) : SV_Target
{
	static const float w[5] = { 0.20417, 0.18018, 0.12383, 0.06629, 0.02763 };
	float4 s = tex.Sample(samp, input.Tex) * w[0];
	[unroll] for (int i = 1; i < 5; i++) {
		s += tex.Sample(samp, input.Tex + Step.xy * i) * w[i];
		s += tex.Sample(samp, input.Tex - Step.xy * i) * w[i];
	}
	return s;
}
)HLSL";

// Quarter size: R = local fine energy |L0 - G1|, G = local structure |G1 - G2|
static const char kDepthEnergyHLSL[] = R"HLSL(
Texture2D L0 : register(t0);
SamplerState samp : register(s0);
Texture2D G1 : register(t1);
Texture2D G2 : register(t2);
SamplerState lin : register(s1);
cbuffer DepthEnergy : register(b0) { float4 Half; };
struct PS_INPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD; };
float4 main(PS_INPUT input) : SV_Target
{
	float ef = 0.0, em = 0.0;
	[unroll] for (int j = 0; j < 4; j++) {
		const float2 o = float2((j & 1) ? 0.5 : -0.5, (j & 2) ? 0.5 : -0.5) * Half.xy;
		const float2 uv = input.Tex + o;
		const float l0 = L0.Sample(lin, uv).r, g1 = G1.Sample(lin, uv).r, g2 = G2.Sample(lin, uv).r;
		ef += abs(l0 - g1);
		em += abs(g1 - g2);
	}
	return float4(ef * 0.25, em * 0.25, 0.0, 1.0);
}
)HLSL";

static const char kDepthMainHLSL[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
Texture2D G1 : register(t1);
Texture2D G2 : register(t2);
Texture2D G3 : register(t3);
Texture2D E  : register(t4);
Texture2D L0 : register(t5);
SamplerState lin : register(s1);
cbuffer DepthMain : register(b0)
{
	float4 Gain;   // detail, edges, depth, split view (1 = left half untouched)
	float4 Map;    // the picture's rect in the input's UV: offset x, y, scale x, y
	float4 Px;     // input texel size x, y; grain floor; 0
	float4 Lw;     // luma weights; 0
	float4 Lt;     // half-size luma texel x, y (times the source spacing); 0; 0
};
struct PS_INPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD; };
float4 main(PS_INPUT input) : SV_Target
{
	const float2 uv = input.Tex;
	const float3 c = tex.Sample(samp, uv).rgb;
	const float2 r = (uv - Map.xy) / Map.zw;
	if (Gain.w > 0.5 && r.x < 0.5) {
		// split view: a thin line at the middle, the left half as it was
		if (r.x > 0.5 - 1.5 * Px.x / Map.z) return float4(0.6, 0.6, 0.6, 1.0);
		return float4(c, 1.0);
	}
	const float Y = dot(max(c, 0.0), Lw.rgb);
	const float g1 = G1.Sample(lin, r).r, g2 = G2.Sample(lin, r).r, g3 = G3.Sample(lin, r).r;
	const float2 e = E.Sample(lin, r).rg;
	const float fl = Px.z;

	// texture only near structure (grain in flat areas has fine energy but no structure)
	const float structure = smoothstep(fl * 1.5, fl * 6.0, e.y);
	// in focus: fine energy that keeps up with the structure around it
	const float focus = smoothstep(0.10, 0.35, e.x / (e.y + 0.003));

	const float fine = Y - g1;
	const float fineC = sign(fine) * max(abs(fine) - fl, 0.0);   // grain-level differences cored away
	float dLocal = Gain.x * fineC * structure * lerp(0.3, 1.0, focus) + Gain.y * (g1 - g2) * lerp(0.4, 1.0, focus);
	float dWide = Gain.z * (g2 - g3) * lerp(0.6, 1.0, focus);

	// no halos: the local part stays within the range of the area around it (3 x 3 on the
	// half-size picture, so a few pixels each way, the size of the Detail / Edges layers),
	// plus a margin
	float lo = Y, hi = Y;
	[unroll] for (int j = -1; j <= 1; j++) {
		[unroll] for (int i = -1; i <= 1; i++) {
			const float v = L0.Sample(lin, r + float2(i, j) * Lt.xy).r;
			lo = min(lo, v); hi = max(hi, v);
		}
	}
	dLocal = clamp(Y + dLocal, lo - 0.03, hi + 0.03) - Y;
	dWide = 0.06 * tanh(dWide / 0.06);

	float d = dLocal + dWide;
	d *= smoothstep(0.012, 0.07, Y);                     // near black: left alone
	if (d > 0.0) d *= 1.0 - smoothstep(0.82, 0.97, Y);   // near white: never brighter
	d = 0.10 * tanh(d / 0.10);

	const float Yn = max(Y + d, 0.0);
	const float k = clamp((Yn + 0.03) / (Y + 0.03), 0.85, 1.2);   // saturation follows the brightness
	return float4(max(Yn + (c - Y) * k, 0.0), 1.0);
}
)HLSL";
