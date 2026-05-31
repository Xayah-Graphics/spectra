#include <spectra/pathtracer/util/math.h>
#include <spectra/pathtracer/util/transform.h>
#include <spectra/pathtracer/util/vecmath.h>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace spectra
{
    // Quaternion Method Definitions

    // DirectionCone Function Definitions
    SPECTRA_CPU_GPU DirectionCone Union(const DirectionCone& a, const DirectionCone& b)
    {
        // Handle the cases where one or both cones are empty
        if (a.IsEmpty())
            return b;
        if (b.IsEmpty())
            return a;

        // Handle the cases where one cone is inside the other
        Float theta_a = SafeACos(a.cosTheta), theta_b = SafeACos(b.cosTheta);
        Float theta_d = AngleBetween(a.w, b.w);
        if (std::min(theta_d + theta_b, Pi) <= theta_a)
            return a;
        if (std::min(theta_d + theta_a, Pi) <= theta_b)
            return b;

        // Compute the spread angle of the merged cone, $\theta_o$
        Float theta_o = (theta_a + theta_d + theta_b) / 2;
        if (theta_o >= Pi)
            return DirectionCone::EntireSphere();

        // Find the merged cone's axis and return cone union
        Float theta_r = theta_o - theta_a;
        Vector3f wr = Cross(a.w, b.w);
        if (LengthSquared(wr) == 0)
            return DirectionCone::EntireSphere();
        Vector3f w = Rotate(Degrees(theta_r), wr)(a.w);
        return DirectionCone(w, std::cos(theta_o));
    }

} // namespace spectra
