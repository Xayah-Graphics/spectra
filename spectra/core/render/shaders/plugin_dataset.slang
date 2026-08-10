module plugin_dataset;

public struct SpectraPluginTransform {
    public float matrix[16];
}

public struct SpectraPluginPoint {
    public float3 position;
    public float radius;
    public float4 color;
    public float scalar;
}

public struct SpectraPluginSegment {
    public float3 first_position;
    public float width;
    public float3 second_position;
    public float4 color;
}

public struct SpectraPluginCurve {
    public float3 control_0;
    public float width;
    public float3 control_1;
    public float3 control_2;
    public float3 control_3;
    public float4 color;
}

public struct SpectraPluginVector {
    public float3 origin;
    public float width;
    public float3 vector;
    public float4 color;
}

public struct SpectraPluginCameraDistortion {
    public float radial_1;
    public float radial_2;
    public float tangential_1;
    public float tangential_2;
}

public struct SpectraPluginCameraObservation {
    public SpectraPluginTransform world_from_camera;
    public float4 intrinsics;
    public SpectraPluginCameraDistortion distortion;
    public uint image_layer;
}
