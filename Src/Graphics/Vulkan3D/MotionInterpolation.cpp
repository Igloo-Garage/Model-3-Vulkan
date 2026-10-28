/**
 ** Supermodel-VK
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright 2026 The Supermodel-VK authors
 **
 ** This file is part of Supermodel-VK, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "MotionInterpolation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kColumnEpsilon = 1.0e-6f;
constexpr float kOrthogonalityEpsilon = 1.0e-3f;

struct Vec3
{
    double x, y, z;
};

Vec3 Column(const float m[16], int index)
{
    return { m[index * 4 + 0], m[index * 4 + 1], m[index * 4 + 2] };
}

double Length(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 Scale(const Vec3& v, double s)
{
    return { v.x * s, v.y * s, v.z * s };
}

double Dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

void NormalizeQuaternion(float q[4])
{
    const double len = std::sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2] + (double)q[3] * q[3]);
    if (len < 1.0e-12)
    {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    const double inv = 1.0 / len;
    q[0] = (float)(q[0] * inv);
    q[1] = (float)(q[1] * inv);
    q[2] = (float)(q[2] * inv);
    q[3] = (float)(q[3] * inv);
}

void QuaternionToMatrix(const float q[4], float out[16])
{
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double xx = x * x, yy = y * y, zz = z * z;
    const double xy = x * y, xz = x * z, yz = y * z;
    const double wx = w * x, wy = w * y, wz = w * z;

    out[0] = (float)(1.0 - 2.0 * (yy + zz));
    out[1] = (float)(2.0 * (xy + wz));
    out[2] = (float)(2.0 * (xz - wy));
    out[3] = 0.0f;

    out[4] = (float)(2.0 * (xy - wz));
    out[5] = (float)(1.0 - 2.0 * (xx + zz));
    out[6] = (float)(2.0 * (yz + wx));
    out[7] = 0.0f;

    out[8] = (float)(2.0 * (xz + wy));
    out[9] = (float)(2.0 * (yz - wx));
    out[10] = (float)(1.0 - 2.0 * (xx + yy));
    out[11] = 0.0f;

    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;
}
}

bool DecomposeRigid(const float m[16], TRS& out)
{
    out = TRS();
    out.valid = false;

    /*
     * Non-finite input has to be rejected here, because it does not fail the checks below:
     * Length() of an infinite column is infinite, which passes the epsilon test, and the
     * normalisation then divides infinity by infinity. The quaternion comes back as NaN, is stored
     * with valid = true, and the rotation-delta test that is supposed to snap the instance compares
     * NaN against the threshold and answers false. A matrix the game never wrote ends up in a draw
     * call with nothing to show for it.
     */
    for (int i = 0; i < 16; i++)
    {
        if (!std::isfinite(m[i]))
            return false;
    }

    Vec3 c0 = Column(m, 0);
    Vec3 c1 = Column(m, 1);
    Vec3 c2 = Column(m, 2);

    const double sx = Length(c0);
    const double sy = Length(c1);
    const double sz = Length(c2);
    if (sx < kColumnEpsilon || sy < kColumnEpsilon || sz < kColumnEpsilon)
        return false;

    c0 = Scale(c0, 1.0 / sx);
    c1 = Scale(c1, 1.0 / sy);
    c2 = Scale(c2, 1.0 / sz);

    // A sheared matrix cannot be represented as T*R*S. Model 3 base and node
    // matrices are rigid; a failure here is the cue to snap rather than to
    // interpolate something that would visibly wobble.
    if (std::fabs(Dot(c0, c1)) > kOrthogonalityEpsilon ||
        std::fabs(Dot(c0, c2)) > kOrthogonalityEpsilon ||
        std::fabs(Dot(c1, c2)) > kOrthogonalityEpsilon)
        return false;

    double scaleZ = sz;
    if (Dot(c0, Cross(c1, c2)) < 0.0)
    {
        // A reflection. Fold it into the Z scale so the rotation stays proper.
        c2 = Scale(c2, -1.0);
        scaleZ = -scaleZ;
    }

    // Rotation matrix columns c0,c1,c2, column-major in r.
    float r[16] = {};
    r[0] = (float)c0.x; r[1] = (float)c0.y; r[2] = (float)c0.z;
    r[4] = (float)c1.x; r[5] = (float)c1.y; r[6] = (float)c1.z;
    r[8] = (float)c2.x; r[9] = (float)c2.y; r[10] = (float)c2.z;
    r[15] = 1.0f;

    const double trace = (double)r[0] + r[5] + r[10];
    float q[4] = { 0, 0, 0, 1 };
    if (trace > 0.0)
    {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q[3] = (float)(0.25 * s);
        q[0] = (float)((r[6] - r[9]) / s);
        q[1] = (float)((r[8] - r[2]) / s);
        q[2] = (float)((r[1] - r[4]) / s);
    }
    else if (r[0] > r[5] && r[0] > r[10])
    {
        const double s = std::sqrt(1.0 + r[0] - r[5] - r[10]) * 2.0;
        q[3] = (float)((r[6] - r[9]) / s);
        q[0] = (float)(0.25 * s);
        q[1] = (float)((r[4] + r[1]) / s);
        q[2] = (float)((r[8] + r[2]) / s);
    }
    else if (r[5] > r[10])
    {
        const double s = std::sqrt(1.0 + r[5] - r[0] - r[10]) * 2.0;
        q[3] = (float)((r[8] - r[2]) / s);
        q[0] = (float)((r[4] + r[1]) / s);
        q[1] = (float)(0.25 * s);
        q[2] = (float)((r[9] + r[6]) / s);
    }
    else
    {
        const double s = std::sqrt(1.0 + r[10] - r[0] - r[5]) * 2.0;
        q[3] = (float)((r[1] - r[4]) / s);
        q[0] = (float)((r[8] + r[2]) / s);
        q[1] = (float)((r[9] + r[6]) / s);
        q[2] = (float)(0.25 * s);
    }
    NormalizeQuaternion(q);

    out.t[0] = m[12];
    out.t[1] = m[13];
    out.t[2] = m[14];
    for (int i = 0; i < 4; i++)
        out.q[i] = q[i];
    out.s[0] = (float)sx;
    out.s[1] = (float)sy;
    out.s[2] = (float)scaleZ;
    out.valid = true;
    return true;
}

void ComposeRigid(const TRS& trs, float out[16])
{
    QuaternionToMatrix(trs.q, out);

    for (int col = 0; col < 3; col++)
    {
        out[col * 4 + 0] *= trs.s[col];
        out[col * 4 + 1] *= trs.s[col];
        out[col * 4 + 2] *= trs.s[col];
    }

    out[12] = trs.t[0];
    out[13] = trs.t[1];
    out[14] = trs.t[2];
    out[3] = out[7] = out[11] = 0.0f;
    out[15] = 1.0f;
}

void InterpolateRigid(const TRS& a, const TRS& b, float alpha, float out[16])
{
    if (!a.valid || !b.valid)
    {
        if (b.valid)
            ComposeRigid(b, out);
        else
            std::memset(out, 0, sizeof(float) * 16), out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }

    const double t = std::clamp((double)alpha, 0.0, 1.0);

    TRS result;
    result.valid = true;
    for (int i = 0; i < 3; i++)
    {
        result.t[i] = (float)((1.0 - t) * a.t[i] + t * b.t[i]);
        result.s[i] = (float)((1.0 - t) * a.s[i] + t * b.s[i]);
    }

    double q0[4] = { a.q[0], a.q[1], a.q[2], a.q[3] };
    double q1[4] = { b.q[0], b.q[1], b.q[2], b.q[3] };
    double dot = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3];
    if (dot < 0.0)
    {
        // Shortest path: the two quaternions describe the same orientation, so
        // negating one is free and avoids a 300 degree spin.
        for (int i = 0; i < 4; i++)
            q1[i] = -q1[i];
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0, 1.0);

    double q[4];
    if (dot > 0.9995)
    {
        for (int i = 0; i < 4; i++)
            q[i] = (1.0 - t) * q0[i] + t * q1[i];
    }
    else
    {
        const double theta = std::acos(dot);
        const double sinTheta = std::sin(theta);
        const double w0 = std::sin((1.0 - t) * theta) / sinTheta;
        const double w1 = std::sin(t * theta) / sinTheta;
        for (int i = 0; i < 4; i++)
            q[i] = w0 * q0[i] + w1 * q1[i];
    }
    for (int i = 0; i < 4; i++)
        result.q[i] = (float)q[i];
    NormalizeQuaternion(result.q);

    ComposeRigid(result, out);
}

void LerpMatrix(const float a[16], const float b[16], float alpha, float out[16])
{
    const float t = std::clamp(alpha, 0.0f, 1.0f);
    const float it = 1.0f - t;
    for (int i = 0; i < 16; i++)
        out[i] = it * a[i] + t * b[i];
}

float RotationDeltaDeg(const TRS& a, const TRS& b)
{
    if (!a.valid || !b.valid)
        return 180.0f;
    double dot = (double)a.q[0] * b.q[0] + (double)a.q[1] * b.q[1] +
        (double)a.q[2] * b.q[2] + (double)a.q[3] * b.q[3];
    dot = std::clamp(std::fabs(dot), 0.0, 1.0);
    return (float)(2.0 * std::acos(dot) * 180.0 / kPi);
}

float TranslationDelta(const TRS& a, const TRS& b)
{
    const double dx = (double)b.t[0] - a.t[0];
    const double dy = (double)b.t[1] - a.t[1];
    const double dz = (double)b.t[2] - a.t[2];
    return (float)std::sqrt(dx * dx + dy * dy + dz * dz);
}

float ScaleDelta(const TRS& a, const TRS& b)
{
    float result = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        const float denom = std::max(1.0f, std::fabs(a.s[i]));
        result = std::max(result, std::fabs(b.s[i] - a.s[i]) / denom);
    }
    return result;
}

void Mul4x4(const float a[16], const float b[16], float out[16])
{
    float r[16];
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            double sum = 0.0;
            for (int k = 0; k < 4; k++)
                sum += (double)a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = (float)sum;
        }
    }
    std::memcpy(out, r, sizeof(r));
}
