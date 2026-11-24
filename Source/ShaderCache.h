/*
 * ShaderCache: the runtime-compiled shaders (RAVU-Zoom-AR, SSimDownscaler) are compiled
 * once and kept in %LOCALAPPDATA%\ClipGuard\shaders (named by a hash of the source and
 * target, so a changed shader is compiled again). Compiler errors go to shaders.log.
 */

#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include <D3Dcompiler.h>

namespace shadercache {

inline std::wstring Dir()
{
	wchar_t d[MAX_PATH] = {};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", d, MAX_PATH)) return L"";
	std::wstring dir = std::wstring(d) + L"\\ClipGuard";
	CreateDirectoryW(dir.c_str(), nullptr);
	dir += L"\\shaders";
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

inline void Log(const std::string& text)
{
	const std::wstring dir = Dir();
	if (dir.empty()) return;
	FILE* f = nullptr;
	if (_wfopen_s(&f, (dir + L"\\shaders.log").c_str(), L"ab") == 0 && f) {
		SYSTEMTIME t; GetLocalTime(&t);
		fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, text.c_str());
		fclose(f);
	}
}

inline unsigned long long Hash(const std::string& s)
{
	unsigned long long h = 1469598103934665603ull;   // FNV-1a 64
	for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
	return h;
}

// Compiles src for target (entry "main"), or loads it from the cache. name is for the log.
inline bool Compile(const char* src, const char* target, const char* name, ID3DBlob** ppBlob)
{
	*ppBlob = nullptr;
	static HMODULE hDll = LoadLibraryW(L"d3dcompiler_47.dll");
	static pD3DCompile fnCompile = hDll ? (pD3DCompile)GetProcAddress(hDll, "D3DCompile") : nullptr;
	typedef HRESULT(WINAPI* pCreateBlob)(SIZE_T, ID3DBlob**);
	static pCreateBlob fnCreate = hDll ? (pCreateBlob)GetProcAddress(hDll, "D3DCreateBlob") : nullptr;
	if (!fnCompile || !fnCreate) { Log("d3dcompiler_47.dll not found"); return false; }

	const std::string code(src);
	wchar_t file[64];
	swprintf_s(file, L"\\%016llx.cso", Hash(std::string(target) + "\n" + code));
	const std::wstring dir = Dir();
	const std::wstring path = dir.empty() ? L"" : dir + file;

	if (!path.empty()) {
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
			fseek(f, 0, SEEK_END);
			const long n = ftell(f);
			fseek(f, 0, SEEK_SET);
			bool ok = false;
			if (n > 0 && SUCCEEDED(fnCreate((SIZE_T)n, ppBlob)) && *ppBlob) {
				ok = fread((*ppBlob)->GetBufferPointer(), 1, (size_t)n, f) == (size_t)n;
				if (!ok) { (*ppBlob)->Release(); *ppBlob = nullptr; }
			}
			fclose(f);
			if (ok) return true;
		}
	}

	ID3DBlob* err = nullptr;
	HRESULT hr = fnCompile(code.c_str(), code.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, ppBlob, &err);
	if (FAILED(hr) || !*ppBlob) {
		std::string msg = std::string(name) + " did not compile";
		if (err) msg += ": " + std::string((const char*)err->GetBufferPointer(), err->GetBufferSize());
		Log(msg);
		if (err) err->Release();
		if (*ppBlob) { (*ppBlob)->Release(); *ppBlob = nullptr; }
		return false;
	}
	if (err) err->Release();

	if (!path.empty()) {
		const std::wstring tmp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
		FILE* f = nullptr;
		if (_wfopen_s(&f, tmp.c_str(), L"wb") == 0 && f) {
			const size_t n = (*ppBlob)->GetBufferSize();
			const bool ok = fwrite((*ppBlob)->GetBufferPointer(), 1, n, f) == n;
			fclose(f);
			if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
			else DeleteFileW(tmp.c_str());
		}
	}
	return true;
}

} // namespace shadercache
