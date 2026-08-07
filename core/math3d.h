#pragma once

/**
 * @file math3d.h
 * @brief Core math types and operations for game development.
 *
 * Coordinate system (engine default):
 *   - Right-handed
 *   - Z-up: +X right, +Y forward, +Z up
 *   - Projection depth range: [0, 1] (near → 0, far → 1; reverse-Z later)
 *
 * Matrices are column-major (index = col * 4 + row), matching GPU upload layouts.
 * Quaternions use (x, y, z, w) with w as the scalar part; identity is (0,0,0,1).
 */

#include "common.h"

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Constants                                                                 */
/* ------------------------------------------------------------------------- */

#define SK_PI 3.14159265358979323846f
#define SK_TAU (2.0f * SK_PI)
#define SK_HALF_PI (0.5f * SK_PI)
#define SK_DEG2RAD (SK_PI / 180.0f)
#define SK_RAD2DEG (180.0f / SK_PI)
#define SK_EPSILON 1.0e-6f

/* ------------------------------------------------------------------------- */
/* Types (layout is public API — POD components)                             */
/* ------------------------------------------------------------------------- */

typedef struct sk_vec2_t {
	f32 x, y;
} sk_vec2_t;

typedef struct sk_vec3_t {
	f32 x, y, z;
} sk_vec3_t;

typedef struct sk_vec4_t {
	f32 x, y, z, w;
} sk_vec4_t;

/** Unit quaternion; rotate with q * v * conjugate(q). Identity: (0,0,0,1). */
typedef struct sk_quat_t {
	f32 x, y, z, w;
} sk_quat_t;

/**
 * 4x4 matrix, column-major.
 * Column 0: m[0..3], column 1: m[4..7], column 2: m[8..11], column 3: m[12..15].
 * Element (row, col) = m[col * 4 + row].
 */
typedef struct sk_mat44_t {
	f32 m[16];
} sk_mat44_t;

/**
 * Euler axis sequence (cglm-style packing).
 * Bits 0–1: first axis, 2–3: second, 4–5: third. Axes: 0=X, 1=Y, 2=Z.
 * Angles are applied in that order (extrinsic fixed axes / intrinsic reverse).
 */
typedef enum sk_euler_seq_t {
	SK_EULER_XYZ = 0 << 0 | 1 << 2 | 2 << 4,
	SK_EULER_XZY = 0 << 0 | 2 << 2 | 1 << 4,
	SK_EULER_YZX = 1 << 0 | 2 << 2 | 0 << 4,
	SK_EULER_YXZ = 1 << 0 | 0 << 2 | 2 << 4,
	SK_EULER_ZXY = 2 << 0 | 0 << 2 | 1 << 4,
	SK_EULER_ZYX = 2 << 0 | 1 << 2 | 0 << 4
} sk_euler_seq_t;

/* Element access: row in [0,3], col in [0,3]. */
#define SK_M44(mat, row, col) ((mat).m[(col) * 4 + (row)])

/** Decode euler sequence axis index (0=X, 1=Y, 2=Z); order 0,1,2. */
#define SK_EULER_AXIS(seq, order) (((u32)(seq) >> ((order) * 2)) & 3u)

/* ------------------------------------------------------------------------- */
/* Scalar helpers                                                            */
/* ------------------------------------------------------------------------- */

SK_FINLINE f32 sk_absf(f32 v) {
	return v < 0.0f ? -v : v;
}
SK_FINLINE f32 sk_minf(f32 a, f32 b) {
	return a < b ? a : b;
}
SK_FINLINE f32 sk_maxf(f32 a, f32 b) {
	return a > b ? a : b;
}
SK_FINLINE f32 sk_clampf(f32 v, f32 lo, f32 hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}
SK_FINLINE f32 sk_lerpf(f32 a, f32 b, f32 t) {
	return a + (b - a) * t;
}
SK_FINLINE f32 sk_radians(f32 deg) {
	return deg * SK_DEG2RAD;
}
SK_FINLINE f32 sk_degrees(f32 rad) {
	return rad * SK_RAD2DEG;
}
SK_FINLINE i32 sk_approx_eqf(f32 a, f32 b, f32 eps) {
	return sk_absf(a - b) <= eps;
}

/* ------------------------------------------------------------------------- */
/* vec2                                                                      */
/* ------------------------------------------------------------------------- */

SK_FINLINE sk_vec2_t sk_vec2(f32 x, f32 y) {
	sk_vec2_t v;
	v.x = x;
	v.y = y;
	return v;
}

SK_FINLINE sk_vec2_t sk_vec2_zero(void) {
	return sk_vec2(0.0f, 0.0f);
}
SK_FINLINE sk_vec2_t sk_vec2_one(void) {
	return sk_vec2(1.0f, 1.0f);
}

SK_FINLINE sk_vec2_t sk_vec2_add(sk_vec2_t a, sk_vec2_t b) {
	return sk_vec2(a.x + b.x, a.y + b.y);
}
SK_FINLINE sk_vec2_t sk_vec2_sub(sk_vec2_t a, sk_vec2_t b) {
	return sk_vec2(a.x - b.x, a.y - b.y);
}
SK_FINLINE sk_vec2_t sk_vec2_mul(sk_vec2_t a, sk_vec2_t b) {
	return sk_vec2(a.x * b.x, a.y * b.y);
}
SK_FINLINE sk_vec2_t sk_vec2_div(sk_vec2_t a, sk_vec2_t b) {
	return sk_vec2(a.x / b.x, a.y / b.y);
}
SK_FINLINE sk_vec2_t sk_vec2_scale(sk_vec2_t v, f32 s) {
	return sk_vec2(v.x * s, v.y * s);
}
SK_FINLINE sk_vec2_t sk_vec2_neg(sk_vec2_t v) {
	return sk_vec2(-v.x, -v.y);
}

SK_FINLINE f32 sk_vec2_dot(sk_vec2_t a, sk_vec2_t b) {
	return a.x * b.x + a.y * b.y;
}
SK_FINLINE f32 sk_vec2_cross(sk_vec2_t a, sk_vec2_t b) {
	/* 2D cross as scalar (z-component of 3D cross). */
	return a.x * b.y - a.y * b.x;
}
SK_FINLINE f32 sk_vec2_length_sq(sk_vec2_t v) {
	return sk_vec2_dot(v, v);
}
SK_FINLINE f32 sk_vec2_length(sk_vec2_t v) {
	return sqrtf(sk_vec2_length_sq(v));
}
SK_FINLINE f32 sk_vec2_distance(sk_vec2_t a, sk_vec2_t b) {
	return sk_vec2_length(sk_vec2_sub(a, b));
}
SK_FINLINE sk_vec2_t sk_vec2_normalize(sk_vec2_t v) {
	f32 len = sk_vec2_length(v);
	if (len > SK_EPSILON) {
		return sk_vec2_scale(v, 1.0f / len);
	}
	return sk_vec2_zero();
}
SK_FINLINE sk_vec2_t sk_vec2_lerp(sk_vec2_t a, sk_vec2_t b, f32 t) {
	return sk_vec2(sk_lerpf(a.x, b.x, t), sk_lerpf(a.y, b.y, t));
}
SK_FINLINE sk_vec2_t sk_vec2_min(sk_vec2_t a, sk_vec2_t b) {
	return sk_vec2(sk_minf(a.x, b.x), sk_minf(a.y, b.y));
}
SK_FINLINE sk_vec2_t sk_vec2_max(sk_vec2_t a, sk_vec2_t b) {
	return sk_vec2(sk_maxf(a.x, b.x), sk_maxf(a.y, b.y));
}
SK_FINLINE sk_vec2_t sk_vec2_abs(sk_vec2_t v) {
	return sk_vec2(sk_absf(v.x), sk_absf(v.y));
}
SK_FINLINE sk_vec2_t sk_vec2_clamp(sk_vec2_t v, sk_vec2_t lo, sk_vec2_t hi) {
	return sk_vec2(sk_clampf(v.x, lo.x, hi.x), sk_clampf(v.y, lo.y, hi.y));
}
SK_FINLINE i32 sk_vec2_approx_eq(sk_vec2_t a, sk_vec2_t b, f32 eps) {
	return sk_approx_eqf(a.x, b.x, eps) && sk_approx_eqf(a.y, b.y, eps);
}

/* ------------------------------------------------------------------------- */
/* vec3                                                                      */
/* ------------------------------------------------------------------------- */

SK_FINLINE sk_vec3_t sk_vec3(f32 x, f32 y, f32 z) {
	sk_vec3_t v;
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

SK_FINLINE sk_vec3_t sk_vec3_zero(void) {
	return sk_vec3(0.0f, 0.0f, 0.0f);
}
SK_FINLINE sk_vec3_t sk_vec3_one(void) {
	return sk_vec3(1.0f, 1.0f, 1.0f);
}
SK_FINLINE sk_vec3_t sk_vec3_right(void) {
	return sk_vec3(1.0f, 0.0f, 0.0f);
}
SK_FINLINE sk_vec3_t sk_vec3_forward(void) {
	return sk_vec3(0.0f, 1.0f, 0.0f);
}
SK_FINLINE sk_vec3_t sk_vec3_up(void) {
	return sk_vec3(0.0f, 0.0f, 1.0f);
}

SK_FINLINE sk_vec3_t sk_vec3_add(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}
SK_FINLINE sk_vec3_t sk_vec3_sub(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}
SK_FINLINE sk_vec3_t sk_vec3_mul(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}
SK_FINLINE sk_vec3_t sk_vec3_div(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3(a.x / b.x, a.y / b.y, a.z / b.z);
}
SK_FINLINE sk_vec3_t sk_vec3_scale(sk_vec3_t v, f32 s) {
	return sk_vec3(v.x * s, v.y * s, v.z * s);
}
SK_FINLINE sk_vec3_t sk_vec3_neg(sk_vec3_t v) {
	return sk_vec3(-v.x, -v.y, -v.z);
}

SK_FINLINE f32 sk_vec3_dot(sk_vec3_t a, sk_vec3_t b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
SK_FINLINE sk_vec3_t sk_vec3_cross(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
SK_FINLINE f32 sk_vec3_length_sq(sk_vec3_t v) {
	return sk_vec3_dot(v, v);
}
SK_FINLINE f32 sk_vec3_length(sk_vec3_t v) {
	return sqrtf(sk_vec3_length_sq(v));
}
SK_FINLINE f32 sk_vec3_distance(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3_length(sk_vec3_sub(a, b));
}
SK_FINLINE sk_vec3_t sk_vec3_normalize(sk_vec3_t v) {
	f32 len = sk_vec3_length(v);
	if (len > SK_EPSILON) {
		return sk_vec3_scale(v, 1.0f / len);
	}
	return sk_vec3_zero();
}
SK_FINLINE sk_vec3_t sk_vec3_lerp(sk_vec3_t a, sk_vec3_t b, f32 t) {
	return sk_vec3(sk_lerpf(a.x, b.x, t), sk_lerpf(a.y, b.y, t), sk_lerpf(a.z, b.z, t));
}
SK_FINLINE sk_vec3_t sk_vec3_min(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3(sk_minf(a.x, b.x), sk_minf(a.y, b.y), sk_minf(a.z, b.z));
}
SK_FINLINE sk_vec3_t sk_vec3_max(sk_vec3_t a, sk_vec3_t b) {
	return sk_vec3(sk_maxf(a.x, b.x), sk_maxf(a.y, b.y), sk_maxf(a.z, b.z));
}
SK_FINLINE sk_vec3_t sk_vec3_abs(sk_vec3_t v) {
	return sk_vec3(sk_absf(v.x), sk_absf(v.y), sk_absf(v.z));
}
SK_FINLINE sk_vec3_t sk_vec3_clamp(sk_vec3_t v, sk_vec3_t lo, sk_vec3_t hi) {
	return sk_vec3(sk_clampf(v.x, lo.x, hi.x), sk_clampf(v.y, lo.y, hi.y), sk_clampf(v.z, lo.z, hi.z));
}
SK_FINLINE sk_vec3_t sk_vec3_reflect(sk_vec3_t v, sk_vec3_t n) {
	/* n should be normalized. r = v - 2 * dot(v,n) * n */
	return sk_vec3_sub(v, sk_vec3_scale(n, 2.0f * sk_vec3_dot(v, n)));
}
SK_FINLINE i32 sk_vec3_approx_eq(sk_vec3_t a, sk_vec3_t b, f32 eps) {
	return sk_approx_eqf(a.x, b.x, eps) && sk_approx_eqf(a.y, b.y, eps) && sk_approx_eqf(a.z, b.z, eps);
}

SK_FINLINE sk_vec2_t sk_vec3_xy(sk_vec3_t v) {
	return sk_vec2(v.x, v.y);
}
SK_FINLINE sk_vec3_t sk_vec3_from_vec2(sk_vec2_t v, f32 z) {
	return sk_vec3(v.x, v.y, z);
}

/* ------------------------------------------------------------------------- */
/* vec4                                                                      */
/* ------------------------------------------------------------------------- */

SK_FINLINE sk_vec4_t sk_vec4(f32 x, f32 y, f32 z, f32 w) {
	sk_vec4_t v;
	v.x = x;
	v.y = y;
	v.z = z;
	v.w = w;
	return v;
}

SK_FINLINE sk_vec4_t sk_vec4_zero(void) {
	return sk_vec4(0.0f, 0.0f, 0.0f, 0.0f);
}
SK_FINLINE sk_vec4_t sk_vec4_one(void) {
	return sk_vec4(1.0f, 1.0f, 1.0f, 1.0f);
}

SK_FINLINE sk_vec4_t sk_vec4_from_vec3(sk_vec3_t v, f32 w) {
	return sk_vec4(v.x, v.y, v.z, w);
}
SK_FINLINE sk_vec3_t sk_vec4_xyz(sk_vec4_t v) {
	return sk_vec3(v.x, v.y, v.z);
}

SK_FINLINE sk_vec4_t sk_vec4_add(sk_vec4_t a, sk_vec4_t b) {
	return sk_vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
SK_FINLINE sk_vec4_t sk_vec4_sub(sk_vec4_t a, sk_vec4_t b) {
	return sk_vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}
SK_FINLINE sk_vec4_t sk_vec4_mul(sk_vec4_t a, sk_vec4_t b) {
	return sk_vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
}
SK_FINLINE sk_vec4_t sk_vec4_scale(sk_vec4_t v, f32 s) {
	return sk_vec4(v.x * s, v.y * s, v.z * s, v.w * s);
}
SK_FINLINE sk_vec4_t sk_vec4_neg(sk_vec4_t v) {
	return sk_vec4(-v.x, -v.y, -v.z, -v.w);
}

SK_FINLINE f32 sk_vec4_dot(sk_vec4_t a, sk_vec4_t b) {
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
SK_FINLINE f32 sk_vec4_length_sq(sk_vec4_t v) {
	return sk_vec4_dot(v, v);
}
SK_FINLINE f32 sk_vec4_length(sk_vec4_t v) {
	return sqrtf(sk_vec4_length_sq(v));
}
SK_FINLINE sk_vec4_t sk_vec4_normalize(sk_vec4_t v) {
	f32 len = sk_vec4_length(v);
	if (len > SK_EPSILON) {
		return sk_vec4_scale(v, 1.0f / len);
	}
	return sk_vec4_zero();
}
SK_FINLINE sk_vec4_t sk_vec4_lerp(sk_vec4_t a, sk_vec4_t b, f32 t) {
	return sk_vec4(sk_lerpf(a.x, b.x, t), sk_lerpf(a.y, b.y, t), sk_lerpf(a.z, b.z, t), sk_lerpf(a.w, b.w, t));
}
SK_FINLINE i32 sk_vec4_approx_eq(sk_vec4_t a, sk_vec4_t b, f32 eps) {
	return sk_approx_eqf(a.x, b.x, eps) && sk_approx_eqf(a.y, b.y, eps) && sk_approx_eqf(a.z, b.z, eps) && sk_approx_eqf(a.w, b.w, eps);
}

/* ------------------------------------------------------------------------- */
/* quat                                                                      */
/* ------------------------------------------------------------------------- */

SK_FINLINE sk_quat_t sk_quat(f32 x, f32 y, f32 z, f32 w) {
	sk_quat_t q;
	q.x = x;
	q.y = y;
	q.z = z;
	q.w = w;
	return q;
}

SK_FINLINE sk_quat_t sk_quat_identity(void) {
	return sk_quat(0.0f, 0.0f, 0.0f, 1.0f);
}

SK_FINLINE sk_quat_t sk_quat_add(sk_quat_t a, sk_quat_t b) {
	return sk_quat(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
SK_FINLINE sk_quat_t sk_quat_scale(sk_quat_t q, f32 s) {
	return sk_quat(q.x * s, q.y * s, q.z * s, q.w * s);
}
SK_FINLINE sk_quat_t sk_quat_neg(sk_quat_t q) {
	return sk_quat(-q.x, -q.y, -q.z, -q.w);
}
SK_FINLINE sk_quat_t sk_quat_conjugate(sk_quat_t q) {
	return sk_quat(-q.x, -q.y, -q.z, q.w);
}

SK_FINLINE f32 sk_quat_dot(sk_quat_t a, sk_quat_t b) {
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
SK_FINLINE f32 sk_quat_length_sq(sk_quat_t q) {
	return sk_quat_dot(q, q);
}
SK_FINLINE f32 sk_quat_length(sk_quat_t q) {
	return sqrtf(sk_quat_length_sq(q));
}
SK_FINLINE sk_quat_t sk_quat_normalize(sk_quat_t q) {
	f32 len = sk_quat_length(q);
	if (len > SK_EPSILON) {
		return sk_quat_scale(q, 1.0f / len);
	}
	return sk_quat_identity();
}
SK_FINLINE sk_quat_t sk_quat_inverse(sk_quat_t q) {
	f32 len_sq = sk_quat_length_sq(q);
	if (len_sq > SK_EPSILON) {
		return sk_quat_scale(sk_quat_conjugate(q), 1.0f / len_sq);
	}
	return sk_quat_identity();
}

/** Hamilton product: applies rotation b then a (a * b). */
SK_FINLINE sk_quat_t sk_quat_mul(sk_quat_t a, sk_quat_t b) {
	return sk_quat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
				   a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

/** Axis-angle; axis need not be unit (normalized here). Angle in radians. */
SK_FINLINE sk_quat_t sk_quat_from_axis_angle(sk_vec3_t axis, f32 angle_rad) {
	sk_vec3_t n = sk_vec3_normalize(axis);
	f32 half = angle_rad * 0.5f;
	f32 s = sinf(half);
	return sk_quat(n.x * s, n.y * s, n.z * s, cosf(half));
}

/** Unit axis vector for Euler packing: 0=X, 1=Y, 2=Z. */
SK_FINLINE sk_vec3_t sk_euler_axis_vec(u32 axis) {
	switch (axis & 3u) {
	case 0u:
		return sk_vec3(1.0f, 0.0f, 0.0f);
	case 1u:
		return sk_vec3(0.0f, 1.0f, 0.0f);
	default:
		return sk_vec3(0.0f, 0.0f, 1.0f);
	}
}

/**
 * Quaternion from Euler angles (radians) in the given axis sequence.
 * angles.x/y/z are the rotations about the 1st/2nd/3rd axes of @p seq.
 * Composition: q2 * q1 * q0 (apply first axis angle, then second, then third).
 */
SK_FINLINE sk_quat_t sk_quat_from_euler(sk_vec3_t angles, sk_euler_seq_t seq) {
	sk_quat_t q0 = sk_quat_from_axis_angle(sk_euler_axis_vec(SK_EULER_AXIS(seq, 0)), angles.x);
	sk_quat_t q1 = sk_quat_from_axis_angle(sk_euler_axis_vec(SK_EULER_AXIS(seq, 1)), angles.y);
	sk_quat_t q2 = sk_quat_from_axis_angle(sk_euler_axis_vec(SK_EULER_AXIS(seq, 2)), angles.z);
	return sk_quat_mul(q2, sk_quat_mul(q1, q0));
}

/**
 * Z-up yaw/pitch/roll convenience (radians).
 * yaw about +Z, pitch about +X, roll about +Y.
 * Composition: yaw * pitch * roll (apply roll, then pitch, then yaw).
 * Equivalent to sk_quat_from_euler(sk_vec3(roll, pitch, yaw), SK_EULER_YXZ).
 */
SK_FINLINE sk_quat_t sk_quat_from_euler_ypr(f32 yaw, f32 pitch, f32 roll) {
	return sk_quat_from_euler(sk_vec3(roll, pitch, yaw), SK_EULER_YXZ);
}

/** Rotate vector by unit quaternion: q * v * q^-1. */
SK_FINLINE sk_vec3_t sk_quat_rotate_vec3(sk_quat_t q, sk_vec3_t v) {
	/* Optimized form (no full quat mul). Assumes q approximately unit. */
	sk_vec3_t u = sk_vec3(q.x, q.y, q.z);
	f32 s = q.w;
	sk_vec3_t t = sk_vec3_scale(sk_vec3_cross(u, v), 2.0f);
	return sk_vec3_add(v, sk_vec3_add(sk_vec3_scale(t, s), sk_vec3_cross(u, t)));
}

SK_FINLINE sk_quat_t sk_quat_nlerp(sk_quat_t a, sk_quat_t b, f32 t) {
	/* Shortest path. */
	if (sk_quat_dot(a, b) < 0.0f) {
		b = sk_quat_neg(b);
	}
	return sk_quat_normalize(sk_quat(sk_lerpf(a.x, b.x, t), sk_lerpf(a.y, b.y, t), sk_lerpf(a.z, b.z, t), sk_lerpf(a.w, b.w, t)));
}

/** Spherical linear interpolation (unit quats). */
sk_quat_t sk_quat_slerp(sk_quat_t a, sk_quat_t b, f32 t);

SK_FINLINE i32 sk_quat_approx_eq(sk_quat_t a, sk_quat_t b, f32 eps) {
	/* Quats q and -q represent the same rotation. */
	if (sk_approx_eqf(a.x, b.x, eps) && sk_approx_eqf(a.y, b.y, eps) && sk_approx_eqf(a.z, b.z, eps) && sk_approx_eqf(a.w, b.w, eps)) {
		return 1;
	}
	return sk_approx_eqf(a.x, -b.x, eps) && sk_approx_eqf(a.y, -b.y, eps) && sk_approx_eqf(a.z, -b.z, eps) && sk_approx_eqf(a.w, -b.w, eps);
}

/* ------------------------------------------------------------------------- */
/* mat44                                                                     */
/* ------------------------------------------------------------------------- */

SK_FINLINE sk_mat44_t sk_mat44_zero(void) {
	sk_mat44_t m;
	i32 i;
	for (i = 0; i < 16; ++i) {
		m.m[i] = 0.0f;
	}
	return m;
}

SK_FINLINE sk_mat44_t sk_mat44_identity(void) {
	sk_mat44_t m = sk_mat44_zero();
	m.m[0] = 1.0f;
	m.m[5] = 1.0f;
	m.m[10] = 1.0f;
	m.m[15] = 1.0f;
	return m;
}

SK_FINLINE sk_mat44_t sk_mat44_translation(sk_vec3_t t) {
	sk_mat44_t m = sk_mat44_identity();
	m.m[12] = t.x;
	m.m[13] = t.y;
	m.m[14] = t.z;
	return m;
}

SK_FINLINE sk_mat44_t sk_mat44_scale(sk_vec3_t s) {
	sk_mat44_t m = sk_mat44_zero();
	m.m[0] = s.x;
	m.m[5] = s.y;
	m.m[10] = s.z;
	m.m[15] = 1.0f;
	return m;
}

SK_FINLINE sk_mat44_t sk_mat44_scale_uniform(f32 s) {
	return sk_mat44_scale(sk_vec3(s, s, s));
}

/** Rotation about +X (right). Angle in radians. */
SK_FINLINE sk_mat44_t sk_mat44_rotation_x(f32 angle_rad) {
	f32 c = cosf(angle_rad);
	f32 s = sinf(angle_rad);
	sk_mat44_t m = sk_mat44_identity();
	m.m[5] = c;
	m.m[6] = s;
	m.m[9] = -s;
	m.m[10] = c;
	return m;
}

/** Rotation about +Y (forward). Angle in radians. */
SK_FINLINE sk_mat44_t sk_mat44_rotation_y(f32 angle_rad) {
	f32 c = cosf(angle_rad);
	f32 s = sinf(angle_rad);
	sk_mat44_t m = sk_mat44_identity();
	m.m[0] = c;
	m.m[2] = -s;
	m.m[8] = s;
	m.m[10] = c;
	return m;
}

/** Rotation about +Z (up). Angle in radians. */
SK_FINLINE sk_mat44_t sk_mat44_rotation_z(f32 angle_rad) {
	f32 c = cosf(angle_rad);
	f32 s = sinf(angle_rad);
	sk_mat44_t m = sk_mat44_identity();
	m.m[0] = c;
	m.m[1] = s;
	m.m[4] = -s;
	m.m[5] = c;
	return m;
}

/** Rotation matrix from unit (or near-unit) quaternion. */
SK_FINLINE sk_mat44_t sk_mat44_from_quat(sk_quat_t q) {
	q = sk_quat_normalize(q);
	f32 x = q.x, y = q.y, z = q.z, w = q.w;
	f32 xx = x * x, yy = y * y, zz = z * z;
	f32 xy = x * y, xz = x * z, yz = y * z;
	f32 wx = w * x, wy = w * y, wz = w * z;

	sk_mat44_t m = sk_mat44_zero();
	m.m[0] = 1.0f - 2.0f * (yy + zz);
	m.m[1] = 2.0f * (xy + wz);
	m.m[2] = 2.0f * (xz - wy);
	m.m[3] = 0.0f;

	m.m[4] = 2.0f * (xy - wz);
	m.m[5] = 1.0f - 2.0f * (xx + zz);
	m.m[6] = 2.0f * (yz + wx);
	m.m[7] = 0.0f;

	m.m[8] = 2.0f * (xz + wy);
	m.m[9] = 2.0f * (yz - wx);
	m.m[10] = 1.0f - 2.0f * (xx + yy);
	m.m[11] = 0.0f;

	m.m[12] = 0.0f;
	m.m[13] = 0.0f;
	m.m[14] = 0.0f;
	m.m[15] = 1.0f;
	return m;
}

/**
 * TRS: T * R * S (scale first, then rotate, then translate).
 * Column-major composition: sk_mat44_mul(T, sk_mat44_mul(R, S)).
 */
SK_FINLINE sk_mat44_t sk_mat44_trs(sk_vec3_t t, sk_quat_t r, sk_vec3_t s) {
	sk_mat44_t R = sk_mat44_from_quat(r);
	sk_mat44_t m;

	/* Columns = R * scale axes, translation in last column. */
	m.m[0] = R.m[0] * s.x;
	m.m[1] = R.m[1] * s.x;
	m.m[2] = R.m[2] * s.x;
	m.m[3] = 0.0f;

	m.m[4] = R.m[4] * s.y;
	m.m[5] = R.m[5] * s.y;
	m.m[6] = R.m[6] * s.y;
	m.m[7] = 0.0f;

	m.m[8] = R.m[8] * s.z;
	m.m[9] = R.m[9] * s.z;
	m.m[10] = R.m[10] * s.z;
	m.m[11] = 0.0f;

	m.m[12] = t.x;
	m.m[13] = t.y;
	m.m[14] = t.z;
	m.m[15] = 1.0f;
	return m;
}

SK_FINLINE sk_vec3_t sk_mat44_get_translation(sk_mat44_t m) {
	return sk_vec3(m.m[12], m.m[13], m.m[14]);
}

SK_FINLINE sk_mat44_t sk_mat44_set_translation(sk_mat44_t m, sk_vec3_t t) {
	m.m[12] = t.x;
	m.m[13] = t.y;
	m.m[14] = t.z;
	return m;
}

/** Matrix multiply: result = a * b (apply b first, then a). */
SK_FINLINE sk_mat44_t sk_mat44_mul(sk_mat44_t a, sk_mat44_t b) {
	sk_mat44_t r;
	i32 col, row;
	for (col = 0; col < 4; ++col) {
		for (row = 0; row < 4; ++row) {
			r.m[col * 4 + row] = a.m[0 * 4 + row] * b.m[col * 4 + 0] + a.m[1 * 4 + row] * b.m[col * 4 + 1] + a.m[2 * 4 + row] * b.m[col * 4 + 2] +
								 a.m[3 * 4 + row] * b.m[col * 4 + 3];
		}
	}
	return r;
}

SK_FINLINE sk_mat44_t sk_mat44_transpose(sk_mat44_t m) {
	sk_mat44_t r;
	i32 col, row;
	for (col = 0; col < 4; ++col) {
		for (row = 0; row < 4; ++row) {
			r.m[col * 4 + row] = m.m[row * 4 + col];
		}
	}
	return r;
}

SK_FINLINE sk_vec4_t sk_mat44_mul_vec4(sk_mat44_t m, sk_vec4_t v) {
	return sk_vec4(m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w, m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w,
				   m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w, m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w);
}

/** Transform point (w = 1). */
SK_FINLINE sk_vec3_t sk_mat44_transform_point(sk_mat44_t m, sk_vec3_t p) {
	sk_vec4_t r = sk_mat44_mul_vec4(m, sk_vec4(p.x, p.y, p.z, 1.0f));
	if (sk_absf(r.w) > SK_EPSILON && sk_absf(r.w - 1.0f) > SK_EPSILON) {
		f32 inv_w = 1.0f / r.w;
		return sk_vec3(r.x * inv_w, r.y * inv_w, r.z * inv_w);
	}
	return sk_vec3(r.x, r.y, r.z);
}

/** Transform direction / free vector (w = 0); ignores translation. */
SK_FINLINE sk_vec3_t sk_mat44_transform_vector(sk_mat44_t m, sk_vec3_t v) {
	return sk_vec3(m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z, m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z, m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z);
}

/**
 * Inverse of a general 4x4. Returns identity if determinant is near zero.
 * Prefer sk_mat44_inverse_trs for pure TRS matrices.
 */
sk_mat44_t sk_mat44_inverse(sk_mat44_t m);

/**
 * Fast inverse for TRS with uniform or non-uniform positive scale
 * (no shear, last row [0,0,0,1]).
 */
sk_mat44_t sk_mat44_inverse_affine(sk_mat44_t m);

/**
 * Look-at view matrix (world → view). Engine default: right-handed, Z-up.
 * Camera looks toward -Z in view space; world up is typically (0,0,1).
 */
sk_mat44_t sk_mat44_look_at(sk_vec3_t eye, sk_vec3_t target, sk_vec3_t up);

/**
 * Perspective projection. Engine default: right-handed, depth [0, 1]
 * (near → 0, far → 1).
 * @param fovy_rad vertical field of view in radians
 * @param aspect   width / height
 * @param z_near   near plane (> 0)
 * @param z_far    far plane (> z_near)
 */
sk_mat44_t sk_mat44_perspective(f32 fovy_rad, f32 aspect, f32 z_near, f32 z_far);

/**
 * Orthographic projection. Engine default: right-handed, depth [0, 1].
 * @param left,right,bottom,top  frustum edges in view space
 * @param z_near,z_far           depth planes
 */
sk_mat44_t sk_mat44_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 z_near, f32 z_far);

SK_FINLINE i32 sk_mat44_approx_eq(sk_mat44_t a, sk_mat44_t b, f32 eps) {
	i32 i;
	for (i = 0; i < 16; ++i) {
		if (!sk_approx_eqf(a.m[i], b.m[i], eps)) {
			return 0;
		}
	}
	return 1;
}

#ifdef __cplusplus
}
#endif
