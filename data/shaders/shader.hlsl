
#define INV_GAMMA   2.2
#define GAMMA       1.0 / INV_GAMMA

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
    float3 sunDirection;
    float3 cameraPosition;
    float4x4 viewproject;
    float4x4 model;
    float4x4 normal;
};

Texture2D colorTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState textureSampler : register(s0);

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
    result.TBN = transpose(float3x3(T, B, N));
    
    return result;
}

float4 PSForward(PSInput input) : SV_TARGET0
{    
    float3 color = pow(colorTexture.Sample(textureSampler, input.texCoord).rgb, INV_GAMMA); // Convert from SRGB to linear colors    
    float3 normal = normalTexture.Sample(textureSampler, input.texCoord).rgb; // Assume normals stored in linear format
    normal = (2.0 * normal) - 1.0;
 
    float3 L = normalize(sunDirection);
    float3 V = normalize(cameraPosition - input.vertexPos);
    float3 H = normalize(L + V);
    float3 N = normalize(mul(input.TBN, normal));
    
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    
    float3 diffuse = NoL * color;
    float3 specular = pow(NoH, 32.0F);
    float3 outColor = diffuse + specular;
    
    return float4(outColor, 1.0);

}
