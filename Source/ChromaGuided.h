/*
 * ClipGuard 2.111.0: luma-guided chroma upsampling, ported from Artoriuz's mpv shaders
 * (MIT License, Copyright (c) 2023 João Chrisóstomo):
 *   JointBilateral  - https://github.com/Artoriuz/glsl-joint-bilateral (JointBilateral.glsl)
 *   CfL             - https://github.com/Artoriuz/glsl-chroma-from-luma-prediction
 *                     (CfL_Prediction.glsl, the full version, not Lite: Hermite-filtered
 *                     low-res luma guide, 12-tap + two 8-tap regressions, cubic spatial
 *                     filter with anti-ringing, and the final joint-bilateral smoothing)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions: The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 *
 * Passes (each a full-screen pixel shader; SV_POSITION gives the output pixel):
 *   LR1   chroma-w x luma-h : luma downscaled across (Hermite, +-1 chroma texel)
 *   LR2   chroma-w x chroma-h: then down (the luma guide at the chroma grid)
 *   JB    luma-w x luma-h    : JointBilateral upscale of the chroma        -> done
 *   CFL   luma-w x luma-h    : CfL prediction upscale of the chroma
 *   SMOOTH luma-w x luma-h   : CfL's 3x3 joint-bilateral smoothing         -> done
 * The converter then reads the finished chroma (t4) one-to-one with the luma.
 * Planes: t0 luma; t1 UV (biplanar) or t1/t2 U and V (planar, CG_SWAPUV for YV12);
 * t3 the previous pass's output.
 */
#pragma once

static const char kCgCommon[] = R"HLSL(
Texture2D tY : register(t0);
#if CG_PLANAR
Texture2D tP1 : register(t1);
Texture2D tP2 : register(t2);
float2 UVat(int2 p) {
	uint w, h; tP1.GetDimensions(w, h);
	p = clamp(p, int2(0, 0), int2(w, h) - 1);
	float a = tP1.Load(int3(p, 0)).r, b = tP2.Load(int3(p, 0)).r;
#if CG_SWAPUV
	return float2(b, a);
#else
	return float2(a, b);
#endif
}
void ChromaDims(out float2 c) { uint w, h; tP1.GetDimensions(w, h); c = float2(w, h); }
#else
Texture2D tUV : register(t1);
float2 UVat(int2 p) {
	uint w, h; tUV.GetDimensions(w, h);
	p = clamp(p, int2(0, 0), int2(w, h) - 1);
	return tUV.Load(int3(p, 0)).rg;
}
void ChromaDims(out float2 c) { uint w, h; tUV.GetDimensions(w, h); c = float2(w, h); }
#endif
Texture2D tIn : register(t3);
void LumaDims(out float2 l) { uint w, h; tY.GetDimensions(w, h); l = float2(w, h); }
float Yat(int2 p) { float2 l; LumaDims(l); p = clamp(p, int2(0, 0), int2(l) - 1); return tY.Load(int3(p, 0)).r; }
float LRat(int2 p) { uint w, h; tIn.GetDimensions(w, h); p = clamp(p, int2(0, 0), int2(w, h) - 1); return tIn.Load(int3(p, 0)).r; }
float2 INat(int2 p) { uint w, h; tIn.GetDimensions(w, h); p = clamp(p, int2(0, 0), int2(w, h) - 1); return tIn.Load(int3(p, 0)).rg; }
struct PS_INPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD; };
float comp_wd_hermite(float x) { x = min(abs(x), 1.0); return smoothstep(0.0, 1.0, 1.0 - x); }
float comp_w_bilateral(float2 sd, float id) { return max(100.0 * exp(-2.0 * dot(sd, sd) - 128.0 * id * id), 1e-32); }
)HLSL";

// LR1: chroma-w x luma-h. Hermite across, over luma texels within one chroma texel.
static const char kCgLR1[] = R"HLSL(
float4 main(PS_INPUT input) : SV_Target
{
	float2 l, c; LumaDims(l); ChromaDims(c);
	const float k = l.x / c.x;                 // luma texels per chroma texel
	const float cx = input.Pos.x * k;         // the chroma texel's centre, in luma pixels
	const int row = (int)input.Pos.y;
	const int s = (int)ceil(cx - k - 0.5), e = (int)floor(cx + k - 0.5);
	float wt = 0.0, sum = 0.0;
	for (int x = s; x <= e; x++) {
		const float w = comp_wd_hermite(((x + 0.5) - cx) / k);
		sum += w * Yat(int2(x, row)); wt += w;
	}
	return float4(saturate(sum / max(wt, 1e-6)), 0, 0, 1);
}
)HLSL";

// LR2: chroma-w x chroma-h, from LR1 (t3). Hermite down.
static const char kCgLR2[] = R"HLSL(
float4 main(PS_INPUT input) : SV_Target
{
	float2 l, c; LumaDims(l); ChromaDims(c);
	const float k = l.y / c.y;
	const float cy = input.Pos.y * k;
	const int col = (int)input.Pos.x;
	const int s = (int)ceil(cy - k - 0.5), e = (int)floor(cy + k - 0.5);
	float wt = 0.0, sum = 0.0;
	for (int y = s; y <= e; y++) {
		const float w = comp_wd_hermite(((y + 0.5) - cy) / k);
		sum += w * LRat(int2(col, y)); wt += w;
	}
	return float4(saturate(sum / max(wt, 1e-6)), 0, 0, 1);
}
)HLSL";

// JointBilateral upscale: luma-w x luma-h; t3 = the luma guide (LR2).
static const char kCgJB[] = R"HLSL(
float4 main(PS_INPUT input) : SV_Target
{
	float2 l, c; LumaDims(l); ChromaDims(c);
	const int2 lp = int2(input.Pos.xy);
	const float lz = Yat(lp);
	float2 pp = input.Pos.xy / l * c - 0.5;
	const float2 fp = floor(pp); pp -= fp;
	const int2 b = int2(fp);
	static const int2 o[12] = { int2(0,-1), int2(1,-1), int2(-1,0), int2(0,0), int2(1,0), int2(2,0),
	                            int2(-1,1), int2(0,1), int2(1,1), int2(2,1), int2(0,2), int2(1,2) };
	float wt = 0.0; float2 ct = 0.0;
	[unroll] for (int i = 0; i < 12; i++) {
		const float w = comp_w_bilateral(float2(o[i]) - pp, lz - LRat(b + o[i]));
		wt += w; ct += w * UVat(b + o[i]);
	}
	return float4(saturate(ct / wt), 0, 1);
}
)HLSL";

// CfL prediction upscale: luma-w x luma-h; t3 = the luma guide (LR2).
static const char kCgCfL[] = R"HLSL(
float comp_wd_cubic(float2 v) {
	const float d = min(length(v), 2.0), d2 = d * d, d3 = d2 * d;
	return d < 1.0 ? 1.25 * d3 - 2.25 * d2 + 1.0 : -0.75 * d3 + 3.75 * d2 - 6.0 * d + 3.0;
}
float4 main(PS_INPUT input) : SV_Target
{
	const float ar_strength = 0.8;
	float2 mix_coeff = 0.8;
	const float2 corr_exponent = 4.0;
	float2 l, c; LumaDims(l); ChromaDims(c);
	const int2 lp = int2(input.Pos.xy);
	const float luma_zero = Yat(lp);
	float2 pp = input.Pos.xy / l * c - 0.5;
	const float2 fp = floor(pp); pp -= fp;
	const int2 b = int2(fp);

	float luma_pixels[16]; float2 chroma_pixels[16];
	[unroll] for (int i = 0; i < 16; i++) {
		const int2 o = int2(i % 4 - 1, i / 4 - 1);
		luma_pixels[i] = LRat(b + o);
		chroma_pixels[i] = UVat(b + o);
	}

	float wt = 0.0; float2 ct = 0.0;
	const float2 chroma_min = min(min(min(chroma_pixels[5], chroma_pixels[6]), chroma_pixels[9]), chroma_pixels[10]);
	const float2 chroma_max = max(max(max(chroma_pixels[5], chroma_pixels[6]), chroma_pixels[9]), chroma_pixels[10]);
	[unroll] for (int j = 0; j < 16; j++) {
		const float wd = comp_wd_cubic(float2(j % 4 - 1, j / 4 - 1) - pp);
		wt += wd; ct += wd * chroma_pixels[j];
	}
	float2 chroma_spatial = ct / wt;
	chroma_spatial = saturate(lerp(chroma_spatial, clamp(chroma_spatial, chroma_min, chroma_max), ar_strength));

	static const int i12[12] = { 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14 };
	static const int i4y[4] = { 1, 2, 13, 14 };
	static const int i4x[4] = { 4, 7, 8, 11 };
	static const int i4[4] = { 5, 6, 9, 10 };
	float luma_sum_4 = 0.0, luma_sum_4y = 0.0, luma_sum_4x = 0.0;
	float2 chroma_sum_4 = 0.0, chroma_sum_4y = 0.0, chroma_sum_4x = 0.0;
	[unroll] for (int a = 0; a < 4; a++) {
		luma_sum_4 += luma_pixels[i4[a]]; luma_sum_4y += luma_pixels[i4y[a]]; luma_sum_4x += luma_pixels[i4x[a]];
		chroma_sum_4 += chroma_pixels[i4[a]]; chroma_sum_4y += chroma_pixels[i4y[a]]; chroma_sum_4x += chroma_pixels[i4x[a]];
	}

	const float luma_avg_12 = (luma_sum_4 + luma_sum_4y + luma_sum_4x) / 12.0;
	float luma_min_12 = luma_pixels[i12[0]], luma_max_12 = luma_pixels[i12[0]], luma_var_12 = 0.0;
	const float2 chroma_avg_12 = (chroma_sum_4 + chroma_sum_4y + chroma_sum_4x) / 12.0;
	float2 chroma_var_12 = 0.0, luma_chroma_cov_12 = 0.0;
	[unroll] for (int m = 0; m < 12; m++) {
		const float lv = luma_pixels[i12[m]]; const float2 cv = chroma_pixels[i12[m]];
		luma_min_12 = min(luma_min_12, lv); luma_max_12 = max(luma_max_12, lv);
		luma_var_12 += (lv - luma_avg_12) * (lv - luma_avg_12);
		chroma_var_12 += (cv - chroma_avg_12) * (cv - chroma_avg_12);
		luma_chroma_cov_12 += (lv - luma_avg_12) * (cv - chroma_avg_12);
	}
	const float2 corr = saturate(abs(luma_chroma_cov_12 / max(sqrt(luma_var_12 * chroma_var_12), 1e-6)));
	mix_coeff = pow(corr, corr_exponent) * mix_coeff;
	const float luma_range_12 = max(luma_max_12 - luma_min_12, 1e-6);
	const float extrapolation = max(max(luma_zero - luma_max_12, luma_min_12 - luma_zero), 0.0) / luma_range_12;
	mix_coeff *= saturate((8.0 - extrapolation) / 4.0);

	const float2 alpha_12 = luma_chroma_cov_12 / max(luma_var_12, 1e-6);
	const float2 beta_12 = chroma_avg_12 - alpha_12 * luma_avg_12;
	const float2 chroma_pred_12 = saturate(alpha_12 * luma_zero + beta_12);

	static const int i8y[8] = { 1, 2, 5, 6, 9, 10, 13, 14 };
	static const int i8x[8] = { 4, 5, 6, 7, 8, 9, 10, 11 };
	const float luma_avg_8y = (luma_sum_4 + luma_sum_4y) / 8.0, luma_avg_8x = (luma_sum_4 + luma_sum_4x) / 8.0;
	float luma_var_8y = 0.0, luma_var_8x = 0.0;
	const float2 chroma_avg_8y = (chroma_sum_4 + chroma_sum_4y) / 8.0, chroma_avg_8x = (chroma_sum_4 + chroma_sum_4x) / 8.0;
	float2 luma_chroma_cov_8y = 0.0, luma_chroma_cov_8x = 0.0;
	[unroll] for (int n = 0; n < 8; n++) {
		const float ly = luma_pixels[i8y[n]], lx = luma_pixels[i8x[n]];
		luma_var_8y += (ly - luma_avg_8y) * (ly - luma_avg_8y);
		luma_var_8x += (lx - luma_avg_8x) * (lx - luma_avg_8x);
		luma_chroma_cov_8y += (ly - luma_avg_8y) * (chroma_pixels[i8y[n]] - chroma_avg_8y);
		luma_chroma_cov_8x += (lx - luma_avg_8x) * (chroma_pixels[i8x[n]] - chroma_avg_8x);
	}
	const float2 alpha_8y = luma_chroma_cov_8y / max(luma_var_8y, 1e-6), alpha_8x = luma_chroma_cov_8x / max(luma_var_8x, 1e-6);
	const float2 beta_8y = chroma_avg_8y - alpha_8y * luma_avg_8y, beta_8x = chroma_avg_8x - alpha_8x * luma_avg_8x;
	const float2 chroma_pred_8 = lerp(saturate(alpha_8y * luma_zero + beta_8y), saturate(alpha_8x * luma_zero + beta_8x), 0.5);

	const float2 o2 = lerp(chroma_spatial, lerp(chroma_pred_12, chroma_pred_8, 0.5), mix_coeff);
	return float4(saturate(o2), 0, 1);
}
)HLSL";

// CfL's final smoothing: luma-w x luma-h; t3 = the CfL result. 3x3 joint bilateral.
static const char kCgSmooth[] = R"HLSL(
float4 main(PS_INPUT input) : SV_Target
{
	const int2 lp = int2(input.Pos.xy);
	const float luma_zero = Yat(lp);
	float wt = 0.0; float2 ct = 0.0;
	[unroll] for (int i = -1; i < 2; i++) {
		[unroll] for (int j = -1; j < 2; j++) {
			const float w = comp_w_bilateral(float2(i, j), luma_zero - Yat(lp + int2(i, j)));
			wt += w; ct += w * INat(lp + int2(i, j));
		}
	}
	return float4(saturate(ct / wt), 0, 1);
}
)HLSL";
