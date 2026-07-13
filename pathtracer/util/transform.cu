#include <algorithm>
#include <cmath>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/core/interaction.cuh>
#include <pathtracer/util/check.cuh>
#include <pathtracer/util/math.cuh>
#include <pathtracer/util/transform.cuh>

namespace spectra {
    // Transform Function Definitions
    // clang-format off
__host__ __device__ Transform Translate(Vector3f delta) {
    SquareMatrix<4> m(1, 0, 0, delta.x,
                      0, 1, 0, delta.y,
                      0, 0, 1, delta.z,
                      0, 0, 0, 1);
    SquareMatrix<4> minv(1, 0, 0, -delta.x,
                         0, 1, 0, -delta.y,
                         0, 0, 1, -delta.z,
                         0, 0, 0, 1);
    return Transform(m, minv);
}
    // clang-format on

    // clang-format off
__host__ __device__ Transform Scale(Float x, Float y, Float z) {
    SquareMatrix<4> m(x, 0, 0, 0,
                      0, y, 0, 0,
                      0, 0, z, 0,
                      0, 0, 0, 1);
    SquareMatrix<4> minv(1 / x,     0,     0, 0,
                             0, 1 / y,     0, 0,
                             0,     0, 1 / z, 0,
                             0,     0,     0, 1);
    return Transform(m, minv);
}
    // clang-format on

    __host__ __device__ Transform LookAt(Point3f pos, Point3f look, Vector3f up) {
        SquareMatrix<4> worldFromCamera;
        // Initialize fourth column of viewing matrix
        worldFromCamera[0][3] = pos.x;
        worldFromCamera[1][3] = pos.y;
        worldFromCamera[2][3] = pos.z;
        worldFromCamera[3][3] = 1;

        // Initialize first three columns of viewing matrix
        Vector3f dir = Normalize(look - pos);
        if (Length(Cross(Normalize(up), dir)) == 0)
            SPECTRA_FATAL("LookAt: \"up\" vector (%f, %f, %f) and viewing direction "
                          "(%f, %f, %f) "
                          "passed to LookAt are pointing in the same direction.",
                up.x, up.y, up.z, dir.x, dir.y, dir.z);
        Vector3f right        = Normalize(Cross(Normalize(up), dir));
        Vector3f newUp        = Cross(dir, right);
        worldFromCamera[0][0] = right.x;
        worldFromCamera[1][0] = right.y;
        worldFromCamera[2][0] = right.z;
        worldFromCamera[3][0] = 0.;
        worldFromCamera[0][1] = newUp.x;
        worldFromCamera[1][1] = newUp.y;
        worldFromCamera[2][1] = newUp.z;
        worldFromCamera[3][1] = 0.;
        worldFromCamera[0][2] = dir.x;
        worldFromCamera[1][2] = dir.y;
        worldFromCamera[2][2] = dir.z;
        worldFromCamera[3][2] = 0.;

        SquareMatrix<4> cameraFromWorld = InvertOrExit(worldFromCamera);
        return Transform(cameraFromWorld, worldFromCamera);
    }

    __host__ __device__ Transform Orthographic(Float zNear, Float zFar) {
        return Scale(1, 1, 1 / (zFar - zNear)) * Translate(Vector3f(0, 0, -zNear));
    }

    __host__ __device__ Transform Perspective(Float fov, Float n, Float f) {
        // Perform projective divide for perspective projection
        // clang-format off
SquareMatrix<4> persp(1, 0,           0,              0,
                      0, 1,           0,              0,
                      0, 0, f / (f - n), -f*n / (f - n),
                      0, 0,           1,              0);
        // clang-format on

        // Scale canonical perspective view to specified field of view
        Float invTanAng = 1 / std::tan(Radians(fov) / 2);
        return Scale(invTanAng, invTanAng, 1) * Transform(persp);
    }

    // Transform Method Definitions
    __host__ __device__ Bounds3f Transform::operator()(const Bounds3f& b) const {
        Bounds3f bt;
        for (int i = 0; i < 8; ++i) bt = Union(bt, (*this)(b.Corner(i)));
        return bt;
    }

    __host__ __device__ Transform Transform::operator*(const Transform& t2) const {
        return Transform(m * t2.m, t2.mInv * mInv);
    }

    __host__ __device__ bool Transform::SwapsHandedness() const {
        // clang-format off
    SquareMatrix<3> s(m[0][0], m[0][1], m[0][2],
                      m[1][0], m[1][1], m[1][2],
                      m[2][0], m[2][1], m[2][2]);
        // clang-format on
        return Determinant(s) < 0;
    }

    __host__ __device__ Transform::operator Quaternion() const {
        Float trace = m[0][0] + m[1][1] + m[2][2];
        Quaternion quat;
        if (trace > 0.f) {
            // Compute w from matrix trace, then xyz
            // 4w^2 = m[0][0] + m[1][1] + m[2][2] + m[3][3] (but m[3][3] == 1)
            Float s  = std::sqrt(trace + 1.0f);
            quat.w   = s / 2.0f;
            s        = 0.5f / s;
            quat.v.x = (m[2][1] - m[1][2]) * s;
            quat.v.y = (m[0][2] - m[2][0]) * s;
            quat.v.z = (m[1][0] - m[0][1]) * s;
        } else {
            // Compute largest of $x$, $y$, or $z$, then remaining components
            const int nxt[3] = {1, 2, 0};
            Float q[3];
            int i = 0;
            if (m[1][1] > m[0][0]) i = 1;
            if (m[2][2] > m[i][i]) i = 2;
            int j   = nxt[i];
            int k   = nxt[j];
            Float s = SafeSqrt((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
            q[i]    = s * 0.5f;
            if (s != 0.f) s = 0.5f / s;
            quat.w   = (m[k][j] - m[j][k]) * s;
            q[j]     = (m[j][i] + m[i][j]) * s;
            q[k]     = (m[k][i] + m[i][k]) * s;
            quat.v.x = q[0];
            quat.v.y = q[1];
            quat.v.z = q[2];
        }
        return quat;
    }

    void Transform::Decompose(Vector3f* T, SquareMatrix<4>* R, SquareMatrix<4>* S) const {
        // Extract translation _T_ from transformation matrix
        T->x = m[0][3];
        T->y = m[1][3];
        T->z = m[2][3];

        // Compute new transformation matrix _M_ without translation
        SquareMatrix<4> M = m;
        for (int i = 0; i < 3; ++i) M[i][3] = M[3][i] = 0.f;
        M[3][3] = 1.f;

        // Extract rotation _R_ from transformation matrix
        Float norm;
        int count = 0;
        *R        = M;
        do {
            // Compute next matrix _Rnext_ in series
            SquareMatrix<4> Rit   = InvertOrExit(Transpose(*R));
            SquareMatrix<4> Rnext = (*R + Rit) / 2;

            // Compute norm of difference between _R_ and _Rnext_
            norm = 0;
            for (int i = 0; i < 3; ++i) {
                Float n = std::abs((*R)[i][0] - Rnext[i][0]) + std::abs((*R)[i][1] - Rnext[i][1]) + std::abs((*R)[i][2] - Rnext[i][2]);
                norm    = std::max(norm, n);
            }

            *R = Rnext;
        } while (++count < 100 && norm > .0001);
        // XXX TODO FIXME deal with flip...

        // Compute scale _S_ using rotation and original matrix
        *S = InvertOrExit(*R) * M;
    }

    __host__ __device__ SurfaceInteraction Transform::operator()(const SurfaceInteraction& si) const {
        SurfaceInteraction ret;
        const Transform& t = *this;
        ret.pi             = t(si.pi);
        // Transform remaining members of _SurfaceInteraction_
        ret.n               = Normalize(t(si.n));
        ret.wo              = Normalize(t(si.wo));
        ret.time            = si.time;
        ret.mediumInterface = si.mediumInterface;
        ret.uv              = si.uv;
        ret.dpdu            = t(si.dpdu);
        ret.dpdv            = t(si.dpdv);
        ret.dndu            = t(si.dndu);
        ret.dndv            = t(si.dndv);
        ret.shading.n       = Normalize(t(si.shading.n));
        ret.shading.dpdu    = t(si.shading.dpdu);
        ret.shading.dpdv    = t(si.shading.dpdv);
        ret.shading.dndu    = t(si.shading.dndu);
        ret.shading.dndv    = t(si.shading.dndv);
        ret.dudx            = si.dudx;
        ret.dvdx            = si.dvdx;
        ret.dudy            = si.dudy;
        ret.dvdy            = si.dvdy;
        ret.dpdx            = t(si.dpdx);
        ret.dpdy            = t(si.dpdy);
        ret.material        = si.material;
        ret.areaLight       = si.areaLight;
        //    ret.n = FaceForward(ret.n, ret.shading.n);
        ret.shading.n = FaceForward(ret.shading.n, ret.n);
        ret.faceIndex = si.faceIndex;

        return ret;
    }

    __host__ __device__ Point3fi Transform::ApplyInverse(const Point3fi& p) const {
        Float x = Float(p.x), y = Float(p.y), z = Float(p.z);
        // Compute transformed coordinates from point _pt_
        Float xp = (mInv[0][0] * x + mInv[0][1] * y) + (mInv[0][2] * z + mInv[0][3]);
        Float yp = (mInv[1][0] * x + mInv[1][1] * y) + (mInv[1][2] * z + mInv[1][3]);
        Float zp = (mInv[2][0] * x + mInv[2][1] * y) + (mInv[2][2] * z + mInv[2][3]);
        Float wp = (mInv[3][0] * x + mInv[3][1] * y) + (mInv[3][2] * z + mInv[3][3]);

        // Compute absolute error for transformed point
        Vector3f pOutError;
        if (p.IsExact()) {
            pOutError.x = gamma(3) * (std::abs(mInv[0][0] * x) + std::abs(mInv[0][1] * y) + std::abs(mInv[0][2] * z));
            pOutError.y = gamma(3) * (std::abs(mInv[1][0] * x) + std::abs(mInv[1][1] * y) + std::abs(mInv[1][2] * z));
            pOutError.z = gamma(3) * (std::abs(mInv[2][0] * x) + std::abs(mInv[2][1] * y) + std::abs(mInv[2][2] * z));
        } else {
            Vector3f pInError = p.Error();
            pOutError.x       = (gamma(3) + 1) * (std::abs(mInv[0][0]) * pInError.x + std::abs(mInv[0][1]) * pInError.y + std::abs(mInv[0][2]) * pInError.z) + gamma(3) * (std::abs(mInv[0][0] * x) + std::abs(mInv[0][1] * y) + std::abs(mInv[0][2] * z) + std::abs(mInv[0][3]));
            pOutError.y       = (gamma(3) + 1) * (std::abs(mInv[1][0]) * pInError.x + std::abs(mInv[1][1]) * pInError.y + std::abs(mInv[1][2]) * pInError.z) + gamma(3) * (std::abs(mInv[1][0] * x) + std::abs(mInv[1][1] * y) + std::abs(mInv[1][2] * z) + std::abs(mInv[1][3]));
            pOutError.z       = (gamma(3) + 1) * (std::abs(mInv[2][0]) * pInError.x + std::abs(mInv[2][1]) * pInError.y + std::abs(mInv[2][2]) * pInError.z) + gamma(3) * (std::abs(mInv[2][0] * x) + std::abs(mInv[2][1] * y) + std::abs(mInv[2][2] * z) + std::abs(mInv[2][3]));
        }

        if (wp == 1)
            return Point3fi(Point3f(xp, yp, zp), pOutError);
        else
            return Point3fi(Point3f(xp, yp, zp), pOutError) / wp;
    }

    __host__ __device__ Interaction Transform::operator()(const Interaction& in) const {
        Interaction ret;
        ret.pi = (*this)(in.pi);
        ret.n  = (*this)(in.n);
        if (LengthSquared(ret.n) > 0) ret.n = Normalize(ret.n);
        ret.uv = in.uv;
        ret.wo = (*this)(in.wo);
        if (LengthSquared(ret.wo) > 0) ret.wo = Normalize(ret.wo);
        ret.time            = in.time;
        ret.mediumInterface = in.mediumInterface;
        return ret;
    }

    __host__ __device__ Interaction Transform::ApplyInverse(const Interaction& in) const {
        Interaction ret;
        Transform t = Inverse(*this);
        ret.pi      = t(in.pi);
        ret.n       = t(in.n);
        if (LengthSquared(ret.n) > 0) ret.n = Normalize(ret.n);
        ret.uv = in.uv;
        ret.wo = t(in.wo);
        if (LengthSquared(ret.wo) > 0) ret.wo = Normalize(ret.wo);
        ret.time            = in.time;
        ret.mediumInterface = in.mediumInterface;
        return ret;
    }

    __host__ __device__ SurfaceInteraction Transform::ApplyInverse(const SurfaceInteraction& si) const {
        SurfaceInteraction ret;
        ret.pi = (*this)(si.pi);

        // Transform remaining members of _SurfaceInteraction_
        Transform t         = Inverse(*this);
        ret.n               = Normalize(t(si.n));
        ret.wo              = Normalize(t(si.wo));
        ret.time            = si.time;
        ret.mediumInterface = si.mediumInterface;
        ret.uv              = si.uv;
        ret.dpdu            = t(si.dpdu);
        ret.dpdv            = t(si.dpdv);
        ret.dndu            = t(si.dndu);
        ret.dndv            = t(si.dndv);
        ret.shading.n       = Normalize(t(si.shading.n));
        ret.shading.dpdu    = t(si.shading.dpdu);
        ret.shading.dpdv    = t(si.shading.dpdv);
        ret.shading.dndu    = t(si.shading.dndu);
        ret.shading.dndv    = t(si.shading.dndv);
        ret.dudx            = si.dudx;
        ret.dvdx            = si.dvdx;
        ret.dudy            = si.dudy;
        ret.dvdy            = si.dvdy;
        ret.dpdx            = t(si.dpdx);
        ret.dpdy            = t(si.dpdy);
        ret.material        = si.material;
        ret.areaLight       = si.areaLight;
        //    ret.n = FaceForward(ret.n, ret.shading.n);
        ret.shading.n = FaceForward(ret.shading.n, ret.n);
        ret.faceIndex = si.faceIndex;
        return ret;
    }


    // AnimatedTransform Method Definitions
    AnimatedTransform::AnimatedTransform(const Transform& startTransform, Float startTime, const Transform& endTransform, Float endTime) : startTransform(startTransform), endTransform(endTransform), startTime(startTime), endTime(endTime), actuallyAnimated(startTransform != endTransform) {
        if (!actuallyAnimated) return;
        // Decompose start and end transformations
        SquareMatrix<4> Rm;
        startTransform.Decompose(&T[0], &Rm, &S[0]);
        R[0] = Quaternion(Transform(Rm));
        endTransform.Decompose(&T[1], &Rm, &S[1]);
        R[1] = Quaternion(Transform(Rm));
        // Flip _R[1]_ if needed to select shortest path
        if (Dot(R[0], R[1]) < 0) R[1] = -R[1];
    }

    __host__ __device__ Ray AnimatedTransform::operator()(const Ray& r, Float* tMax) const {
        if (!actuallyAnimated || r.time <= startTime)
            return startTransform(r, tMax);
        else if (r.time >= endTime)
            return endTransform(r, tMax);
        else {
            Transform t = Interpolate(r.time);
            return t(r, tMax);
        }
    }

    __host__ __device__ Ray AnimatedTransform::ApplyInverse(const Ray& r, Float* tMax) const {
        if (!actuallyAnimated || r.time <= startTime)
            return startTransform.ApplyInverse(r, tMax);
        else if (r.time >= endTime)
            return endTransform.ApplyInverse(r, tMax);
        else {
            Transform t = Interpolate(r.time);
            return t.ApplyInverse(r, tMax);
        }
    }

    __host__ __device__ RayDifferential AnimatedTransform::operator()(const RayDifferential& r, Float* tMax) const {
        if (!actuallyAnimated || r.time <= startTime)
            return startTransform(r, tMax);
        else if (r.time >= endTime)
            return endTransform(r, tMax);
        else {
            Transform t = Interpolate(r.time);
            return t(r, tMax);
        }
    }

    __host__ __device__ Point3f AnimatedTransform::operator()(Point3f p, Float time) const {
        if (!actuallyAnimated || time <= startTime)
            return startTransform(p);
        else if (time >= endTime)
            return endTransform(p);
        Transform t = Interpolate(time);
        return t(p);
    }

    __host__ __device__ Vector3f AnimatedTransform::operator()(Vector3f v, Float time) const {
        if (!actuallyAnimated || time <= startTime)
            return startTransform(v);
        else if (time >= endTime)
            return endTransform(v);
        Transform t = Interpolate(time);
        return t(v);
    }

    __host__ __device__ Normal3f AnimatedTransform::operator()(Normal3f n, Float time) const {
        if (!actuallyAnimated || time <= startTime)
            return startTransform(n);
        else if (time >= endTime)
            return endTransform(n);
        Transform t = Interpolate(time);
        return t(n);
    }

    __host__ __device__ Interaction AnimatedTransform::operator()(const Interaction& it) const {
        if (!actuallyAnimated) return startTransform(it);
        Transform t = Interpolate(it.time);
        return t(it);
    }

    __host__ __device__ Interaction AnimatedTransform::ApplyInverse(const Interaction& it) const {
        if (!actuallyAnimated) return startTransform.ApplyInverse(it);
        Transform t = Interpolate(it.time);
        return t.ApplyInverse(it);
    }

    __host__ __device__ SurfaceInteraction AnimatedTransform::operator()(const SurfaceInteraction& it) const {
        if (!actuallyAnimated) return startTransform(it);
        Transform t = Interpolate(it.time);
        return t(it);
    }

    __host__ __device__ SurfaceInteraction AnimatedTransform::ApplyInverse(const SurfaceInteraction& it) const {
        if (!actuallyAnimated) return startTransform.ApplyInverse(it);
        Transform t = Interpolate(it.time);
        return t.ApplyInverse(it);
    }


    __host__ __device__ Transform AnimatedTransform::Interpolate(Float time) const {
        // Handle boundary conditions for matrix interpolation
        if (!actuallyAnimated || time <= startTime) return startTransform;
        if (time >= endTime) return endTransform;

        Float dt = (time - startTime) / (endTime - startTime);
        // Interpolate translation at _dt_
        Vector3f trans = (1 - dt) * T[0] + dt * T[1];

        // Interpolate rotation at _dt_
        Quaternion rotate = Slerp(dt, R[0], R[1]);

        // Interpolate scale at _dt_
        SquareMatrix<4> scale = (1 - dt) * S[0] + dt * S[1];

        // Return interpolated matrix as product of interpolated components
        return Translate(trans) * Transform(rotate) * Transform(scale);
    }
} // namespace spectra
