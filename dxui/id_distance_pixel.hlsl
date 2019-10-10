Texture2D tx : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 id_distance_pixel(PS_INPUT input) : SV_Target {
    float x = tx.Sample(samLinear, input.Tex).y;
    float s = 1 / fwidth(x);
    float d = (x - 0.5) * s;
    float4 color = {1, 1, 1, clamp(d + 0.5, 0.0, 1.0)};
    return color;
}
