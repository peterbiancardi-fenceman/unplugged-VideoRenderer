#ifndef QUANTIZATION
    // 255 or 1023 (Maximum quantized integer value)
    #define QUANTIZATION 255
#endif

Texture2D tex : register(t0);
Texture2D texDither : register(t1);
Texture3D cubeLut : register(t2);
SamplerState samp : register(s0);
SamplerState sampDither : register(s1);
SamplerState sampCube : register(s2);

cbuffer PS_CONSTANTS : register(b0)
{
    float2 ditherCoordScale;
    float  cubeLutEnabled;   // 1.0 when a display calibration LUT is loaded
    float  cubeLutSize;      // N of LUT_3D_SIZE N
    float  ditherEnabled;    // 0.0 when this pass runs only to apply the LUT
    float  cubeLutMode;      // 1 = gamma only, 2 = greyscale, 3 = full 3D
    // Display transfer conversion. The pass is the last display-referred
    // stage, so this is where SDR-to-target and the tone map's 2.2 code are
    // re-encoded together. Spaces: 0=2.2, 1=2.4, 2=sRGB, 3=plain gamma,
    // 4=2.0. srcSpace 0 is the tone map's native 2.2; destination 0..4 is the
    // user's explicit target (Follow source never enables the conversion).
    float  convertEnabled;
    float  srcSpace;
    float  dstSpace;
    float  displayBlack;     // normalized black for BT.1886
    float  srcGamma;
    // Gamut conversion for the SDR path (the HDR tone map already converted).
    // Spaces: 0=BT.709, 1=BT.2020, 2=DCI-P3 (D65).
    float  gamutConvert;
    float  gamutSrc;
    float  gamutDst;
    float2 ditherOffset;     // ClipGuard 2.45.0: temporal dither, a new whole-texel offset each frame (0 = static)
};

#include "../convert/colorspace_gamut_conversion.hlsl"

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float SrgbToLinear(float c)
{
    c = saturate(c);
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float LinearToSrgb(float c)
{
    c = saturate(c);
    return (c <= 0.0031308) ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float DecodeInput(float c, float space, float gamma)
{
    c = saturate(c);
    if (space < 0.5) return pow(c, 2.2);
    if (space < 1.5) return pow(c, 2.4);
    if (space < 2.5) return SrgbToLinear(c);
    if (space < 3.5) return pow(c, max(gamma, 0.1));
    return c;
}

float EncodeTarget(float lin, float space, float black)
{
    lin = saturate(lin);
    if (space < 0.5) return pow(lin, 1.0 / 2.2);
    if (space < 1.5) return pow(lin, 1.0 / 2.4);
    if (space < 2.5)
    {
        // BT.1886 with the measured panel black. black 0 reduces to 2.4.
        if (lin <= black) return 0.0;
        return pow(saturate((lin - black) / max(1.0 - black, 1e-4)), 1.0 / 2.4);
    }
    if (space < 3.5) return LinearToSrgb(lin);
    if (space < 4.5) return pow(lin, 1.0 / 2.0);
    return lin;
}

float4 main(PS_INPUT input) : SV_Target
{
    float4 pixel = tex.Sample(samp, input.Tex);

    // Display transfer. The input code is either the tone map's native 2.2 or
    // the SDR source's own transfer; the destination is the user's target.
    // Follow source never enables this, so the 2.1.0 paths are untouched.
    if (convertEnabled > 0.5) {
        float3 lin;
        lin.r = DecodeInput(pixel.r, srcSpace, srcGamma);
        lin.g = DecodeInput(pixel.g, srcSpace, srcGamma);
        lin.b = DecodeInput(pixel.b, srcSpace, srcGamma);
        // SDR primaries conversion, only when the target differs from the source.
        // The HDR tone map has already converted to its selected gamut.
        if (gamutConvert > 0.5) lin = GamutConvertSpace(lin, gamutSrc, gamutDst);
        pixel.r = EncodeTarget(lin.r, dstSpace, displayBlack);
        pixel.g = EncodeTarget(lin.g, dstSpace, displayBlack);
        pixel.b = EncodeTarget(lin.b, dstSpace, displayBlack);
    }

    // ClipGuard runs in the post-scale chain, before this display-referred pass.
    // This shader only encodes the target and applies the calibration LUT/dither.

    // Display calibration LUT (.cube). This pass is the last display-referred
    // stage — after tone mapping, after the ML grade, after scaling — which is
    // the only correct place for a correction that describes the PANEL rather
    // than the signal. Trilinear over a TEXTURE3D; the half-texel offset maps
    // 0.0 and 1.0 exactly onto the corner texels instead of half a texel short.
    if (cubeLutEnabled > 0.5 && cubeLutSize > 1.0 && cubeLutMode > 0.5) {
        float n = cubeLutSize;
        float s = (n - 1.0) / n;      // grid scale
        float o = 0.5 / n;            // half-texel so 0.0 and 1.0 hit the corners
        float3 rgb = pixel.rgb;

        if (cubeLutMode < 1.5) {
            // GAMMA ONLY. The cube's neutral diagonal is its tone response; take
            // its LUMINANCE shape and apply it to all three channels equally, so
            // chromaticity passes through untouched — no cast added, none removed.
            // For a display whose grey balance was already set by hand (projector
            // gain/offset), this is the correction you want: tone, not colour.
            float y = dot(rgb, float3(0.2126, 0.7152, 0.0722));
            float3 d = cubeLut.Sample(sampCube, float3(y, y, y) * s + o).rgb;
            float y2 = dot(d, float3(0.2126, 0.7152, 0.0722));
            pixel.rgb = (y > 0.0001) ? rgb * (y2 / y) : rgb;
        }
        else if (cubeLutMode < 2.5) {
            // GREYSCALE. Per-channel diagonal: tone response AND grey balance,
            // still with no cross-channel mixing, so gamut is untouched.
            float3 d;
            d.r = cubeLut.Sample(sampCube, float3(rgb.r, rgb.r, rgb.r) * s + o).r;
            d.g = cubeLut.Sample(sampCube, float3(rgb.g, rgb.g, rgb.g) * s + o).g;
            d.b = cubeLut.Sample(sampCube, float3(rgb.b, rgb.b, rgb.b) * s + o).b;
            pixel.rgb = d;
        }
        else {
            // FULL 3D. Tone, grey balance and colour/gamut together.
            pixel.rgb = cubeLut.Sample(sampCube, rgb * s + o).rgb;
        }
    }

    if (ditherEnabled > 0.5) {
        float4 ditherValue = texDither.Sample(sampDither, input.Tex * ditherCoordScale + ditherOffset);
        pixel = floor(pixel * QUANTIZATION + ditherValue) / QUANTIZATION;
    }

    return pixel;
};