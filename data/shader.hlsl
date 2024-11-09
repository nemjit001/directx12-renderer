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
    float4 position : SV_POSITION;
    float3 color    : COLOR0;
    float2 texCoord : TEXCOORD0;
    float3x3 TBN    : COLOR1;
};

cbuffer SceneData : register(b0, space0)
{
    float4x4 viewproject;
    float4x4 model;
    float4x4 normal;
};

PSInput VSForward(VSInput input)
{
    float4 position = mul(model, float4(input.position, 1.));
    float3 N = normalize(mul(normal, float4(input.normal, 0.)).xyz);
    float3 T = normalize(mul(normal, float4(input.tangent, 0.)).xyz);
    float3 B = cross(T, N);
    
    PSInput result;
    result.position = mul(viewproject, position);
    result.color = input.color;
    result.texCoord = input.texCoord;
    result.TBN = float3x3(T, B, N);
    
    return result;
}

float4 PSForward(PSInput input) : SV_TARGET0
{
    float3 normal = float3(0., 0., 1.);
    float3 N = mul(input.TBN, normal);
    
    // return float4(0.5 + 0.5 * N, 1.);
    return float4(input.color, 1.);
}
