/*
 * NeuralUpscale (ClipGuard 2.43.0): FSRCNNX x2 luma doubler for the shader
 * scaling path. Runs only when the picture is upscaled about 2x (1080p on a
 * 4K screen). The network doubles the luma (brightness detail); colour is
 * doubled by KrigBilateral guided by that new luma (2.43.1). The 2x
 * picture then goes through the normal scaler to the exact output size.
 *
 * The 27 pixel shaders are compiled once on a background thread; until they
 * are ready (a few seconds the first time) the usual scaler is used.
 *
 * FSRCNNX by igv, LGPL 3.0: https://github.com/igv/FSRCNN-TensorFlow
 */

#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include "DX11Helper.h"
#include "NeuralUpscale_FSRCNNX.h"

HRESULT CompileShader(const std::string& srcCode, const D3D_SHADER_MACRO* pDefines, LPCSTR pTarget, ID3DBlob** ppCode);

class CNeuralUpscale
{
public:
	enum State { kOff = 0, kLoading = 1, kReady = 2, kFailed = 3 };

	// One pass: render target (its size), pixel shader and its inputs.
	using DrawFn = std::function<void(ID3D11RenderTargetView* rtv, UINT w, UINT h,
		ID3D11PixelShader* ps, ID3D11ShaderResourceView* const* srvs, UINT nSrv)>;

	~CNeuralUpscale() { m_stop = true; Join(); ReleaseBlobs(); }

	// Device lost or renderer reset: textures and shaders go; the compiled
	// code is kept, so shaders come back at once on the new device.
	void ReleaseDevice()
	{
		for (auto& p : m_ps) p.Release();
		m_ps.clear();
		ReleaseTextures();
		if (m_state == kReady) m_state = kLoading;
	}

	// Starts compiling the first time the upscaler is wanted.
	void Request()
	{
		if (m_started) return;
		m_started = true;
		m_state = kLoading;
		m_thread = std::thread([this]() {
			std::vector<ID3DBlob*> blobs(fsrcnnx::kPassCount, nullptr);
			bool ok = true;
			for (int i = 0; i < fsrcnnx::kPassCount && ok && !m_stop; i++) {
				ok = SUCCEEDED(CompileShader(fsrcnnx::kPasses[i].src, nullptr, "ps_4_0", &blobs[i])) && blobs[i];
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

	// Kept up to date by Ready(): kOff, kLoading, kReady or kFailed.
	int GetState() const { return m_state; }

	// True when the shaders exist on this device.
	bool Ready(ID3D11Device* pDevice)
	{
		if (m_state == kFailed || !m_started) return false;
		if (m_compiled == 0) return false;
		if (m_compiled < 0) { Join(); m_state = kFailed; return false; }
		if (m_ps.empty()) {
			Join();
			m_ps.resize(fsrcnnx::kPassCount);
			for (int i = 0; i < fsrcnnx::kPassCount; i++) {
				if (FAILED(pDevice->CreatePixelShader(m_blobs[i]->GetBufferPointer(), m_blobs[i]->GetBufferSize(), nullptr, &m_ps[i]))) {
					for (auto& p : m_ps) p.Release();
					m_ps.clear();
					m_state = kFailed;
					return false;
				}
			}
		}
		m_state = kReady;
		return true;
	}

	// Doubles src (w x h, any RGB format) into Out (2w x 2h, RGBA16F).
	HRESULT Run(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
		ID3D11ShaderResourceView* pSrc, UINT w, UINT h, const DrawFn& draw)
	{
		HRESULT hr = CheckTextures(pDevice, w, h);
		if (FAILED(hr)) return hr;
		for (int i = 0; i < fsrcnnx::kPassCount; i++) {
			const auto& p = fsrcnnx::kPasses[i];
			ID3D11ShaderResourceView* srvs[5] = {};
			for (int k = 0; k < p.nIn; k++) {
				srvs[k] = (p.in[k] == fsrcnnx::kSlotSrc) ? pSrc : m_tex[p.in[k]].pShaderResource.p;
			}
			const bool big = (p.out == fsrcnnx::kSlotOut || p.out == fsrcnnx::kSlotLuma2);
			draw(m_rtv[p.out], big ? 2 * w : w, big ? 2 * h : h, m_ps[i], srvs, (UINT)p.nIn);
		}
		// Leave nothing bound, so Out can be read straight away.
		ID3D11ShaderResourceView* none[5] = {};
		pContext->PSSetShaderResources(0, 5, none);
		pContext->OMSetRenderTargets(0, nullptr, nullptr);
		return S_OK;
	}

	const Tex2D_t& Out() const { return m_tex[fsrcnnx::kSlotOut]; }

private:
	HRESULT CheckTextures(ID3D11Device* pDevice, UINT w, UINT h)
	{
		if (m_w == w && m_h == h && m_tex[fsrcnnx::kSlotOut].pTexture) return S_OK;
		ReleaseTextures();
		for (int s = 0; s < fsrcnnx::kSlots; s++) {
			if (s == fsrcnnx::kSlotSrc) continue;
			const bool big = (s == fsrcnnx::kSlotOut || s == fsrcnnx::kSlotLuma2);
			const DXGI_FORMAT fmt = (s == fsrcnnx::kSlotLuma || s == fsrcnnx::kSlotLuma2) ? DXGI_FORMAT_R16_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
			HRESULT hr = m_tex[s].Create(pDevice, fmt, big ? 2 * w : w, big ? 2 * h : h, Tex2D_DefaultShaderRTarget);
			if (SUCCEEDED(hr)) hr = pDevice->CreateRenderTargetView(m_tex[s].pTexture, nullptr, &m_rtv[s]);
			if (FAILED(hr)) { ReleaseTextures(); return hr; }
		}
		m_w = w; m_h = h;
		return S_OK;
	}

	void ReleaseTextures()
	{
		for (int s = 0; s < fsrcnnx::kSlots; s++) { m_rtv[s].Release(); m_tex[s].Release(); }
		m_w = m_h = 0;
	}

	void Join()
	{
		if (m_thread.joinable()) m_thread.join();
	}

	void ReleaseBlobs()
	{
		for (auto* b : m_blobs) if (b) b->Release();
		m_blobs.clear();
	}

	std::thread m_thread;
	std::atomic<int> m_compiled{ 0 };   // 0 compiling or not started, 1 done, -1 failed
	std::atomic<bool> m_stop{ false };
	bool m_started = false;
	int m_state = kOff;
	std::vector<ID3DBlob*> m_blobs;
	std::vector<CComPtr<ID3D11PixelShader>> m_ps;
	Tex2D_t m_tex[fsrcnnx::kSlots];
	CComPtr<ID3D11RenderTargetView> m_rtv[fsrcnnx::kSlots];
	UINT m_w = 0, m_h = 0;
};
