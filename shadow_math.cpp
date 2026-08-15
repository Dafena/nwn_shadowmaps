#include "shadow_math.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void v_normalize(float v[3]) {
    float l = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (l > 1e-8f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

void quat_from_to(const float a[3], const float b[3], float out[4]) {
    float d = a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    if (d < -0.999999f) {
        float axis[3] = { -a[1], a[0], 0.0f };
        if (std::sqrt(axis[0]*axis[0] + axis[1]*axis[1]) < 1e-6f)
            { axis[0] = 0.0f; axis[1] = -a[2]; axis[2] = a[1]; }
        v_normalize(axis);
        out[0] = axis[0]; out[1] = axis[1]; out[2] = axis[2]; out[3] = 0.0f;
        return;
    }
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
    out[3] = 1.0f + d;
    float l = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
    if (l > 1e-8f) { out[0] /= l; out[1] /= l; out[2] /= l; out[3] /= l; }
}

void mat4_mul(const float* a, const float* b, float* out) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c*4+r] = a[0*4+r]*b[c*4+0] + a[1*4+r]*b[c*4+1] +
                         a[2*4+r]*b[c*4+2] + a[3*4+r]*b[c*4+3];
}

void mat4_rigid_inverse(const float* m, float* out) {
    out[0]=m[0]; out[4]=m[1]; out[8] =m[2];  out[12]=0.0f;
    out[1]=m[4]; out[5]=m[5]; out[9] =m[6];  out[13]=0.0f;
    out[2]=m[8]; out[6]=m[9]; out[10]=m[10]; out[14]=0.0f;
    out[3]=0.0f; out[7]=0.0f; out[11]=0.0f;  out[15]=1.0f;
    const float t0=m[12], t1=m[13], t2=m[14];
    out[12] = -(m[0]*t0 + m[1]*t1 + m[2] *t2);
    out[13] = -(m[4]*t0 + m[5]*t1 + m[6] *t2);
    out[14] = -(m[8]*t0 + m[9]*t1 + m[10]*t2);
}

bool mat4_inverse(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
           + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
           - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
           + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
            - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
           - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
           + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
           - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
            + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
           + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
           - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
            + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
            - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
           - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
           + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
            - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
            + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    const float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (!std::isfinite(det) || std::fabs(det) < 1.0e-8f) return false;
    const float rdet = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * rdet;
    return true;
}

float mat4_max_delta(const float* a, const float* b) {
    float delta = 0.0f;
    for (int i = 0; i < 16; ++i)
        delta = std::max(delta, std::fabs(a[i] - b[i]));
    return delta;
}

void build_ortho(float e, float zn, float zf, float m[16]) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0]  = 1.0f / e;
    m[5]  = 1.0f / e;
    m[10] = -2.0f / (zf - zn);
    m[14] = -(zf + zn) / (zf - zn);
    m[15] = 1.0f;
}

void build_perspective(float fovYRadians, float aspect, float zn, float zf, float m[16]) {
    std::memset(m, 0, sizeof(float) * 16);
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}
