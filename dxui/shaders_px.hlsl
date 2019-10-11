Texture2D tx : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float4 Clr : COLOR;
    float2 Tex : TEXCOORD;
    float  Blw : BLENDWEIGHT;
};

float4 id_pixel(PS_INPUT input) : SV_Target {
    return input.Clr * (1 - input.Blw) + tx.Sample(samLinear, input.Tex) * input.Blw;
}

float contour(float d, float w) {
    return clamp((d - .5) / w + .5, 0., 1.);
}

float4 distance_color(PS_INPUT input) : SV_Target {
    float d = tx.Sample(samLinear, input.Tex).r;
    float w = fwidth(d);
    float2 duv = 0.333 * (ddx(input.Tex) + ddy(input.Tex));
    float4 box = {input.Tex - duv, input.Tex + duv};
    float b = contour(tx.Sample(samLinear, box.xy).r, w)
            + contour(tx.Sample(samLinear, box.zw).r, w)
            + contour(tx.Sample(samLinear, box.xw).r, w)
            + contour(tx.Sample(samLinear, box.zy).r, w);
    return float4(input.Clr.rgb, input.Clr.a * (contour(d, w) / 3 + b / 6));
}
