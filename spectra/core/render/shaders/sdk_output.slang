module sdk_output;

public struct SdkTransform {
    public float matrix[16];
}

public struct SdkSphere {
    public float position[3];
    public float radius;
}

public struct SdkInstance {
    public uint64_t instance_id;
    public SdkTransform transform;
}

public struct SdkLine {
    public float first_position[3];
    public float width;
    public float second_position[3];
    public float color[4];
    public float scalar;
}

public struct SdkVector {
    public float origin[3];
    public float width;
    public float vector[3];
    public float color[4];
    public float scalar;
}

public float3 sdk_float3(const float value[3]) {
    return float3(value[0], value[1], value[2]);
}

public float4 sdk_float4(const float value[4]) {
    return float4(value[0], value[1], value[2], value[3]);
}
