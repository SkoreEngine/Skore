#include "math3d.h"

sk_quat_t sk_quat_slerp(sk_quat_t a, sk_quat_t b, f32 t) {
	f32 cos_omega = sk_quat_dot(a, b);

	/* Shortest path. */
	if (cos_omega < 0.0f) {
		b = sk_quat_neg(b);
		cos_omega = -cos_omega;
	}

	/* Near-parallel: fall back to nlerp. */
	if (cos_omega > 0.9995f) {
		return sk_quat_nlerp(a, b, t);
	}

	f32 omega = acosf(sk_clampf(cos_omega, -1.0f, 1.0f));
	f32 sin_omega = sinf(omega);
	f32 inv_sin = 1.0f / sin_omega;
	f32 wa = sinf((1.0f - t) * omega) * inv_sin;
	f32 wb = sinf(t * omega) * inv_sin;

	return sk_quat_normalize(sk_quat(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb));
}

sk_mat44_t sk_mat44_inverse(sk_mat44_t m) {
	/* Cramer's rule on 4x4 (column-major). */
	f32 a00 = m.m[0], a01 = m.m[4], a02 = m.m[8], a03 = m.m[12];
	f32 a10 = m.m[1], a11 = m.m[5], a12 = m.m[9], a13 = m.m[13];
	f32 a20 = m.m[2], a21 = m.m[6], a22 = m.m[10], a23 = m.m[14];
	f32 a30 = m.m[3], a31 = m.m[7], a32 = m.m[11], a33 = m.m[15];

	f32 b00 = a00 * a11 - a01 * a10;
	f32 b01 = a00 * a12 - a02 * a10;
	f32 b02 = a00 * a13 - a03 * a10;
	f32 b03 = a01 * a12 - a02 * a11;
	f32 b04 = a01 * a13 - a03 * a11;
	f32 b05 = a02 * a13 - a03 * a12;
	f32 b06 = a20 * a31 - a21 * a30;
	f32 b07 = a20 * a32 - a22 * a30;
	f32 b08 = a20 * a33 - a23 * a30;
	f32 b09 = a21 * a32 - a22 * a31;
	f32 b10 = a21 * a33 - a23 * a31;
	f32 b11 = a22 * a33 - a23 * a32;

	f32 det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	if (sk_absf(det) < SK_EPSILON) {
		return sk_mat44_identity();
	}
	f32 inv_det = 1.0f / det;

	sk_mat44_t r;
	r.m[0] = (a11 * b11 - a12 * b10 + a13 * b09) * inv_det;
	r.m[1] = (a12 * b08 - a10 * b11 - a13 * b07) * inv_det;
	r.m[2] = (a10 * b10 - a11 * b08 + a13 * b06) * inv_det;
	r.m[3] = (a11 * b07 - a10 * b09 - a12 * b06) * inv_det;

	r.m[4] = (a02 * b10 - a01 * b11 - a03 * b09) * inv_det;
	r.m[5] = (a00 * b11 - a02 * b08 + a03 * b07) * inv_det;
	r.m[6] = (a01 * b08 - a00 * b10 - a03 * b06) * inv_det;
	r.m[7] = (a00 * b09 - a01 * b07 + a02 * b06) * inv_det;

	r.m[8] = (a31 * b05 - a32 * b04 + a33 * b03) * inv_det;
	r.m[9] = (a32 * b02 - a30 * b05 - a33 * b01) * inv_det;
	r.m[10] = (a30 * b04 - a31 * b02 + a33 * b00) * inv_det;
	r.m[11] = (a31 * b01 - a30 * b03 - a32 * b00) * inv_det;

	r.m[12] = (a22 * b04 - a21 * b05 - a23 * b03) * inv_det;
	r.m[13] = (a20 * b05 - a22 * b02 + a23 * b01) * inv_det;
	r.m[14] = (a21 * b02 - a20 * b04 - a23 * b00) * inv_det;
	r.m[15] = (a20 * b03 - a21 * b01 + a22 * b00) * inv_det;
	return r;
}

sk_mat44_t sk_mat44_inverse_affine(sk_mat44_t m) {
	/*
     * Invert upper-left 3x3 and translation for matrices with last row [0,0,0,1].
     * Layout (column-major):
     *   | a b c tx |
     *   | d e f ty |
     *   | g h i tz |
     *   | 0 0 0 1  |
     */
	f32 a = m.m[0], b = m.m[4], c = m.m[8];
	f32 d = m.m[1], e = m.m[5], f = m.m[9];
	f32 g = m.m[2], h = m.m[6], i = m.m[10];
	f32 tx = m.m[12], ty = m.m[13], tz = m.m[14];

	f32 det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (sk_absf(det) < SK_EPSILON) {
		return sk_mat44_identity();
	}
	f32 inv_det = 1.0f / det;

	/* inv3 = (1/det) * cofactor^T */
	f32 ia = (e * i - f * h) * inv_det;
	f32 ib = (c * h - b * i) * inv_det;
	f32 ic = (b * f - c * e) * inv_det;
	f32 id = (f * g - d * i) * inv_det;
	f32 ie = (a * i - c * g) * inv_det;
	f32 i_f = (c * d - a * f) * inv_det;
	f32 ig = (d * h - e * g) * inv_det;
	f32 ih = (b * g - a * h) * inv_det;
	f32 ii = (a * e - b * d) * inv_det;

	sk_mat44_t r = sk_mat44_zero();
	r.m[0] = ia;
	r.m[1] = id;
	r.m[2] = ig;
	r.m[4] = ib;
	r.m[5] = ie;
	r.m[6] = ih;
	r.m[8] = ic;
	r.m[9] = i_f;
	r.m[10] = ii;
	r.m[15] = 1.0f;

	/* inv_t = -inv3 * t */
	r.m[12] = -(ia * tx + ib * ty + ic * tz);
	r.m[13] = -(id * tx + ie * ty + i_f * tz);
	r.m[14] = -(ig * tx + ih * ty + ii * tz);
	return r;
}

sk_mat44_t sk_mat44_look_at(sk_vec3_t eye, sk_vec3_t target, sk_vec3_t up) {
	sk_vec3_t f = sk_vec3_normalize(sk_vec3_sub(target, eye)); /* forward */
	sk_vec3_t s_raw = sk_vec3_cross(f, up);					   /* right (pre-norm) */
	/* If f is parallel to up, cross is zero — pick an alternate world axis. */
	if (sk_vec3_length_sq(s_raw) < (SK_EPSILON * SK_EPSILON)) {
		sk_vec3_t alt = sk_vec3_right();
		if (sk_absf(sk_vec3_dot(f, alt)) > 0.99f) {
			alt = sk_vec3_forward();
		}
		s_raw = sk_vec3_cross(f, alt);
	}
	sk_vec3_t s = sk_vec3_normalize(s_raw);
	sk_vec3_t u = sk_vec3_cross(s, f); /* true up */

	sk_mat44_t m = sk_mat44_identity();
	/* Column-major view: rows are s, u, -f (camera looks down -Z in view space). */
	m.m[0] = s.x;
	m.m[4] = s.y;
	m.m[8] = s.z;
	m.m[1] = u.x;
	m.m[5] = u.y;
	m.m[9] = u.z;
	m.m[2] = -f.x;
	m.m[6] = -f.y;
	m.m[10] = -f.z;
	m.m[12] = -sk_vec3_dot(s, eye);
	m.m[13] = -sk_vec3_dot(u, eye);
	m.m[14] = sk_vec3_dot(f, eye);
	return m;
}

sk_mat44_t sk_mat44_perspective(f32 fovy_rad, f32 aspect, f32 z_near, f32 z_far) {
	sk_mat44_t m = sk_mat44_zero();
	f32 tan_half = tanf(fovy_rad * 0.5f);
	if (sk_absf(tan_half) < SK_EPSILON || sk_absf(aspect) < SK_EPSILON) {
		return sk_mat44_identity();
	}

	m.m[0] = 1.0f / (aspect * tan_half);
	m.m[5] = 1.0f / tan_half;
	m.m[10] = z_far / (z_near - z_far); /* maps far → 1, near → 0 */
	m.m[11] = -1.0f;					/* RH: perspective divide */
	m.m[14] = -(z_far * z_near) / (z_far - z_near);
	return m;
}

sk_mat44_t sk_mat44_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 z_near, f32 z_far) {
	sk_mat44_t m = sk_mat44_zero();
	f32 rl = right - left;
	f32 tb = top - bottom;
	f32 fn = z_far - z_near;
	if (sk_absf(rl) < SK_EPSILON || sk_absf(tb) < SK_EPSILON || sk_absf(fn) < SK_EPSILON) {
		return sk_mat44_identity();
	}

	m.m[0] = 2.0f / rl;
	m.m[5] = 2.0f / tb;
	m.m[10] = -1.0f / fn; /* RH, depth [0,1] */
	m.m[12] = -(right + left) / rl;
	m.m[13] = -(top + bottom) / tb;
	m.m[14] = -z_near / fn;
	m.m[15] = 1.0f;
	return m;
}

#ifdef SK_TESTS
#include "test.h"

SK_TEST(vec2_arithmetic_and_dot) {
	sk_vec2_t a = sk_vec2(3.0f, 4.0f);
	sk_vec2_t b = sk_vec2(1.0f, 2.0f);

	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 5.0f, sk_vec2_length(a));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 11.0f, sk_vec2_dot(a, b));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, sk_vec2_cross(a, b)); /* 3*2 - 4*1 */

	sk_vec2_t n = sk_vec2_normalize(a);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.6f, n.x);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.8f, n.y);
	TEST_ASSERT_TRUE(sk_vec2_approx_eq(sk_vec2_add(a, b), sk_vec2(4.0f, 6.0f), 1e-5f));
}

SK_TEST(vec3_cross_dot_and_handedness) {
	/* Right-handed Z-up: right × forward = up */
	sk_vec3_t x = sk_vec3_right();
	sk_vec3_t y = sk_vec3_forward();
	sk_vec3_t z = sk_vec3_cross(x, y);

	TEST_ASSERT_TRUE(sk_vec3_approx_eq(z, sk_vec3_up(), 1e-5f));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, sk_vec3_dot(x, x));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, sk_vec3_dot(x, y));

	sk_vec3_t v = sk_vec3(1.0f, 2.0f, 2.0f);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, sk_vec3_length(v));
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(sk_vec3_normalize(v), sk_vec3(1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f), 1e-5f));
}

SK_TEST(vec3_reflect) {
	sk_vec3_t v = sk_vec3(1.0f, -1.0f, 0.0f);
	sk_vec3_t n = sk_vec3(0.0f, 1.0f, 0.0f);
	sk_vec3_t r = sk_vec3_reflect(v, n);
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(r, sk_vec3(1.0f, 1.0f, 0.0f), 1e-5f));
}

SK_TEST(vec4_basics) {
	sk_vec4_t a = sk_vec4_from_vec3(sk_vec3(1.0f, 2.0f, 3.0f), 4.0f);
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(sk_vec4_xyz(a), sk_vec3(1.0f, 2.0f, 3.0f), 1e-5f));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 30.0f, sk_vec4_dot(a, a)); /* 1+4+9+16 */
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, sk_vec4_length(sk_vec4_normalize(a)));
}

SK_TEST(quat_axis_angle_and_rotate) {
	/* 90° about Z (up): +X → +Y (right-handed). */
	sk_quat_t q = sk_quat_from_axis_angle(sk_vec3_up(), SK_HALF_PI);
	sk_vec3_t r = sk_quat_rotate_vec3(q, sk_vec3_right());
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(r, sk_vec3_forward(), 1e-4f));

	sk_quat_t id = sk_quat_mul(q, sk_quat_inverse(q));
	TEST_ASSERT_TRUE(sk_quat_approx_eq(id, sk_quat_identity(), 1e-4f));
}

SK_TEST(quat_slerp_endpoints) {
	sk_quat_t a = sk_quat_identity();
	sk_quat_t b = sk_quat_from_axis_angle(sk_vec3_up(), SK_HALF_PI);

	TEST_ASSERT_TRUE(sk_quat_approx_eq(sk_quat_slerp(a, b, 0.0f), a, 1e-4f));
	TEST_ASSERT_TRUE(sk_quat_approx_eq(sk_quat_slerp(a, b, 1.0f), b, 1e-4f));

	sk_vec3_t mid = sk_quat_rotate_vec3(sk_quat_slerp(a, b, 0.5f), sk_vec3_right());
	/* 45°: (cos45, sin45, 0) ≈ (0.707, 0.707, 0) */
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(mid, sk_vec3(0.70710678f, 0.70710678f, 0.0f), 1e-3f));
}

SK_TEST(mat44_identity_mul_and_translation) {
	sk_mat44_t I = sk_mat44_identity();
	sk_mat44_t T = sk_mat44_translation(sk_vec3(1.0f, 2.0f, 3.0f));
	sk_vec3_t p = sk_mat44_transform_point(T, sk_vec3(0.0f, 0.0f, 0.0f));
	sk_vec3_t d = sk_mat44_transform_vector(T, sk_vec3(1.0f, 0.0f, 0.0f));

	TEST_ASSERT_TRUE(sk_mat44_approx_eq(sk_mat44_mul(I, T), T, 1e-5f));
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(p, sk_vec3(1.0f, 2.0f, 3.0f), 1e-5f));
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(d, sk_vec3(1.0f, 0.0f, 0.0f), 1e-5f));
}

SK_TEST(mat44_rotation_z_matches_quat) {
	f32 angle = SK_HALF_PI;
	sk_mat44_t R = sk_mat44_rotation_z(angle);
	sk_mat44_t Rq = sk_mat44_from_quat(sk_quat_from_axis_angle(sk_vec3_up(), angle));

	sk_vec3_t p_m = sk_mat44_transform_point(R, sk_vec3_right());
	sk_vec3_t p_q = sk_mat44_transform_point(Rq, sk_vec3_right());

	TEST_ASSERT_TRUE(sk_vec3_approx_eq(p_m, sk_vec3_forward(), 1e-4f));
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(p_q, sk_vec3_forward(), 1e-4f));
	TEST_ASSERT_TRUE(sk_mat44_approx_eq(R, Rq, 1e-4f));
}

SK_TEST(mat44_trs_and_inverse_affine) {
	sk_vec3_t t = sk_vec3(10.0f, -3.0f, 5.0f);
	sk_quat_t r = sk_quat_from_axis_angle(sk_vec3_up(), sk_radians(30.0f));
	sk_vec3_t s = sk_vec3(2.0f, 0.5f, 1.5f);

	sk_mat44_t m = sk_mat44_trs(t, r, s);
	sk_mat44_t inv = sk_mat44_inverse_affine(m);
	sk_mat44_t I = sk_mat44_mul(inv, m);

	TEST_ASSERT_TRUE(sk_mat44_approx_eq(I, sk_mat44_identity(), 1e-3f));

	sk_vec3_t p = sk_vec3(1.0f, 2.0f, 3.0f);
	sk_vec3_t back = sk_mat44_transform_point(inv, sk_mat44_transform_point(m, p));
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(back, p, 1e-3f));
}

SK_TEST(mat44_look_at_z_up) {
	sk_vec3_t eye = sk_vec3(0.0f, -5.0f, 2.0f);
	sk_vec3_t target = sk_vec3(0.0f, 0.0f, 2.0f);
	sk_vec3_t up = sk_vec3_up();

	sk_mat44_t V = sk_mat44_look_at(eye, target, up);

	/* Eye maps near origin in view space. */
	sk_vec3_t eye_v = sk_mat44_transform_point(V, eye);
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(eye_v, sk_vec3_zero(), 1e-4f));

	/* Target is in front of camera → view -Z (negative Z component). */
	sk_vec3_t target_v = sk_mat44_transform_point(V, target);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, target_v.x);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, target_v.y);
	TEST_ASSERT_TRUE(target_v.z < 0.0f);
}

SK_TEST(mat44_perspective_depth_range) {
	sk_mat44_t P = sk_mat44_perspective(sk_radians(90.0f), 1.0f, 1.0f, 100.0f);

	/* Point on -Z axis at near plane (view space). */
	sk_vec4_t near_clip = sk_mat44_mul_vec4(P, sk_vec4(0.0f, 0.0f, -1.0f, 1.0f));
	f32 near_ndc_z = near_clip.z / near_clip.w;
	TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, near_ndc_z);

	/* Far plane. */
	sk_vec4_t far_clip = sk_mat44_mul_vec4(P, sk_vec4(0.0f, 0.0f, -100.0f, 1.0f));
	f32 far_ndc_z = far_clip.z / far_clip.w;
	TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, far_ndc_z);
}

SK_TEST(mat44_ortho) {
	sk_mat44_t O = sk_mat44_ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f);
	sk_vec4_t near_p = sk_mat44_mul_vec4(O, sk_vec4(0.0f, 0.0f, -0.0f, 1.0f));
	sk_vec4_t far_p = sk_mat44_mul_vec4(O, sk_vec4(0.0f, 0.0f, -10.0f, 1.0f));

	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, near_p.z / near_p.w);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, far_p.z / far_p.w);
}

SK_TEST(quat_from_euler_seq) {
	/* SK_EULER_ZYX: angles.x is about Z first. 90° about Z: +X → +Y. */
	sk_quat_t q_z = sk_quat_from_euler(sk_vec3(SK_HALF_PI, 0.0f, 0.0f), SK_EULER_ZYX);
	sk_vec3_t r = sk_quat_rotate_vec3(q_z, sk_vec3_right());
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(r, sk_vec3_forward(), 1e-4f));

	/* SK_EULER_XYZ: angles.x about X. 90° about X: +Y → +Z. */
	sk_quat_t q_x = sk_quat_from_euler(sk_vec3(SK_HALF_PI, 0.0f, 0.0f), SK_EULER_XYZ);
	sk_vec3_t ry = sk_quat_rotate_vec3(q_x, sk_vec3_forward());
	TEST_ASSERT_TRUE(sk_vec3_approx_eq(ry, sk_vec3_up(), 1e-4f));

	/* ypr convenience matches YXZ sequence with (roll, pitch, yaw). */
	f32 yaw = sk_radians(30.0f);
	f32 pitch = sk_radians(15.0f);
	f32 roll = sk_radians(-10.0f);
	sk_quat_t a = sk_quat_from_euler_ypr(yaw, pitch, roll);
	sk_quat_t b = sk_quat_from_euler(sk_vec3(roll, pitch, yaw), SK_EULER_YXZ);
	TEST_ASSERT_TRUE(sk_quat_approx_eq(a, b, 1e-5f));
}

SK_TEST(scalar_helpers) {
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, SK_PI, sk_radians(180.0f));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 180.0f, sk_degrees(SK_PI));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, sk_lerpf(0.0f, 1.0f, 0.5f));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, sk_clampf(5.0f, 1.0f, 3.0f));
	TEST_ASSERT_TRUE(sk_approx_eqf(1.0f, 1.0f + 1e-7f, 1e-6f));
}
#endif /* SK_TESTS */
