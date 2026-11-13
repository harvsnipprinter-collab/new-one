// SculptForge 3D - Metal shaders: PBR mesh shading, background gradient, lines (grid / cursor)
#include <metal_stdlib>
using namespace metal;

// Must match FrameUniforms / ObjectUniforms in Renderer.swift (only float4x4 and float4 members).
struct FrameUniforms {
    float4x4 viewProj;
    float4x4 view;
    float4 cameraPos;        // xyz
    float4 lightDir[4];      // xyz = direction towards the light, or position for point lights; w = 1 for point
    float4 lightColor[4];    // rgb * intensity; a = enabled
    float4 ambientSky;       // rgb, a = ambient intensity
    float4 ambientGround;    // rgb
    float4 params;           // x exposure, y shading mode, z show mask, w mask overlay opacity
    float4 backgroundTop;
    float4 backgroundBottom;
};

struct ObjectUniforms {
    float4x4 model;
    float4x4 normalMatrix;
    float4 baseColor;        // rgb tint, a = opacity
    float4 material;         // x roughness, y metallic, z emission, w usePainted
    float4 flags;            // x wireframe overlay, y selected, z unused, w unused
};

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float3 color    [[attribute(2)]];
    float  rough    [[attribute(3)]];
    float  metal    [[attribute(4)]];
    float  mask     [[attribute(5)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float3 color;
    float rough;
    float metal;
    float mask;
};

vertex VertexOut mesh_vertex(VertexIn in [[stage_in]],
                             constant FrameUniforms& frame [[buffer(8)]],
                             constant ObjectUniforms& obj [[buffer(9)]]) {
    VertexOut out;
    float4 world = obj.model * float4(in.position, 1.0);
    out.worldPos = world.xyz;
    out.position = frame.viewProj * world;
    out.normal = (obj.normalMatrix * float4(in.normal, 0.0)).xyz;
    out.color = in.color;
    out.rough = in.rough;
    out.metal = in.metal;
    out.mask = in.mask;
    return out;
}

static float distributionGGX(float NdotH, float a) {
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-6);
}
static float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}
static float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
static float3 acesTonemap(float3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

fragment float4 mesh_fragment(VertexOut in [[stage_in]],
                              bool frontFacing [[front_facing]],
                              constant FrameUniforms& frame [[buffer(8)]],
                              constant ObjectUniforms& obj [[buffer(9)]]) {
    if (obj.flags.x > 0.5) { // wireframe overlay pass
        return float4(0.0, 0.0, 0.0, 0.35);
    }
    int mode = int(frame.params.y + 0.5);
    float3 N = normalize(in.normal);
    if (mode == 2) { // flat shading: geometric normal from screen-space derivatives
        float3 g = normalize(cross(dfdx(in.worldPos), dfdy(in.worldPos)));
        N = dot(g, N) < 0.0 ? -g : g;
    }
    if (!frontFacing) N = -N; // two-sided lighting
    float3 V = normalize(frame.cameraPos.xyz - in.worldPos);
    if (frame.cameraPos.w > 0.5) { // orthographic: constant view direction stored in view matrix row 2
        V = normalize(float3(frame.view[0][2], frame.view[1][2], frame.view[2][2]));
    }
    float3 albedo = clamp(in.color * obj.baseColor.rgb, 0.0, 1.0);
    float painted = obj.material.w;
    float roughness = clamp(mix(obj.material.x, in.rough, painted), 0.04, 1.0);
    float metallic = clamp(mix(obj.material.y, in.metal, painted), 0.0, 1.0);
    float opacity = obj.baseColor.a;
    float3 color;

    if (mode == 3) { // face orientation diagnostics
        color = frontFacing ? float3(0.15, 0.3, 0.9) : float3(0.9, 0.15, 0.1);
        color *= 0.4 + 0.6 * max(dot(N, V), 0.0);
    } else if (mode == 1) { // matcap-style studio shading computed from the view-space normal
        float3 vn = normalize((frame.view * float4(N, 0.0)).xyz);
        float lit = pow(clamp(vn.z, 0.0, 1.0), 1.4);
        float rim = pow(1.0 - clamp(vn.z, 0.0, 1.0), 3.0);
        float3 h = normalize(float3(-0.35, 0.45, 0.82));
        float spec = pow(max(dot(vn, h), 0.0), mix(160.0, 12.0, roughness)) * (1.0 - roughness * 0.8);
        color = albedo * (0.18 + 0.85 * lit) + float3(0.25, 0.27, 0.32) * rim * 0.5 + float3(spec);
    } else {
        float3 F0 = mix(float3(0.04), albedo, metallic);
        float NdotV = max(dot(N, V), 1e-4);
        float a = roughness * roughness;
        float3 lighting = float3(0.0);
        for (int i = 0; i < 4; ++i) {
            if (frame.lightColor[i].a < 0.5) continue;
            float3 L;
            float atten = 1.0;
            if (frame.lightDir[i].w > 0.5) {
                float3 d = frame.lightDir[i].xyz - in.worldPos;
                float dist = max(length(d), 1e-4);
                L = d / dist;
                atten = 1.0 / (1.0 + dist * dist * 0.08);
            } else {
                L = normalize(frame.lightDir[i].xyz);
            }
            float NdotL = max(dot(N, L), 0.0);
            if (NdotL <= 0.0) continue;
            float3 H = normalize(V + L);
            float NdotH = max(dot(N, H), 0.0);
            float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
            float D = distributionGGX(NdotH, a);
            float G = geometrySmith(NdotV, NdotL, roughness);
            float3 specular = F * (D * G / max(4.0 * NdotV * NdotL, 1e-4));
            float3 kd = (1.0 - F) * (1.0 - metallic);
            lighting += (kd * albedo / 3.14159265 + specular) * frame.lightColor[i].rgb * (NdotL * atten);
        }
        // hemisphere ambient with a rough-reflection approximation of the same sky/ground gradient
        float3 sky = frame.ambientSky.rgb, ground = frame.ambientGround.rgb;
        float amb = frame.ambientSky.a;
        float3 diffuseEnv = mix(ground, sky, N.y * 0.5 + 0.5);
        float3 R = reflect(-V, N);
        float3 specEnv = mix(ground, sky, smoothstep(-0.3 - roughness, 0.3 + roughness, R.y));
        float3 Fenv = F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
        float3 ambient = ((1.0 - Fenv) * (1.0 - metallic) * albedo * diffuseEnv + Fenv * specEnv) * amb;
        color = lighting + ambient + albedo * obj.material.z;
        color = acesTonemap(color * frame.params.x);
    }
    if (frame.params.z > 0.5) { // mask overlay
        color = mix(color, color * 0.22, clamp(in.mask * frame.params.w, 0.0, 1.0));
    }
    if (obj.flags.y > 0.5) { // subtle highlight on the selected object while transforming
        color += float3(0.05, 0.035, 0.0);
    }
    return float4(color, opacity);
}

// ---- background gradient (full-screen triangle) --------------------------------------------
struct BackgroundOut {
    float4 position [[position]];
    float t;
};

vertex BackgroundOut background_vertex(uint vid [[vertex_id]]) {
    BackgroundOut out;
    float2 p = float2((vid << 1) & 2, vid & 2);
    out.position = float4(p * 2.0 - 1.0, 1.0, 1.0);
    out.t = p.y;
    return out;
}

fragment float4 background_fragment(BackgroundOut in [[stage_in]], constant FrameUniforms& frame [[buffer(8)]]) {
    return float4(mix(frame.backgroundBottom.rgb, frame.backgroundTop.rgb, clamp(in.t, 0.0, 1.0)), 1.0);
}

// ---- coloured lines (grid, axes, brush cursor, symmetry plane) -----------------------------
struct LineIn {
    float3 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
};
struct LineOut {
    float4 position [[position]];
    float4 color;
};

vertex LineOut line_vertex(LineIn in [[stage_in]], constant FrameUniforms& frame [[buffer(8)]]) {
    LineOut out;
    out.position = frame.viewProj * float4(in.position, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 line_fragment(LineOut in [[stage_in]]) {
    return in.color;
}
