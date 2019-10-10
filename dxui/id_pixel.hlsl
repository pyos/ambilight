Texture2D tx : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 id_pixel(PS_INPUT input) : SV_Target {
    return tx.Sample(samLinear, input.Tex);
}
