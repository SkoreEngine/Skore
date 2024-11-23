///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_SHADER_CORE_H
#define VA_SHADER_CORE_H

#define VA_SATURATE     saturate
#define VA_MIN          min
#define VA_MAX          max
#define VA_LENGTH       length
#define VA_INLINE       
#define VA_REFERENCE    
#define VA_CONST

#define vaMatrix4x4 column_major float4x4
#define vaMatrix4x3 column_major float3x4
#define vaMatrix3x3 column_major float3x3
#define vaVector4   float4
#define vaVector3   float3
#define vaVector2   float2
#define vaVector2i  int2
#define vaVector2ui uint2
#define vaVector4i  int4
#define vaVector4ui uint4

#define CONCATENATE_HELPER(a, b) a##b
#define CONCATENATE(a, b) CONCATENATE_HELPER(a, b)

#define B_CONCATENATER(x) CONCATENATE(b,x)
#define S_CONCATENATER(x) CONCATENATE(s,x)
#define T_CONCATENATER(x) CONCATENATE(t,x)
#define U_CONCATENATER(x) CONCATENATE(u,x)

#define ShaderMin( x, y )        min( x, y )

#endif // VA_SHADER_CORE_H