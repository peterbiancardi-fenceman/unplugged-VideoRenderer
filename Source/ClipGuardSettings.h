#pragma once

// Native ClipGuard settings, shared by the renderer interface (Settings_t),
// the DX11 processor and the ClipGuard implementation. The add-on's
// clipguard.ini/shared-memory path is deliberately not used in Unplugged: the
// application is the single source of truth and pushes this block on every
// settings change.
struct ClipGuardSettings
{
	bool  enabled = false;       // off on a fresh install; the projector feature is opt-in
	float strength = 0.35f;      // 0..1
	int   fixShape = 1;          // 0 = Normal, 1 = Anchor
	float fixStartPct = 10.0f;   // picture brightness where the fix begins
	float fullPct = 3.0f;        // picture brightness at/under which it is full
	float knee = 0.40f;          // highlight start, picture code 0.20..0.95
	float colorProtect = 0.90f;  // 0 = strongest channel, 1 = brightness
	float textSkipPct = 25.0f;   // hold off when this much of the highlights is bright-on-black
	float hotSpot = 0.60f;       // 0..1
	float hotBelowPct = 3.0f;    // no hot spots above this picture brightness
	float gradedSpot = 0.50f;    // 0..1
	int   grid = 1;              // 0 = 128x72, 1 = 256x144
	int   lookAhead = 0;         // 0, 2, 4, 6 (forced 0 in live capture)
	int   view = 0;              // 0 normal, 1 fix areas, 2 split, 3 side by side
	bool  osd = false;           // overlay visible
	bool  osdPinned = true;      // Overlay = Always
	int   osdSelected = 0;       // selected overlay row
	// The app's visible video rect, in window pixels. Zero size = whole window.
	int   osdX = 0;
	int   osdY = 0;
	int   osdW = 0;
	int   osdH = 0;
	// One-shot log request from the app (F11). The renderer compares it with
	// the previous value and saves the last 30 s + a screenshot when it changes.
	unsigned logRequest = 0;
};