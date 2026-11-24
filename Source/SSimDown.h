/*
 * SSimDownscaler (ClipGuard 2.114.0): a downscale that keeps the source's local contrast
 * (detail) instead of softening it. The normal downscaler draws the picture at the output
 * size first; four passes then correct it against the source. Ported from the mpv shader
 * by igv (https://gist.github.com/igv/36508af3ffc84410fe39761d6969be10), LGPL 3.0.
 */

#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include "DX11Helper.h"
#include "ShaderCache.h"

namespace ssimd {
// cb: source offset, source size (w1, h1), output size (w2, h2), output offset.
static const char* kCommon =
	"cbuffer C : register(b0) { int2 srcOff; int2 s1; int2 s2; int2 dstOff; };\n"
	"#define MN(B,C,x) (x < 1.0 ? ((2.-1.5*B-(C))*x + (-3.+2.*B+C))*x*x + (1.-(B)/3.) : (((-(B)/6.-(C))*x + (B+5.*C))*x + (-2.*B-8.*C))*x+((4./3.)*B+4.*C))\n"
	"#define Kernel(x) MN(0.0, 0.5, abs(x))\n"
	"#define Luma(rgb) dot(rgb, float3(0.2126, 0.7152, 0.0722))\n";

// L2 pass 1: the source squared, down to w1 x h2 (vertically).
static const char* kL2a =
	"Texture2D<float4> P : register(t0);\n"
	"float4 main(float4 pos : SV_POSITION) : SV_TARGET\n"
	"{\n"
	"\tconst int x = (int)pos.x, y = (int)pos.y;\n"
	"\tconst float c = (y + 0.5) / s2.y;\n"
	"\tconst int lo = (int)ceil((c - 2.0 / s2.y) * s1.y - 0.5), hi = (int)floor((c + 2.0 / s2.y) * s1.y - 0.5);\n"
	"\tfloat W = 0.0; float3 avg = 0.0;\n"
	"\tfor (int k = lo; k <= hi; k++) {\n"
	"\t\tconst float w = Kernel(((k + 0.5) / s1.y - c) * s2.y);\n"
	"\t\tconst float3 t = P.Load(int3(srcOff + int2(x, clamp(k, 0, s1.y - 1)), 0)).rgb;\n"
	"\t\tavg += w * t * t; W += w;\n"
	"\t}\n"
	"\treturn float4(avg / W, 1.0);\n"
	"}\n";

// L2 pass 2: down to w2 x h2 (horizontally).
static const char* kL2b =
	"Texture2D<float4> A : register(t0);\n"
	"float4 main(float4 pos : SV_POSITION) : SV_TARGET\n"
	"{\n"
	"\tconst int x = (int)pos.x, y = (int)pos.y;\n"
	"\tconst float c = (x + 0.5) / s2.x;\n"
	"\tconst int lo = (int)ceil((c - 2.0 / s2.x) * s1.x - 0.5), hi = (int)floor((c + 2.0 / s2.x) * s1.x - 0.5);\n"
	"\tfloat W = 0.0; float3 avg = 0.0;\n"
	"\tfor (int k = lo; k <= hi; k++) {\n"
	"\t\tconst float w = Kernel(((k + 0.5) / s1.x - c) * s2.x);\n"
	"\t\tavg += w * A.Load(int3(clamp(k, 0, s1.x - 1), y, 0)).rgb; W += w;\n"
	"\t}\n"
	"\treturn float4(avg / W, 1.0);\n"
	"}\n";

// mean & R: local mean of the downscale, and the contrast ratio to restore.
static const char* kMR =
	"Texture2D<float4> H : register(t0);\n"
	"Texture2D<float4> L2 : register(t1);\n"
	"float4 main(float4 pos : SV_POSITION) : SV_TARGET\n"
	"{\n"
	"\tconst int2 p = int2(pos.xy);\n"
	"\tfloat W = 0.0; float3 m0 = 0.0, m1 = 0.0, m2 = 0.0;\n"
	"\t[unroll] for (int i = -1; i <= 1; i++)\n"
	"\t[unroll] for (int j = -1; j <= 1; j++) {\n"
	"\t\tconst float w = pow(0.5, abs(i)) * pow(0.5, abs(j));\n"
	"\t\tconst int2 q = clamp(p + int2(i, j), int2(0, 0), s2 - 1);\n"
	"\t\tconst float3 L = H.Load(int3(q, 0)).rgb;\n"
	"\t\tm0 += w * L; m1 += w * L * L; m2 += w * L2.Load(int3(q, 0)).rgb; W += w;\n"
	"\t}\n"
	"\tm0 /= W; m1 /= W; m2 /= W;\n"
	"\tconst float sn = 10.0 / (255.0 * 255.0);\n"
	"\tconst float Sl = Luma(max(m1 - m0 * m0, 0.0));\n"
	"\tconst float Sh = Luma(max(m2 - m0 * m0, 0.0));\n"
	"\tconst float R = (Sl > Sh) ? saturate(Sh / Sl) : sqrt((Sh + sn) / (Sl + sn));\n"
	"\treturn float4(m0, R);\n"
	"}\n";

// final: written at the output rectangle.
static const char* kFinal =
	"Texture2D<float4> H : register(t0);\n"
	"Texture2D<float4> MR : register(t1);\n"
	"float4 main(float4 spos : SV_POSITION) : SV_TARGET\n"
	"{\n"
	"\tconst int2 p = int2(spos.xy) - dstOff;\n"
	"\tfloat W = 0.0; float3 a0 = 0.0, a1 = 0.0, a2 = 0.0;\n"
	"\t[unroll] for (int i = -1; i <= 1; i++)\n"
	"\t[unroll] for (int j = -1; j <= 1; j++) {\n"
	"\t\tconst float w = pow(0.5, abs(i)) * pow(0.5, abs(j));\n"
	"\t\tconst float4 v = MR.Load(int3(clamp(p + int2(i, j), int2(0, 0), s2 - 1), 0));\n"
	"\t\ta0 += w * v.a * v.rgb; a1 += w * v.rgb; a2 += w * v.aaa; W += w;\n"
	"\t}\n"
	"\ta0 /= W; a1 /= W; a2 /= W;\n"
	"\tconst float4 L = H.Load(int3(p, 0));\n"
	"\treturn float4(a1 + a2 * L.rgb - a0, L.a);\n"
	"}\n";
} // namespace ssimd

class CSsimDown
{
public:
	enum State { kOff = 0, kLoading = 1, kReady = 2, kFailed = 3 };

	~CSsimDown() { m_stop = true; Join(); for (auto* b : m_blobs) if (b) b->Release(); }

	void ReleaseDevice()
	{
		for (auto& p : m_ps) p.Release();
		m_ps.clear();
		m_cb.Release();
		ReleaseTextures();
		if (m_state == kReady) m_state = kLoading;
	}

	void Request()
	{
		if (m_started) return;
		m_started = true;
		m_state = kLoading;
		m_thread = std::thread([this]() {
			const char* srcs[4] = { ssimd::kL2a, ssimd::kL2b, ssimd::kMR, ssimd::kFinal };
			std::vector<ID3DBlob*> blobs(4, nullptr);
			bool ok = true;
			for (int i = 0; i < 4 && ok && !m_stop; i++) {
				ok = shadercache::Compile((std::string(ssimd::kCommon) + srcs[i]).c_str(), "ps_4_0", "SSimDownscaler", &blobs[i]);
			}
			if (!ok || m_stop) {
				for (auto* b : blobs) if (b) b->Release();
				m_compiled = -1;
				return;
			}
			m_blobs = blobs;
			m_compiled = 1;
		});
	}

	int GetState() const { return m_state; }

	bool Ready(ID3D11Device* pDevice)
	{
		if (m_state == kFailed || !m_started) return false;
		if (m_compiled == 0) return false;
		if (m_compiled < 0) { Join(); m_state = kFailed; return false; }
		if (m_ps.empty()) {
			Join();
			m_ps.resize(4);
			bool ok = true;
			for (int i = 0; i < 4 && ok; i++) ok = SUCCEEDED(pDevice->CreatePixelShader(m_blobs[i]->GetBufferPointer(), m_blobs[i]->GetBufferSize(), nullptr, &m_ps[i]));
			if (ok) {
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				ok = SUCCEEDED(pDevice->CreateBuffer(&bd, nullptr, &m_cb));
			}
			if (!ok) { for (auto& p : m_ps) p.Release(); m_ps.clear(); m_state = kFailed; return false; }
		}
		m_state = kReady;
		return true;
	}

	// Hooked(): where the normal downscaler draws the picture (w2 x h2) before Run.
	HRESULT CheckTextures(ID3D11Device* pDevice, UINT w1, UINT h1, UINT w2, UINT h2)
	{
		if (m_w1 == w1 && m_h1 == h1 && m_w2 == w2 && m_h2 == h2 && m_hooked.pTexture) return S_OK;
		ReleaseTextures();
		const DXGI_FORMAT f = DXGI_FORMAT_R16G16B16A16_FLOAT;
		HRESULT hr = m_hooked.Create(pDevice, f, w2, h2, Tex2D_DefaultShaderRTarget);
		if (SUCCEEDED(hr)) hr = m_l2a.Create(pDevice, f, w1, h2, Tex2D_DefaultShaderRTarget);
		if (SUCCEEDED(hr)) hr = m_l2.Create(pDevice, f, w2, h2, Tex2D_DefaultShaderRTarget);
		if (SUCCEEDED(hr)) hr = m_mr.Create(pDevice, f, w2, h2, Tex2D_DefaultShaderRTarget);
		if (SUCCEEDED(hr)) hr = pDevice->CreateRenderTargetView(m_l2a.pTexture, nullptr, &m_rtvL2a);
		if (SUCCEEDED(hr)) hr = pDevice->CreateRenderTargetView(m_l2.pTexture, nullptr, &m_rtvL2);
		if (SUCCEEDED(hr)) hr = pDevice->CreateRenderTargetView(m_mr.pTexture, nullptr, &m_rtvMR);
		if (FAILED(hr)) { ReleaseTextures(); return hr; }
		m_w1 = w1; m_h1 = h1; m_w2 = w2; m_h2 = h2;
		return S_OK;
	}

	ID3D11Texture2D* Hooked() const { return m_hooked.pTexture; }

	using DrawFn = std::function<void(ID3D11RenderTargetView* rtv, const D3D11_VIEWPORT& vp,
		ID3D11PixelShader* ps, ID3D11ShaderResourceView* const* srvs, UINT nSrv, ID3D11Buffer* cb)>;

	// pSrc: the source (PREKERNEL) at srcRect; the result goes to rtvTarget at dstRect.
	HRESULT Run(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pSrc, const CRect& srcRect,
		ID3D11RenderTargetView* rtvTarget, const CRect& dstRect, const DrawFn& draw)
	{
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(pContext->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return E_FAIL;
		int* c = (int*)m.pData;
		c[0] = srcRect.left; c[1] = srcRect.top; c[2] = (int)m_w1; c[3] = (int)m_h1;
		c[4] = (int)m_w2; c[5] = (int)m_h2; c[6] = dstRect.left; c[7] = dstRect.top;
		pContext->Unmap(m_cb, 0);

		auto vp = [](float x, float y, float w, float h) { D3D11_VIEWPORT v = { x, y, w, h, 0.0f, 1.0f }; return v; };
		{ ID3D11ShaderResourceView* s[1] = { pSrc }; draw(m_rtvL2a, vp(0, 0, (float)m_w1, (float)m_h2), m_ps[0], s, 1, m_cb); }
		{ ID3D11ShaderResourceView* s[1] = { m_l2a.pShaderResource }; draw(m_rtvL2, vp(0, 0, (float)m_w2, (float)m_h2), m_ps[1], s, 1, m_cb); }
		{ ID3D11ShaderResourceView* s[2] = { m_hooked.pShaderResource, m_l2.pShaderResource }; draw(m_rtvMR, vp(0, 0, (float)m_w2, (float)m_h2), m_ps[2], s, 2, m_cb); }
		{ ID3D11ShaderResourceView* s[2] = { m_hooked.pShaderResource, m_mr.pShaderResource };
		  draw(rtvTarget, vp((float)dstRect.left, (float)dstRect.top, (float)m_w2, (float)m_h2), m_ps[3], s, 2, m_cb); }
		return S_OK;
	}

private:
	void ReleaseTextures()
	{
		m_rtvL2a.Release(); m_rtvL2.Release(); m_rtvMR.Release();
		m_hooked.Release(); m_l2a.Release(); m_l2.Release(); m_mr.Release();
		m_w1 = m_h1 = m_w2 = m_h2 = 0;
	}

	void Join() { if (m_thread.joinable()) m_thread.join(); }

	std::thread m_thread;
	std::atomic<int> m_compiled{ 0 };
	std::atomic<bool> m_stop{ false };
	bool m_started = false;
	int m_state = kOff;
	std::vector<ID3DBlob*> m_blobs;
	std::vector<CComPtr<ID3D11PixelShader>> m_ps;
	CComPtr<ID3D11Buffer> m_cb;
	Tex2D_t m_hooked, m_l2a, m_l2, m_mr;
	CComPtr<ID3D11RenderTargetView> m_rtvL2a, m_rtvL2, m_rtvMR;
	UINT m_w1 = 0, m_h1 = 0, m_w2 = 0, m_h2 = 0;
};
