Texture2D tx : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float4 Clr : COLOR;
    float2 Tex : TEXCOORD;
};

float4 id_pixel(PS_INPUT input) : SV_Target {
    return tx.Sample(samLinear, input.Tex);
}

float4 id_color(PS_INPUT input) : SV_Target {
    return input.Clr;
}

float4 id_distance_pixel(PS_INPUT input) : SV_Target {
    float x = tx.Sample(samLinear, input.Tex).y;
    float s = 1 / fwidth(x);
    float d = (x - 0.5) * s;
    float4 color = {input.Clr.x, input.Clr.y, input.Clr.z, input.Clr.a * clamp(d + 0.5, 0.0, 1.0)};
    return color;
}
