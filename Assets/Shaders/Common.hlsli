// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef SK_COMMON_HLSLI
#define SK_COMMON_HLSLI


static const float PI = 3.14159265359;
static const float TwoPI = 2 * PI;
static const float Epsilon = 0.0001f;
static const float minGGXAlpha = 0.0064f;

#define SK_COL32_R_SHIFT    0
#define SK_COL32_G_SHIFT    8
#define SK_COL32_B_SHIFT    16
#define SK_COL32_A_SHIFT    24

float4 ColorFromU32(uint value)
{
	return float4(
			((value >> SK_COL32_R_SHIFT) & 0xFF) / 255.0,
			((value >> SK_COL32_G_SHIFT) & 0xFF) / 255.0,
			((value >> SK_COL32_B_SHIFT) & 0xFF) / 255.0,
			((value >> SK_COL32_A_SHIFT) & 0xFF) / 255.0
	);
}


float3 GetSamplingVector(float outputWidth, float outputHeight, float outputDepth, uint3 threadID)
{
	float2 st = threadID.xy/float2(outputWidth, outputHeight);
	float2 uv = 2.0 * float2(st.x, 1.0-st.y) - 1.0;

	// Select vector based on cubemap face index.
	float3 ret;
	switch(threadID.z)
	{
		case 0: ret = float3(1.0,  uv.y, -uv.x); break;
		case 1: ret = float3(-1.0, uv.y,  uv.x); break;
		case 2: ret = float3(uv.x, 1.0, -uv.y); break;
		case 3: ret = float3(uv.x, -1.0, uv.y); break;
		case 4: ret = float3(uv.x, uv.y, 1.0); break;
		case 5: ret = float3(-uv.x, uv.y, -1.0); break;
	}
	return normalize(ret);
}



#endif

