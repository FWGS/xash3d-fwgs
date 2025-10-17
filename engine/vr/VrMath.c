#define _USE_MATH_DEFINES

#include <string.h>
#include "VrMath.h"
#include "mathlib.h"

float ToDegrees(float rad) {
	return (float)(rad / M_PI * 180.0f);
}

float ToRadians(float deg) {
	return (float)(deg * M_PI / 180.0f);
}

/*
================================================================================

XrTime

================================================================================
*/

double FromXrTime(const XrTime time) {
    return (time * 1e-9);
}

XrTime ToXrTime(const double timeInSeconds) {
    return (XrTime)(timeInSeconds * 1e9);
}

/*
================================================================================

XrPosef

================================================================================
*/

XrPosef XrPosef_Identity( void ) {
       XrPosef r;
       r.orientation.x = 0;
       r.orientation.y = 0;
       r.orientation.z = 0;
       r.orientation.w = 1;
       r.position.x = 0;
       r.position.y = 0;
       r.position.z = 0;
       return r;
}

XrPosef XrPosef_Inverse(const XrPosef a) {
	XrPosef b;
	b.orientation = XrQuaternionf_Inverse(a.orientation);
	b.position = XrQuaternionf_Rotate(b.orientation, XrVector3f_ScalarMultiply(a.position, -1.0f));
	return b;
}

/*
================================================================================

XrQuaternionf

================================================================================
*/

XrQuaternionf XrQuaternionf_CreateFromVectorAngle(const XrVector3f axis, const float angle) {
	XrQuaternionf r;
	if (XrVector3f_LengthSquared(axis) == 0.0f) {
		r.x = 0;
		r.y = 0;
		r.z = 0;
		r.w = 1;
		return r;
	}

	XrVector3f unitAxis = XrVector3f_Normalized(axis);
	float sinHalfAngle = sinf(angle * 0.5f);

	r.w = cosf(angle * 0.5f);
	r.x = unitAxis.x * sinHalfAngle;
	r.y = unitAxis.y * sinHalfAngle;
	r.z = unitAxis.z * sinHalfAngle;
	return r;
}

XrQuaternionf XrQuaternionf_Inverse(const XrQuaternionf q) {
	XrQuaternionf r;
	r.x = -q.x;
	r.y = -q.y;
	r.z = -q.z;
	r.w = q.w;
	return r;
}

XrQuaternionf XrQuaternionf_Multiply(const XrQuaternionf a, const XrQuaternionf b) {
	XrQuaternionf c;
	c.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	c.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	c.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	c.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return c;
}

XrVector3f XrQuaternionf_Rotate(const XrQuaternionf a, const XrVector3f v) {
	XrVector3f r;
	XrQuaternionf q = {v.x, v.y, v.z, 0.0f};
	XrQuaternionf aq = XrQuaternionf_Multiply(a, q);
	XrQuaternionf aInv = XrQuaternionf_Inverse(a);
	XrQuaternionf aqaInv = XrQuaternionf_Multiply(aq, aInv);
	r.x = aqaInv.x;
	r.y = aqaInv.y;
	r.z = aqaInv.z;
	return r;
}

XrVector3f XrQuaternionf_ToEulerAngles(const XrQuaternionf q) {
	float M[16];
	XrQuaternionf_ToMatrix4f( &q, M);

	XrVector4f v1 = {0, 0, -1, 0};
	XrVector4f v2 = {1, 0, 0, 0};
	XrVector4f v3 = {0, 1, 0, 0};

	XrVector4f forwardInVRSpace = XrVector4f_MultiplyMatrix4f(M, &v1);
	XrVector4f rightInVRSpace = XrVector4f_MultiplyMatrix4f(M, &v2);
	XrVector4f upInVRSpace = XrVector4f_MultiplyMatrix4f(M, &v3);

	XrVector3f forward = {-forwardInVRSpace.z, -forwardInVRSpace.x, forwardInVRSpace.y};
	XrVector3f right = {-rightInVRSpace.z, -rightInVRSpace.x, rightInVRSpace.y};
	XrVector3f up = {-upInVRSpace.z, -upInVRSpace.x, upInVRSpace.y};

	XrVector3f forwardNormal = XrVector3f_Normalized(forward);
	XrVector3f rightNormal = XrVector3f_Normalized(right);
	XrVector3f upNormal = XrVector3f_Normalized(up);

	return XrVector3f_GetAnglesFromVectors(forwardNormal, rightNormal, upNormal);
}

void XrQuaternionf_ToMatrix4f(const XrQuaternionf* q, float* matrix) {
	const float ww = q->w * q->w;
	const float xx = q->x * q->x;
	const float yy = q->y * q->y;
	const float zz = q->z * q->z;

	float M[4][4];
	M[0][0] = ww + xx - yy - zz;
	M[0][1] = 2 * (q->x * q->y - q->w * q->z);
	M[0][2] = 2 * (q->x * q->z + q->w * q->y);
	M[0][3] = 0;

	M[1][0] = 2 * (q->x * q->y + q->w * q->z);
	M[1][1] = ww - xx + yy - zz;
	M[1][2] = 2 * (q->y * q->z - q->w * q->x);
	M[1][3] = 0;

	M[2][0] = 2 * (q->x * q->z - q->w * q->y);
	M[2][1] = 2 * (q->y * q->z + q->w * q->x);
	M[2][2] = ww - xx - yy + zz;
	M[2][3] = 0;

	M[3][0] = 0;
	M[3][1] = 0;
	M[3][2] = 0;
	M[3][3] = 1;

	memcpy(matrix, &M, sizeof(float) * 16);
}

/*
================================================================================

XrVector3f, XrVector4f

================================================================================
*/

float XrVector3f_LengthSquared(const XrVector3f v) {
	return v.x * v.x + v.y * v.y + v.z * v.z;;
}

XrVector3f XrVector3f_GetAnglesFromVectors(const XrVector3f forward, const XrVector3f right, const XrVector3f up) {
	float sr, sp, sy, cr, cp, cy;

	sp = -forward.z;

	float cp_x_cy = forward.x;
	float cp_x_sy = forward.y;
	float cp_x_sr = -right.z;
	float cp_x_cr = up.z;

	float yaw = atan2(cp_x_sy, cp_x_cy);
	float roll = atan2(cp_x_sr, cp_x_cr);

	cy = cos(yaw);
	sy = sin(yaw);
	cr = cos(roll);
	sr = sin(roll);

	if (fabs(cy) > EPSILON) {
		cp = cp_x_cy / cy;
	} else if (fabs(sy) > EPSILON) {
		cp = cp_x_sy / sy;
	} else if (fabs(sr) > EPSILON) {
		cp = cp_x_sr / sr;
	} else if (fabs(cr) > EPSILON) {
		cp = cp_x_cr / cr;
	} else {
		cp = cos(asin(sp));
	}

	float pitch = atan2(sp, cp);

	XrVector3f angles;
	angles.x = ToDegrees(pitch);
	angles.y = ToDegrees(yaw);
	angles.z = ToDegrees(roll);
	return angles;
}

XrVector3f XrVector3f_Normalized(const XrVector3f v) {
	float rcpLen = 1.0f / sqrtf(XrVector3f_LengthSquared(v));
	return XrVector3f_ScalarMultiply(v, rcpLen);
}

XrVector3f XrVector3f_ScalarMultiply(const XrVector3f v, float scale) {
	XrVector3f u;
	u.x = v.x * scale;
	u.y = v.y * scale;
	u.z = v.z * scale;
	return u;
}

XrVector4f XrVector4f_MultiplyMatrix4f(const float* m, const XrVector4f* v) {
	float M[4][4];
	memcpy(&M, m, sizeof(float) * 16);

	XrVector4f out;
	out.x = M[0][0] * v->x + M[0][1] * v->y + M[0][2] * v->z + M[0][3] * v->w;
	out.y = M[1][0] * v->x + M[1][1] * v->y + M[1][2] * v->z + M[1][3] * v->w;
	out.z = M[2][0] * v->x + M[2][1] * v->y + M[2][2] * v->z + M[2][3] * v->w;
	out.w = M[3][0] * v->x + M[3][1] * v->y + M[3][2] * v->z + M[3][3] * v->w;
	return out;
}

/*
================================================================================

XrMatrix4ff

================================================================================
*/

ovrMatrix4f ovrMatrix4f_CreateFromQuaternion(const XrQuaternionf* q) {
	const float ww = q->w * q->w;
	const float xx = q->x * q->x;
	const float yy = q->y * q->y;
	const float zz = q->z * q->z;

	ovrMatrix4f out;
	out.M[0][0] = ww + xx - yy - zz;
	out.M[0][1] = 2 * (q->x * q->y - q->w * q->z);
	out.M[0][2] = 2 * (q->x * q->z + q->w * q->y);
	out.M[0][3] = 0;

	out.M[1][0] = 2 * (q->x * q->y + q->w * q->z);
	out.M[1][1] = ww - xx + yy - zz;
	out.M[1][2] = 2 * (q->y * q->z - q->w * q->x);
	out.M[1][3] = 0;

	out.M[2][0] = 2 * (q->x * q->z - q->w * q->y);
	out.M[2][1] = 2 * (q->y * q->z + q->w * q->x);
	out.M[2][2] = ww - xx - yy + zz;
	out.M[2][3] = 0;

	out.M[3][0] = 0;
	out.M[3][1] = 0;
	out.M[3][2] = 0;
	out.M[3][3] = 1;
	return out;
}

ovrMatrix4f ovrMatrix4f_CreateRotation(const float radiansX, const float radiansY, const float radiansZ) {
	const float sinX = sinf(radiansX);
	const float cosX = cosf(radiansX);
	const ovrMatrix4f rotationX = {
			{{1, 0, 0, 0}, {0, cosX, -sinX, 0}, {0, sinX, cosX, 0}, {0, 0, 0, 1}}};
	const float sinY = sinf(radiansY);
	const float cosY = cosf(radiansY);
	const ovrMatrix4f rotationY = {
			{{cosY, 0, sinY, 0}, {0, 1, 0, 0}, {-sinY, 0, cosY, 0}, {0, 0, 0, 1}}};
	const float sinZ = sinf(radiansZ);
	const float cosZ = cosf(radiansZ);
	const ovrMatrix4f rotationZ = {
			{{cosZ, -sinZ, 0, 0}, {sinZ, cosZ, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
	const ovrMatrix4f rotationXY = ovrMatrix4f_Multiply(&rotationY, &rotationX);
	return ovrMatrix4f_Multiply(&rotationZ, &rotationXY);
}

/// Use left-multiplication to accumulate transformations.
ovrMatrix4f ovrMatrix4f_Multiply(const ovrMatrix4f* a, const ovrMatrix4f* b) {
	ovrMatrix4f out;
	out.M[0][0] = a->M[0][0] * b->M[0][0] + a->M[0][1] * b->M[1][0] + a->M[0][2] * b->M[2][0] +
	              a->M[0][3] * b->M[3][0];
	out.M[1][0] = a->M[1][0] * b->M[0][0] + a->M[1][1] * b->M[1][0] + a->M[1][2] * b->M[2][0] +
	              a->M[1][3] * b->M[3][0];
	out.M[2][0] = a->M[2][0] * b->M[0][0] + a->M[2][1] * b->M[1][0] + a->M[2][2] * b->M[2][0] +
	              a->M[2][3] * b->M[3][0];
	out.M[3][0] = a->M[3][0] * b->M[0][0] + a->M[3][1] * b->M[1][0] + a->M[3][2] * b->M[2][0] +
	              a->M[3][3] * b->M[3][0];

	out.M[0][1] = a->M[0][0] * b->M[0][1] + a->M[0][1] * b->M[1][1] + a->M[0][2] * b->M[2][1] +
	              a->M[0][3] * b->M[3][1];
	out.M[1][1] = a->M[1][0] * b->M[0][1] + a->M[1][1] * b->M[1][1] + a->M[1][2] * b->M[2][1] +
	              a->M[1][3] * b->M[3][1];
	out.M[2][1] = a->M[2][0] * b->M[0][1] + a->M[2][1] * b->M[1][1] + a->M[2][2] * b->M[2][1] +
	              a->M[2][3] * b->M[3][1];
	out.M[3][1] = a->M[3][0] * b->M[0][1] + a->M[3][1] * b->M[1][1] + a->M[3][2] * b->M[2][1] +
	              a->M[3][3] * b->M[3][1];

	out.M[0][2] = a->M[0][0] * b->M[0][2] + a->M[0][1] * b->M[1][2] + a->M[0][2] * b->M[2][2] +
	              a->M[0][3] * b->M[3][2];
	out.M[1][2] = a->M[1][0] * b->M[0][2] + a->M[1][1] * b->M[1][2] + a->M[1][2] * b->M[2][2] +
	              a->M[1][3] * b->M[3][2];
	out.M[2][2] = a->M[2][0] * b->M[0][2] + a->M[2][1] * b->M[1][2] + a->M[2][2] * b->M[2][2] +
	              a->M[2][3] * b->M[3][2];
	out.M[3][2] = a->M[3][0] * b->M[0][2] + a->M[3][1] * b->M[1][2] + a->M[3][2] * b->M[2][2] +
	              a->M[3][3] * b->M[3][2];

	out.M[0][3] = a->M[0][0] * b->M[0][3] + a->M[0][1] * b->M[1][3] + a->M[0][2] * b->M[2][3] +
	              a->M[0][3] * b->M[3][3];
	out.M[1][3] = a->M[1][0] * b->M[0][3] + a->M[1][1] * b->M[1][3] + a->M[1][2] * b->M[2][3] +
	              a->M[1][3] * b->M[3][3];
	out.M[2][3] = a->M[2][0] * b->M[0][3] + a->M[2][1] * b->M[1][3] + a->M[2][2] * b->M[2][3] +
	              a->M[2][3] * b->M[3][3];
	out.M[3][3] = a->M[3][0] * b->M[0][3] + a->M[3][1] * b->M[1][3] + a->M[3][2] * b->M[2][3] +
	              a->M[3][3] * b->M[3][3];
	return out;
}
