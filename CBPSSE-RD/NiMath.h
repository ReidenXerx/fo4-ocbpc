#pragma once
// fo4-anatomy: the classic F4SE SDK's NiPoint3 / NiMatrix43 / NiTransform (f4se/NiTypes.h, NiTypes.cpp), carried into
// the Runtime Database build unchanged, so the physics maths behaves exactly as in the classic build. They are laid
// over CommonLib's node memory (Game.h): the same bytes, asserted below.
#include <cmath>

class NiPoint3
{
public:
	float x;   // 0
	float y;   // 4
	float z;   // 8

	NiPoint3() : x(0.0f), y(0.0f), z(0.0f) {}
	NiPoint3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

	NiPoint3 operator-() const { return NiPoint3(-x, -y, -z); }
	NiPoint3 operator+(const NiPoint3& pt) const { return NiPoint3(x + pt.x, y + pt.y, z + pt.z); }
	NiPoint3 operator-(const NiPoint3& pt) const { return NiPoint3(x - pt.x, y - pt.y, z - pt.z); }
	NiPoint3& operator+=(const NiPoint3& pt)
	{
		x += pt.x;
		y += pt.y;
		z += pt.z;
		return *this;
	}
	NiPoint3& operator-=(const NiPoint3& pt)
	{
		x -= pt.x;
		y -= pt.y;
		z -= pt.z;
		return *this;
	}
	NiPoint3 operator*(float s) const { return NiPoint3(s * x, s * y, s * z); }
	NiPoint3 operator/(float s) const
	{
		float inv = 1.0f / s;
		return NiPoint3(inv * x, inv * y, inv * z);
	}
	NiPoint3& operator*=(float s)
	{
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}
	NiPoint3& operator/=(float s)
	{
		float inv = 1.0f / s;
		x *= inv;
		y *= inv;
		z *= inv;
		return *this;
	}
};

#define MATH_PI 3.14159265358979323846

class NiMatrix43
{
public:
	union
	{
		float data[3][4];
		float arr[12];
	};

	NiMatrix43() : arr{} {}

	NiMatrix43 operator*(const NiMatrix43& rhs) const
	{
		NiMatrix43 tmp;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				tmp.data[r][c] = data[r][0] * rhs.data[0][c] + data[r][1] * rhs.data[1][c] + data[r][2] * rhs.data[2][c];
		return tmp;
	}

	NiPoint3 operator*(const NiPoint3& pt) const
	{
		return NiPoint3(data[0][0] * pt.x + data[0][1] * pt.y + data[0][2] * pt.z,
			data[1][0] * pt.x + data[1][1] * pt.y + data[1][2] * pt.z,
			data[2][0] * pt.x + data[2][1] * pt.y + data[2][2] * pt.z);
	}

	NiMatrix43 Transpose() const
	{
		NiMatrix43 result;
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++)
				result.data[r][c] = data[c][r];
			result.data[r][3] = data[r][3];
		}
		return result;
	}

	// http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToEuler/index.htm
	void GetEulerAngles(float* heading, float* attitude, float* bank)
	{
		if (data[1][0] > 0.998) {
			*heading = std::atan2(data[0][2], data[2][2]);
			*attitude = static_cast<float>(MATH_PI / 2);
			*bank = 0;
		} else if (data[1][0] < -0.998) {
			*heading = std::atan2(data[0][2], data[2][2]);
			*attitude = static_cast<float>(-MATH_PI / 2);
			*bank = 0;
		} else {
			*heading = std::atan2(-data[2][0], data[0][0]);
			*bank = std::atan2(-data[1][2], data[1][1]);
			*attitude = std::asin(data[1][0]);
		}
	}

	// http://www.euclideanspace.com/maths/geometry/rotations/conversions/eulerToMatrix/index.htm
	void SetEulerAngles(float heading, float attitude, float bank)
	{
		double ch = std::cos(heading), sh = std::sin(heading);
		double ca = std::cos(attitude), sa = std::sin(attitude);
		double cb = std::cos(bank), sb = std::sin(bank);
		data[0][0] = static_cast<float>(ch * ca);
		data[0][1] = static_cast<float>(sh * sb - ch * sa * cb);
		data[0][2] = static_cast<float>(ch * sa * sb + sh * cb);
		data[1][0] = static_cast<float>(sa);
		data[1][1] = static_cast<float>(ca * cb);
		data[1][2] = static_cast<float>(-ca * sb);
		data[2][0] = static_cast<float>(-sh * ca);
		data[2][1] = static_cast<float>(sh * sa * cb + ch * sb);
		data[2][2] = static_cast<float>(-sh * sa * sb + ch * cb);
	}
};

class NiTransform
{
public:
	NiMatrix43 rot;   // 00
	NiPoint3 pos;     // 30
	float scale;      // 3C

	NiTransform() : scale(1.0f) {}

	NiTransform operator*(const NiTransform& rhs) const
	{
		NiTransform tmp;
		tmp.scale = scale * rhs.scale;
		tmp.rot = rot * rhs.rot;
		tmp.pos = pos + (rot * rhs.pos) * scale;
		return tmp;
	}

	NiPoint3 operator*(const NiPoint3& pt) const { return ((rot * pt) * scale) + pos; }
};

static_assert(sizeof(NiPoint3) == 0xC);
static_assert(sizeof(NiMatrix43) == 0x30);
static_assert(sizeof(NiTransform) == 0x40);
static_assert(offsetof(NiTransform, pos) == 0x30);
static_assert(offsetof(NiTransform, scale) == 0x3C);
