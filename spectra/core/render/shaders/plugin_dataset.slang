module plugin_dataset;

public struct SpectraPluginTransform {
    public float matrix[16];
}

public struct SpectraPluginSphere {
    public float position[3];
    public float radius;
}

public struct SpectraPluginInstanceTransform {
    public uint64_t instance_id;
    public SpectraPluginTransform transform;
}

public struct SpectraPluginPoint {
    public float position[3];
    public float radius;
    public float color[4];
    public float scalar;
}

public struct SpectraPluginSegment {
    public float first_position[3];
    public float width;
    public float second_position[3];
    public float color[4];
    public float scalar;
}

public struct SpectraPluginCurve {
    public float control_0[3];
    public float width;
    public float control_1[3];
    public float control_2[3];
    public float control_3[3];
    public float color[4];
    public float scalar;
}

public struct SpectraPluginVector {
    public float origin[3];
    public float width;
    public float vector[3];
    public float color[4];
    public float scalar;
}

public struct SpectraPluginCameraDistortion {
    public float radial_1;
    public float radial_2;
    public float tangential_1;
    public float tangential_2;
}

public struct SpectraPluginCameraObservation {
    public SpectraPluginTransform world_from_camera;
    public float intrinsics[4];
    public SpectraPluginCameraDistortion distortion;
    public uint image_layer;
}

public float3 plugin_float3(const float value[3]) {
    return float3(value[0], value[1], value[2]);
}

public float4 plugin_float4(const float value[4]) {
    return float4(value[0], value[1], value[2], value[3]);
}
