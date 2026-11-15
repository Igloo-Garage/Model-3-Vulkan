/**
 ** REXABYTE Modular Engine SDK (Audio DSP / Camera / Input)
 ** Original standalone component developed from scratch by REXABYTE SOFTWARE INC.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. All Rights Reserved.
 **
 ** Dual-Licensed Component (Commercial License / GPLv3 with Section 7 Additional Terms):
 **
 **   1. Commercial / Proprietary License (Paid License Required for Commercial Use):
 **      This file is the exclusive intellectual property of REXABYTE SOFTWARE INC.
 **      Any use, copying, modification, integration, or distribution of this file (or
 **      derivative works thereof) in any closed-source product, proprietary software,
 **      paid application, commercial arcade cabinet/hardware, or any distribution that
 **      does not disclose full corresponding source code under GPLv3 Strictly Requires
 **      a prior written, paid Commercial License from REXABYTE SOFTWARE INC.
 **      (authorized for official REXABYTE SOFTWARE INC. commercial products including
 **      the Android edition).
 **
 **   2. Open-Source GPLv3 Integration Grant (Non-Proprietary / Full Copyleft Only):
 **      When distributed strictly as part of a complete open-source GPLv3 project
 **      (such as the PC build of Model 3-Vulkan), REXABYTE SOFTWARE INC. grants permission
 **      to use and distribute this file under the GNU General Public License v3.0 SUBJECT
 **      TO the mandatory Section 7 Additional Terms (see LICENSE-ADDITIONAL-TERMS.md):
 **      all REXABYTE SOFTWARE INC. copyright notices, license headers, and in-app author
 **      attributions MUST be preserved intact. Any removal of attribution or closed-source
 **      commercial exploitation immediately terminates this open-source grant.
 **
 ** This file contains zero code, types, or headers from upstream Supermodel.
 **/

#ifndef IGLOO_CHASE_CAMERA_H
#define IGLOO_CHASE_CAMERA_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Igloo {
namespace Camera {

struct StabilizerParams
{
    float tau;
    float deadbandDeg;
    float deadbandScale;
    float maxLagDeg;
};

inline constexpr StabilizerParams kStabilizerPresets[4] = {
    { 0.0f,  0.0f, 1.0f,  0.0f  },  // 0: Off
    { 0.25f, 1.5f, 0.25f, 7.5f  },  // 1: Light
    { 0.50f, 3.0f, 0.10f, 13.0f },  // 2: Standard (NFS-style chase)
    { 0.85f, 4.5f, 0.04f, 18.0f },  // 3: Strong
};

// Checks whether a 4x4 column-major matrix has unit determinant (|det - 1| < 0.05),
// qualifying it as a rigid world-to-view camera matrix at the viewport root node.
inline bool IsValidRigidCameraMatrix(const float bm[16]) noexcept
{
    const float det = bm[0] * (bm[5] * bm[10] - bm[9] * bm[6])
        - bm[4] * (bm[1] * bm[10] - bm[9] * bm[2])
        + bm[8] * (bm[1] * bm[6] - bm[5] * bm[2]);
    return std::fabs(det - 1.0f) < 0.05f;
}

// A true 3D world camera has a non-trivial world translation (|T| >= 2.0); screen-space 3D HUD
// viewports (speedometer, course map, turn arrows) sit near the origin.
inline bool HasNonTrivialWorldTranslation(const float cm[16], float minDistSq = 4.0f) noexcept
{
    const float camDistSq = cm[12] * cm[12] + cm[13] * cm[13] + cm[14] * cm[14];
    return camDistSq >= minDistSq;
}

// Checks whether two viewports share the exact same 3D world camera matrix and priority.
inline bool SharesWorldCamera(
    bool vpFullScreen, bool vpHasCameraMatrix, int vpPriority, const float vpCamera[16],
    int primaryPriority, const float primaryCamera[16]) noexcept
{
    if (!vpFullScreen || !vpHasCameraMatrix || primaryCamera == nullptr)
        return false;
    if (vpPriority != primaryPriority)
        return false;
    for (int k = 0; k < 16; ++k)
    {
        if (std::fabs(vpCamera[k] - primaryCamera[k]) > 1.0e-3f)
            return false;
    }
    return true;
}

// Identifies a screen-aligned far backdrop quad (identity view-space rotation at Z < -50)
// that must remain locked to the camera frustum rather than counter-rotated.
inline bool IsScreenAlignedBackdrop(const float modelMat[16]) noexcept
{
    return modelMat[0] > 0.999f && modelMat[5] > 0.999f && modelMat[10] > 0.999f &&
        std::fabs(modelMat[12]) < 0.01f && std::fabs(modelMat[13]) < 0.01f &&
        modelMat[14] < -50.0f;
}

// Checks whether a model sits inside the player car view-space bounding box and is stationary
// in view space between two frames, returning its distance (-vz) along the view axis.
inline bool IsPlayerCarPivotCandidate(
    const float currModelMat[16],
    const float prevModelMat[16],
    float& outDistanceZ) noexcept
{
    const float vx = currModelMat[12];
    const float vy = currModelMat[13];
    const float vz = currModelMat[14];
    if (std::fabs(vx) > 1.2f || std::fabs(vy) > 1.8f || vz > -3.5f || vz < -16.0f)
        return false;

    const float dvx = vx - prevModelMat[12];
    const float dvy = vy - prevModelMat[13];
    const float dvz = vz - prevModelMat[14];
    if (dvx * dvx + dvy * dvy + dvz * dvz >= 0.04f)
        return false;

    outDistanceZ = -vz;
    return true;
}

// Frustum culling margin helpers when ChaseCameraStabilizer or widescreen is active.
inline float CullRadiusScaleFor(int stabilizerPreset, bool wideScreen) noexcept
{
    return (stabilizerPreset > 0 || wideScreen) ? 1.35f : 1.0f;
}

inline float CullAngleSinMarginFor(int stabilizerPreset, bool wideScreen) noexcept
{
    return (stabilizerPreset > 0) ? 0.45f : (wideScreen ? 0.20f : 0.0f);
}

inline float ComputeExpandedCullRadius(
    float rawRadius, float modelScale, float cullRadiusScale,
    float cullAngleSin, float viewDist) noexcept
{
    return rawRadius * modelScale * cullRadiusScale + cullAngleSin * viewDist;
}

// Builds the 4x4 column-major counter-rotation matrix around the player car pivot (0, 0, -L)
// in view space: C = T(0, 0, -L) * Ry(-yawLag) * T(0, 0, +L).
inline void BuildPivotYawCounterRotation(float yawLag, float pivotDistance, float out[16]) noexcept
{
    const float c = std::cos(yawLag);
    const float s = std::sin(yawLag);
    const float L = pivotDistance;
    std::memset(out, 0, 16 * sizeof(float));
    out[0]  =  c;
    out[8]  = -s;
    out[5]  =  1.0f;
    out[2]  =  s;
    out[10] =  c;
    out[12] = -s * L;
    out[14] = (c - 1.0f) * L;
    out[15] =  1.0f;
}

/*
 * ChaseCameraStabilizer
 *
 * Spring-damper horizontal yaw-lag integrator with non-linear deadband and view-space
 * player-car pivot tracking. Smooths arcade rigid-rod chase cameras into a modern
 * Need-for-Speed style damped chase camera without introducing 120 Hz sub-frame ghosting
 * or road tile seams.
 */
class ChaseCameraStabilizer
{
public:
    struct StepResult
    {
        float prevYawLag = 0.0f;
        float currYawLag = 0.0f;
        float deltaYaw   = 0.0f;
        bool  stabilized = false;
        bool  cameraCut  = false;
    };

    void Reset() noexcept
    {
        m_valid = false;
        m_yawLag = 0.0f;
    }

    // Returns true if the preset changed (and state was reset).
    bool SetPreset(int preset) noexcept
    {
        preset = std::clamp(preset, 0, 3);
        if (preset == m_preset)
            return false;
        m_preset = preset;
        Reset();
        return true;
    }

    int Preset() const noexcept { return m_preset; }
    float YawLag() const noexcept { return m_yawLag; }
    float PivotDistance() const noexcept { return m_pivotDistance; }

    StepResult StepSceneCamera(
        const float currCamera[16],
        float cameraCutAngleDeg,
        float stepHz = 57.52416f) noexcept
    {
        StepResult res{};
        if (currCamera == nullptr)
            return res;

        if (!m_valid)
        {
            std::memcpy(m_prevCamera, currCamera, sizeof(m_prevCamera));
            m_yawLag = 0.0f;
            m_valid = true;
            return res;
        }

        const float* c = currCamera;
        const float* p = m_prevCamera;
        const float step02 = c[0] * p[2] + c[4] * p[6] + c[8]  * p[10];
        const float step22 = c[2] * p[2] + c[6] * p[6] + c[10] * p[10];
        const float deltaYaw = std::atan2(step02, step22);
        constexpr float kRadToDeg = 57.2957795f;
        constexpr float kDegToRad = 0.0174532925f;

        if (std::fabs(deltaYaw * kRadToDeg) > cameraCutAngleDeg)
        {
            m_yawLag = 0.0f;
            res.cameraCut = true;
        }
        else if (m_preset > 0)
        {
            const StabilizerParams& sp = kStabilizerPresets[m_preset];
            const float prevLag = m_yawLag;
            float newLag = prevLag + deltaYaw;
            const float lagDeg = std::fabs(newLag) * kRadToDeg;
            const float dt = 1.0f / stepHz;
            float follow = dt / (sp.tau + dt);
            if (lagDeg < sp.deadbandDeg && sp.deadbandDeg > 0.0f)
            {
                const float r = lagDeg / sp.deadbandDeg;
                follow *= (sp.deadbandScale + (1.0f - sp.deadbandScale) * r * r);
            }
            newLag *= (1.0f - follow);
            const float maxLagRad = sp.maxLagDeg * kDegToRad;
            newLag = std::clamp(newLag, -maxLagRad, maxLagRad);

            res.prevYawLag = prevLag;
            res.currYawLag = newLag;
            res.deltaYaw   = deltaYaw;
            res.stabilized = true;
            m_yawLag = newLag;
        }

        std::memcpy(m_prevCamera, currCamera, sizeof(m_prevCamera));
        return res;
    }

    void UpdatePivotDistanceFromSamples(double sumZ, unsigned countZ) noexcept
    {
        if (countZ >= 3)
        {
            const float measured = std::clamp(static_cast<float>(sumZ / countZ), 4.0f, 16.0f);
            m_pivotDistance = 0.85f * m_pivotDistance + 0.15f * measured;
        }
    }

private:
    int   m_preset = 0;
    bool  m_valid = false;
    float m_prevCamera[16] = {};
    float m_yawLag = 0.0f;
    float m_pivotDistance = 7.25f;
};

} // namespace Camera
} // namespace Igloo

#endif // IGLOO_CHASE_CAMERA_H
