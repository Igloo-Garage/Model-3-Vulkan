/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Additional Terms under GNU GPLv3 Section 7 apply to this file
 ** (see LICENSE-ADDITIONAL-TERMS.md).
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

/*
 * MotionInterpolation.h
 *
 * Rigid-transform decomposition and interpolation for the high-refresh path.
 *
 * The obvious way to blend two frames - (1-a)*M0 + a*M1 element by element - is
 * wrong for the rotation part. At a 90 degree turn and a = 0.5 the columns of the
 * blended matrix have length cos(45 degrees) = 0.707, so the world visibly
 * shrinks; at other angles it shears. The rotation has to stay on SO(3), which
 * means separating it from translation and scale, interpolating the rotation as
 * a quaternion (shortest path) and rebuilding the matrix.
 *
 * Matrices are column-major, matching New3D::Mat4 and the push constants.
 */

#ifndef INCLUDED_MOTION_INTERPOLATION_H
#define INCLUDED_MOTION_INTERPOLATION_H

#include <cstdint>

struct TRS
{
    float t[3] = {};        // translation
    float q[4] = { 0, 0, 0, 1 };  // rotation quaternion, xyzw
    float s[3] = { 1, 1, 1 };     // per-axis scale
    bool valid = false;
};

// Splits a matrix into translation, rotation and scale. Returns false for a
// matrix that is degenerate, sheared or reflected beyond what a rigid transform
// can express; callers then snap to the current state rather than interpolating
// an artefact.
bool DecomposeRigid(const float m[16], TRS& out);

void ComposeRigid(const TRS& trs, float out[16]);

// Interpolates a -> b. Falls back to b when either endpoint is invalid, so a
// failed decomposition can only cost smoothness, never correctness.
void InterpolateRigid(const TRS& a, const TRS& b, float alpha, float out[16]);

/*
 * Element-wise linear interpolation of two column-major 4x4 matrices.
 *
 * Unlike InterpolateRigid this is a plain linear combination, which is exactly what instance
 * interpolation needs. Two models that share an edge stay coincident for every alpha because
 * (1-a)*A*E + a*A'*E equals (1-a)*B*E + a*B'*E whenever A*E = B*E and A'*E = B'*E - a linear
 * blend of coincident edges is coincident. The nonlinear slerp in InterpolateRigid does not have
 * that property, which is why a shared edge opened a one-frame seam.
 *
 * The cost is a slight shrink at large rotation angles. Instances that rotate more than the cut
 * angle are snapped rather than interpolated, so the blend never sees a rotation large enough for
 * that shrink to be visible.
 */
void LerpMatrix(const float a[16], const float b[16], float alpha, float out[16]);

// Metrics used by the cut detector. Neither modifies the inputs.
float RotationDeltaDeg(const TRS& a, const TRS& b);
float TranslationDelta(const TRS& a, const TRS& b);
float ScaleDelta(const TRS& a, const TRS& b);

// out = a * b, column-major 4x4.
void Mul4x4(const float a[16], const float b[16], float out[16]);

#endif // INCLUDED_MOTION_INTERPOLATION_H
