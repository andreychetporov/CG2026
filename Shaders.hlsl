cbuffer PerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float3 gLightPosW;
    float _pad0;
    float3 gEyePosW;
    float _pad1;
    float4 gDiffuseColor;
    float4 gSpecColorPower; // rgb spec, a power
};

struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float4 Color : COLOR;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 Color : COLOR;
};

PSInput VSMain(VSInput vin)
{
    PSInput vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    float3 nW = mul(vin.NormalL, (float3x3) gWorld);
    vout.NormalW = normalize(nW);

    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);

    vout.Color = vin.Color; // ? пробросили цвет
    return vout;
}

float4 PSMain(PSInput pin) : SV_Target
{
    float3 N = normalize(pin.NormalW);
    float3 L = normalize(gLightPosW - pin.PosW);
    float3 V = normalize(gEyePosW - pin.PosW);
    float3 R = reflect(-L, N);

    float NdotL = saturate(dot(N, L));
    float3 diffuse = (pin.Color.rgb) * NdotL; // ? цвет грани
    float3 ambient = 0.10f * pin.Color.rgb;

    float specPow = gSpecColorPower.a;
    float specTerm = pow(saturate(dot(R, V)), specPow);
    float3 specular = gSpecColorPower.rgb * specTerm;

    return float4(ambient + diffuse + specular, 1.0f);
}
