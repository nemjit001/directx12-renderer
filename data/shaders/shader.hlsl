struct VSInput
{
    float3 position : POSITION0;
    float3 color    : COLOR0;
    float3 normal   : NORMAL0;
    float3 tangent  : TANGENT0;
    float2 texCoord : TEXCOORD0;
};

struct PSInput
{
    float4 position     : SV_POSITION;
    float3 vertexPos    : POSITION0;
    float3 color        : COLOR0;
    float2 texCoord     : TEXCOORD0;
    float3x3 TBN        : TEXCOORD1;
};

cbuffer SceneData : register(b0)
{
    float azimuth;
    float zenith;
    float3 cameraPosition;
    float4x4 viewproject;
    float4x4 model;
    float4x4 normal;
};

Texture2D colorTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState textureSampler : register(s0);

float3 calcSunDir()
{
    float alpha = radians(azimuth);
    float beta = radians(zenith);
    float3 dir = float3(cos(alpha), 0.0, sin(alpha)); // TODO(nemjit001): use zenith with rotation around X
    
    return normalize(dir);
}

PSInput VSForward(VSInput input)
{
    // calc world pos
    float4 position = mul(model, float4(input.position, 1.));
    
    // calc tangent and normal vectors
    float3 T = normalize(mul(normal, float4(input.tangent, 0.)).xyz);
    float3 N = normalize(mul(normal, float4(input.normal, 0.)).xyz);
    
    // Re-orthogonalize T and N & calc B
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    
    PSInput result;
    result.position = mul(viewproject, position);
    result.vertexPos = position.xyz / position.w;
    result.color = input.color;
    result.texCoord = input.texCoord;
    result.TBN = float3x3(T, B, N);
    
    return result;
}

float4 PSForward(PSInput input) : SV_TARGET0
{    
    float3 color = pow(colorTexture.Sample(textureSampler, input.texCoord).rgb, 2.2); // Convert from SRGB to linear colors    
    float3 normal = pow(normalTexture.Sample(textureSampler, input.texCoord).rgb, 2.2); // Convert from SRGB to linear colors;
    normal.y *= -1; // Flip y for OpenGL -> DirectX conversion
    normal = 2.0 * normal - 1.0;
 
    float3 L = calcSunDir();
    float3 V = normalize(cameraPosition - input.vertexPos);
    float3 H = normalize(L + V);
    float3 N = normalize(mul(input.TBN, float3(0, 1, 0)));
    
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    
    float3 diffuse = NoL * color;
    float3 specular = pow(NoH, 16.0F);
    float3 outColor = diffuse + specular;
    
    return float4(0.5 + 0.5 * N, 1.0);
}
