module sphere_mapping;

public float3 equal_area_square_to_sphere(float2 point) {
    const float2 uv = 2.0 * point - 1.0;
    const float2 absolute = abs(uv);
    const float signed_distance = 1.0 - (absolute.x + absolute.y);
    const float radius = 1.0 - abs(signed_distance);
    const float phi = (radius == 0.0 ? 1.0 : (absolute.y - absolute.x) / radius + 1.0) * 3.14159265358979323846 / 4.0;
    const float z = sign(signed_distance) * (1.0 - radius * radius);
    return float3(
        sign(uv.x) * cos(phi) * radius * sqrt(max(0.0, 2.0 - radius * radius)),
        sign(uv.y) * sin(phi) * radius * sqrt(max(0.0, 2.0 - radius * radius)),
        z);
}

public float2 equal_area_sphere_to_square(float3 direction) {
    direction = normalize(direction);
    const float3 absolute = abs(direction);
    const float radius = sqrt(max(0.0, 1.0 - absolute.z));
    const float a = max(absolute.x, absolute.y);
    const float b = a == 0.0 ? 0.0 : min(absolute.x, absolute.y) / a;
    float phi = 0.4067585662467884896e-5 + b * (0.6362265452740161349 + b * (0.6157201789828021349e-2 + b * (-0.2473337332812689442 + b * (0.8817706647753162947e-1 + b * (0.4190388180291657359e-1 + b * -0.2513909723434835093e-1)))));
    if (absolute.x < absolute.y) phi = 1.0 - phi;
    float v = phi * radius;
    float u = radius - v;
    if (direction.z < 0.0) {
        const float swap = u;
        u = 1.0 - v;
        v = 1.0 - swap;
    }
    u = sign(direction.x) * u;
    v = sign(direction.y) * v;
    return 0.5 * (float2(u, v) + 1.0);
}
