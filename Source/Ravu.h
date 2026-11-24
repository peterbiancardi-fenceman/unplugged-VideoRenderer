/*
 * RAVU-Zoom-AR r3 (ClipGuard 2.114.0): the upscaler for any ratio (not about 2x, where the
 * neural network runs). Ported from bjin/mpv-prescalers ravu-zoom-ar-r3.hook, LGPL-3.0
 * (https://gh.ali360.cloudns.ch/bjin/mpv-prescalers). The edge direction is measured on luma and the
 * learned weights are applied to R, G and B, then mpv's anti-ringing clamp per channel.
 * The two lookup tables (rgba16f, as mpv uploads them) are the IDF_RAVU_LUT resource.
 */

#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include "DX11Helper.h"
#include "ShaderCache.h"
#include "resource.h"
#include "Utils/Util.h"

namespace ravu {
static const char* kPS =
	R"RAVU(
// RAVU-Zoom-AR r3, RGB form (ClipGuard 2.114.0). Ported from bjin/mpv-prescalers
// ravu-zoom-ar-r3.hook (LGPL-3.0): edge direction is measured on luma, the learned
// weights are applied to R, G and B, then the anti-ringing clamp per channel.
cbuffer C : register(b0) { int2 srcOff; int2 s1; int2 s2; int2 dstOff; };
Texture2D<float4> H : register(t0);
Texture2D<float4> LZ : register(t1);
Texture2D<float4> LA : register(t2);
SamplerState sl : register(s0);
#define YW float3(0.2126, 0.7152, 0.0722)
#define ARSTR 0.8
#define LD(dx, dy) H.Load(int3(srcOff + clamp(ip + int2(dx, dy), int2(0, 0), s1 - 1), 0)).rgb
#define LUTPOS(x, n) lerp(0.5 / (n), 1.0 - 0.5 / (n), (x))
#define AR(sa, sb, wa, wb) { float3 c, c1; \
	c = 0.1 + saturate(sa); c1 = c; c *= c; c *= c; c *= c; c *= c; c *= c; hi += c * (wa); hi2 += c * c1 * (wa); \
	c = 1.1 - saturate(sa); c1 = c; c *= c; c *= c; c *= c; c *= c; c *= c; lo += c * (wa); lo2 += c * c1 * (wa); \
	c = 0.1 + saturate(sb); c1 = c; c *= c; c *= c; c *= c; c *= c; c *= c; hi += c * (wb); hi2 += c * c1 * (wb); \
	c = 1.1 - saturate(sb); c1 = c; c *= c; c *= c; c *= c; c *= c; c *= c; lo += c * (wb); lo2 += c * c1 * (wb); }
float fmod_pos(float x, float y) { return x - y * floor(x / y); }
float4 main(float4 spos : SV_POSITION) : SV_TARGET
{
	const int2 o = int2(spos.xy) - dstOff;
	const float2 p = (float2(o) + 0.5) * float2(s1) / float2(s2);
	float2 subpix = frac(p - 0.5);
	const int2 ip = int2(floor(p - 0.5));
	subpix = LUTPOS(subpix, float2(9.0, 9.0));
	float2 subpix_inv = 1.0 - subpix;
	const float2 subpix_ar = subpix / float2(2.0, 288.0);
	const float2 subpix_inv_ar = subpix_inv / float2(2.0, 288.0);
	subpix /= float2(5.0, 288.0);
	subpix_inv /= float2(5.0, 288.0);
	const float3 q0 = LD(-2, -2); const float l0 = dot(saturate(q0), YW);
	const float3 q1 = LD(-2, -1); const float l1 = dot(saturate(q1), YW);
	const float3 q2 = LD(-2, 0); const float l2 = dot(saturate(q2), YW);
	const float3 q3 = LD(-2, 1); const float l3 = dot(saturate(q3), YW);
	const float3 q4 = LD(-2, 2); const float l4 = dot(saturate(q4), YW);
	const float3 q5 = LD(-2, 3); const float l5 = dot(saturate(q5), YW);
	const float3 q6 = LD(-1, -2); const float l6 = dot(saturate(q6), YW);
	const float3 q7 = LD(-1, -1); const float l7 = dot(saturate(q7), YW);
	const float3 q8 = LD(-1, 0); const float l8 = dot(saturate(q8), YW);
	const float3 q9 = LD(-1, 1); const float l9 = dot(saturate(q9), YW);
	const float3 q10 = LD(-1, 2); const float l10 = dot(saturate(q10), YW);
	const float3 q11 = LD(-1, 3); const float l11 = dot(saturate(q11), YW);
	const float3 q12 = LD(0, -2); const float l12 = dot(saturate(q12), YW);
	const float3 q13 = LD(0, -1); const float l13 = dot(saturate(q13), YW);
	const float3 q14 = LD(0, 0); const float l14 = dot(saturate(q14), YW);
	const float3 q15 = LD(0, 1); const float l15 = dot(saturate(q15), YW);
	const float3 q16 = LD(0, 2); const float l16 = dot(saturate(q16), YW);
	const float3 q17 = LD(0, 3); const float l17 = dot(saturate(q17), YW);
	const float3 q18 = LD(1, -2); const float l18 = dot(saturate(q18), YW);
	const float3 q19 = LD(1, -1); const float l19 = dot(saturate(q19), YW);
	const float3 q20 = LD(1, 0); const float l20 = dot(saturate(q20), YW);
	const float3 q21 = LD(1, 1); const float l21 = dot(saturate(q21), YW);
	const float3 q22 = LD(1, 2); const float l22 = dot(saturate(q22), YW);
	const float3 q23 = LD(1, 3); const float l23 = dot(saturate(q23), YW);
	const float3 q24 = LD(2, -2); const float l24 = dot(saturate(q24), YW);
	const float3 q25 = LD(2, -1); const float l25 = dot(saturate(q25), YW);
	const float3 q26 = LD(2, 0); const float l26 = dot(saturate(q26), YW);
	const float3 q27 = LD(2, 1); const float l27 = dot(saturate(q27), YW);
	const float3 q28 = LD(2, 2); const float l28 = dot(saturate(q28), YW);
	const float3 q29 = LD(2, 3); const float l29 = dot(saturate(q29), YW);
	const float3 q30 = LD(3, -2); const float l30 = dot(saturate(q30), YW);
	const float3 q31 = LD(3, -1); const float l31 = dot(saturate(q31), YW);
	const float3 q32 = LD(3, 0); const float l32 = dot(saturate(q32), YW);
	const float3 q33 = LD(3, 1); const float l33 = dot(saturate(q33), YW);
	const float3 q34 = LD(3, 2); const float l34 = dot(saturate(q34), YW);
	const float3 q35 = LD(3, 3); const float l35 = dot(saturate(q35), YW);
	float3 abd = 0.0;
	float gx, gy;
	gx = (l13-l1)/2.0;
	gy = (l8-l6)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.04792235409415088;
	gx = (l14-l2)/2.0;
	gy = (-l10+8.0*l9-8.0*l7+l6)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (l15-l3)/2.0;
	gy = (-l11+8.0*l10-8.0*l8+l7)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (l16-l4)/2.0;
	gy = (l11-l9)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.04792235409415088;
	gx = (-l25+8.0*l19-8.0*l7+l1)/12.0;
	gy = (l14-l12)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (-l26+8.0*l20-8.0*l8+l2)/12.0;
	gy = (-l16+8.0*l15-8.0*l13+l12)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.07901060453704994;
	gx = (-l27+8.0*l21-8.0*l9+l3)/12.0;
	gy = (-l17+8.0*l16-8.0*l14+l13)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.07901060453704994;
	gx = (-l28+8.0*l22-8.0*l10+l4)/12.0;
	gy = (l17-l15)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (-l31+8.0*l25-8.0*l13+l7)/12.0;
	gy = (l20-l18)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (-l32+8.0*l26-8.0*l14+l8)/12.0;
	gy = (-l22+8.0*l21-8.0*l19+l18)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.07901060453704994;
	gx = (-l33+8.0*l27-8.0*l15+l9)/12.0;
	gy = (-l23+8.0*l22-8.0*l20+l19)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.07901060453704994;
	gx = (-l34+8.0*l28-8.0*l16+l10)/12.0;
	gy = (l23-l21)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (l31-l19)/2.0;
	gy = (l26-l24)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.04792235409415088;
	gx = (l32-l20)/2.0;
	gy = (-l28+8.0*l27-8.0*l25+l24)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (l33-l21)/2.0;
	gy = (-l29+8.0*l28-8.0*l26+l25)/12.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.06153352068439959;
	gx = (l34-l22)/2.0;
	gy = (l29-l27)/2.0;
	abd += float3(gx * gx, gx * gy, gy * gy) * 0.04792235409415088;
	float a = abd.x, b = abd.y, d = abd.z;
	float T = a + d, D = a * d - b * b;
	float delta = sqrt(max(T * T / 4.0 - D, 0.0));
	float L1 = T / 2.0 + delta, L2 = T / 2.0 - delta;
	float sqrtL1 = sqrt(L1), sqrtL2 = sqrt(L2);
	float theta = (abs(b) < 1.192092896e-7) ? 0.0 : fmod_pos(atan2(L1 - a, b) + 3.141592653589793, 3.141592653589793);
	float lambda = sqrtL1;
	float mu = (sqrtL1 + sqrtL2 < 1.192092896e-7) ? 0.0 : (sqrtL1 - sqrtL2) / (sqrtL1 + sqrtL2);
	float angle = floor(theta * 24.0 / 3.141592653589793);
	float strength = (lambda >= 0.016) ? ((lambda >= 0.05) ? 3.0 : 2.0) : ((lambda >= 0.004) ? 1.0 : 0.0);
	float coherence = (mu >= 0.5) ? 2.0 : ((mu >= 0.25) ? 1.0 : 0.0);
	float coord_y = ((angle * 4.0 + strength) * 3.0 + coherence) / 288.0;
	float3 res = 0.0;
	float4 w;
	float3 lo = 0.0, hi = 0.0;
	float3 lo2 = 0.0, hi2 = 0.0;
	w = LZ.SampleLevel(sl, float2(0.0, coord_y) + subpix, 0);
	res += q0 * w[0];
	res += q1 * w[1];
	res += q2 * w[2];
	res += q3 * w[3];
	w = LZ.SampleLevel(sl, float2(0.2, coord_y) + subpix, 0);
	res += q4 * w[0];
	res += q5 * w[1];
	res += q6 * w[2];
	res += q7 * w[3];
	w = LZ.SampleLevel(sl, float2(0.4, coord_y) + subpix, 0);
	res += q8 * w[0];
	res += q9 * w[1];
	res += q10 * w[2];
	res += q11 * w[3];
	w = LZ.SampleLevel(sl, float2(0.6, coord_y) + subpix, 0);
	res += q12 * w[0];
	res += q13 * w[1];
	res += q14 * w[2];
	res += q15 * w[3];
	w = LZ.SampleLevel(sl, float2(0.8, coord_y) + subpix, 0);
	res += q16 * w[0];
	res += q17 * w[1];
	w = LZ.SampleLevel(sl, float2(0.0, coord_y) + subpix_inv, 0);
	res += q35 * w[0];
	res += q34 * w[1];
	res += q33 * w[2];
	res += q32 * w[3];
	w = LZ.SampleLevel(sl, float2(0.2, coord_y) + subpix_inv, 0);
	res += q31 * w[0];
	res += q30 * w[1];
	res += q29 * w[2];
	res += q28 * w[3];
	w = LZ.SampleLevel(sl, float2(0.4, coord_y) + subpix_inv, 0);
	res += q27 * w[0];
	res += q26 * w[1];
	res += q25 * w[2];
	res += q24 * w[3];
	w = LZ.SampleLevel(sl, float2(0.6, coord_y) + subpix_inv, 0);
	res += q23 * w[0];
	res += q22 * w[1];
	res += q21 * w[2];
	res += q20 * w[3];
	w = LZ.SampleLevel(sl, float2(0.8, coord_y) + subpix_inv, 0);
	res += q19 * w[0];
	res += q18 * w[1];
	w = LA.SampleLevel(sl, float2(0.0, coord_y) + subpix_ar, 0);
	AR(q7, q8, w[0], w[1]);
	AR(q9, q10, w[2], w[3]);
	w = LA.SampleLevel(sl, float2(0.5, coord_y) + subpix_ar, 0);
	AR(q13, q14, w[0], w[1]);
	AR(q15, q16, w[2], w[3]);
	w = LA.SampleLevel(sl, float2(0.0, coord_y) + subpix_inv_ar, 0);
	AR(q28, q27, w[0], w[1]);
	AR(q26, q25, w[2], w[3]);
	w = LA.SampleLevel(sl, float2(0.5, coord_y) + subpix_inv_ar, 0);
	AR(q22, q21, w[0], w[1]);
	AR(q20, q19, w[2], w[3]);
	hi = hi2 / hi - 0.1;
	lo = 1.1 - lo2 / lo;
	res = lerp(res, clamp(res, lo, hi), ARSTR);
	const float a0 = H.Load(int3(srcOff + clamp(int2(floor(p)), int2(0, 0), s1 - 1), 0)).a;
	return float4(res, a0);
}

)RAVU";
} // namespace ravu

class CRavuZoom
{
public:
	enum State { kOff = 0, kLoading = 1, kReady = 2, kFailed = 3 };

	~CRavuZoom() { m_stop = true; Join(); if (m_blob) m_blob->Release(); }

	void ReleaseDevice()
	{
		m_ps.Release(); m_cb.Release(); m_samp.Release();
		m_lutZ.Release(); m_lutA.Release(); m_srvZ.Release(); m_srvA.Release();
		if (m_state == kReady) m_state = kLoading;
	}

	void Request()
	{
		if (m_started) return;
		m_started = true;
		m_state = kLoading;
		m_thread = std::thread([this]() {
			ID3DBlob* b = nullptr;
			const bool ok = shadercache::Compile(ravu::kPS, "ps_5_0", "RAVU-Zoom-AR", &b);
			if (!ok || m_stop) { if (b) b->Release(); m_compiled = -1; return; }
			m_blob = b;
			m_compiled = 1;
		});
	}

	int GetState() const { return m_state; }

	bool Ready(ID3D11Device* pDevice)
	{
		if (m_state == kFailed || !m_started) return false;
		if (m_compiled == 0) return false;
		if (m_compiled < 0) { Join(); m_state = kFailed; return false; }
		if (!m_ps) {
			Join();
			bool ok = SUCCEEDED(pDevice->CreatePixelShader(m_blob->GetBufferPointer(), m_blob->GetBufferSize(), nullptr, &m_ps));
			if (ok) {
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				ok = SUCCEEDED(pDevice->CreateBuffer(&bd, nullptr, &m_cb));
			}
			if (ok) {
				D3D11_SAMPLER_DESC sd = {};
				sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
				ok = SUCCEEDED(pDevice->CreateSamplerState(&sd, &m_samp));
			}
			if (ok) {
				LPVOID data = nullptr; DWORD size = 0;
				const UINT bZ = 45 * 2592 * 8, bA = 18 * 2592 * 8;
				ok = SUCCEEDED(GetDataFromResource(data, size, IDF_RAVU_LUT)) && data && size == bZ + bA;
				if (ok) ok = MakeLut(pDevice, 45, 2592, (const BYTE*)data, &m_lutZ, &m_srvZ) && MakeLut(pDevice, 18, 2592, (const BYTE*)data + bZ, &m_lutA, &m_srvA);
			}
			if (!ok) { ReleaseDevice(); m_state = kFailed; return false; }
		}
		m_state = kReady;
		return true;
	}

	using DrawFn = std::function<void(ID3D11RenderTargetView* rtv, const D3D11_VIEWPORT& vp,
		ID3D11PixelShader* ps, ID3D11ShaderResourceView* const* srvs, UINT nSrv, ID3D11Buffer* cb, ID3D11SamplerState* samp)>;

	// pSrc's srcRect is scaled to rtv's dstRect.
	HRESULT Run(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pSrc, const CRect& srcRect,
		ID3D11RenderTargetView* rtv, const CRect& dstRect, const DrawFn& draw)
	{
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(pContext->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return E_FAIL;
		int* c = (int*)m.pData;
		c[0] = srcRect.left; c[1] = srcRect.top; c[2] = srcRect.Width(); c[3] = srcRect.Height();
		c[4] = dstRect.Width(); c[5] = dstRect.Height(); c[6] = dstRect.left; c[7] = dstRect.top;
		pContext->Unmap(m_cb, 0);
		D3D11_VIEWPORT vp = { (float)dstRect.left, (float)dstRect.top, (float)dstRect.Width(), (float)dstRect.Height(), 0.0f, 1.0f };
		ID3D11ShaderResourceView* s[3] = { pSrc, m_srvZ, m_srvA };
		draw(rtv, vp, m_ps, s, 3, m_cb, m_samp);
		return S_OK;
	}

private:
	static bool MakeLut(ID3D11Device* pDevice, UINT w, UINT h, const BYTE* p, ID3D11Texture2D** ppTex, ID3D11ShaderResourceView** ppSrv)
	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA sd = { p, w * 8, 0 };
		if (FAILED(pDevice->CreateTexture2D(&td, &sd, ppTex))) return false;
		return SUCCEEDED(pDevice->CreateShaderResourceView(*ppTex, nullptr, ppSrv));
	}

	void Join() { if (m_thread.joinable()) m_thread.join(); }

	std::thread m_thread;
	std::atomic<int> m_compiled{ 0 };
	std::atomic<bool> m_stop{ false };
	bool m_started = false;
	int m_state = kOff;
	ID3DBlob* m_blob = nullptr;
	CComPtr<ID3D11PixelShader> m_ps;
	CComPtr<ID3D11Buffer> m_cb;
	CComPtr<ID3D11SamplerState> m_samp;
	CComPtr<ID3D11Texture2D> m_lutZ, m_lutA;
	CComPtr<ID3D11ShaderResourceView> m_srvZ, m_srvA;
};
