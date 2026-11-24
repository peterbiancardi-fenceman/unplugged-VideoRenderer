/*
 * ClipGuard 2.107.0: Detail and Grain for low-bitrate films.
 *
 *   Detail : the finest texture (1-3 pixels), the part an encoder trims first, raised.
 *            The picture minus a 3x3 blur, on brightness only. Guards, no settings:
 *            - the smallest differences are cored away (noise the encoder left)
 *            - flat areas (skies, walls) get none, strong edges are held back (no halos),
 *              and the result never goes past the brightest / darkest neighbour
 *            - tapers off near black, and for brightening near white (no new clipping)
 *            - colour kept: the change is in brightness
 *   Grain  : fine single-pixel grain, new every frame, mostly in the midtones; black and
 *            the highlights stay clean. Added after Detail, so Detail never sharpens it.
 *
 * Runs on the frame about to be shown, before Depth (ClipBee), so Depth works on the
 * restored texture. Pixel coordinates: the target rect is the source rect.
 */
#pragma once

static const char kBitrateHLSL[] = R"HLSL(
Texture2D tex : register(t0);
cbuffer Bitrate : register(b0) {
	float4 P;     // x = detail gain, y = grain amplitude, z = frame seed, w = core
	float4 R;     // rect: left, top, right - 1, bottom - 1 (pixels)
	float4 Lw;    // luma weights
};
struct PS_INPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD; };
float3 px(int2 p) { return tex.Load(int3(clamp(p, int2(R.xy), int2(R.zw)), 0)).rgb; }
float hash(float2 p) { p = frac(p * float2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return frac((p.x + p.y) * p.x); }
float4 main(PS_INPUT input) : SV_Target
{
	const int2 c = int2(input.Pos.xy);
	const float3 rgb = px(c);
	const float l = dot(max(rgb, 0.0), Lw.rgb);
	float outL = l;

	if (P.x > 0.0) {
		float blur = 0.0, mn = l, mx = l;
		[unroll] for (int j = -1; j <= 1; j++) {
			[unroll] for (int i = -1; i <= 1; i++) {
				const float v = dot(max(px(c + int2(i, j)), 0.0), Lw.rgb);
				blur += v * (2 - abs(i)) * (2 - abs(j));   // 1 2 1 / 2 4 2 / 1 2 1
				mn = min(mn, v); mx = max(mx, v);
			}
		}
		blur /= 16.0;
		const float f = l - blur;
		const float range = mx - mn;
		float g = P.x * smoothstep(P.w, P.w * 2.5, abs(f));       // coring
		g *= smoothstep(0.0015, 0.006, range);                   // 2.108.0: truly flat areas only (soft texture is lifted)
		g *= 1.0 - smoothstep(0.10, 0.30, range);                // strong edges: held back
		g *= smoothstep(0.004, 0.03, l);                         // 2.108.0: near black only (dark scenes lifted)
		float d = f * g;
		if (d > 0.0) d *= 1.0 - smoothstep(0.80, 0.95, l);       // near white: no brightening
		outL = clamp(l + d, mn, mx);
	}

	float3 o = rgb * (l > 1e-5 ? outL / l : 1.0);

	if (P.y > 0.0) {
		const float2 s = float2(c) + P.z * float2(17.13, 31.71);
		const float n = (hash(s) + hash(s + 71.7) - 1.0);              // triangular, -1..1
		const float w = smoothstep(0.02, 0.15, outL) * (1.0 - smoothstep(0.70, 0.95, outL));
		o += n * P.y * w;
	}
	return float4(o, 1.0);
}
)HLSL";
