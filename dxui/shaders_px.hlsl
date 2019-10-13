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

float4 blur(PS_INPUT input) : SV_Target {
    // Let G(x) be the Gaussian distribution. Normally, we'd do Sample(i) * G(i)
    // for all i in [-2n, 2n]. With a linear sampler, we can instead handle i = 0
    // separately and then (Sample(O(i)) + Sample(-O(i))) * W(i) for i in [1, n],
    // where O(i) = (G(2i - 1) * (2i - 1) + G(2i) * 2i) / (G(2i - 1) + G(2i)), and
    // W(i) = G(2i - 1) + G(2i). This makes the shader half as big.
    //
    // For reference: G(x) = e ** (-x ** 2 / 2 / s ** 2) / (2 * pi * s ** 2) ** 0.5.
    // In this case, n = s = 5; this cuts off about 3% of intensity.
    const float offset[] = {1.4850044983805901, 3.4650570548417856, 5.4452207648927855, 7.425557483188341, 9.406126897065857};
    const float weight[] = {0.15186256685575583, 0.12458323113065647, 0.08723135590047126, 0.05212966006304008, 0.026588224962816442};
    const float w0 = 0.07978845608028654;

    float2 duv = ddx(input.Tex) * (1 - input.Blw) + ddy(input.Tex) * input.Blw;
    float4 color = tx.Sample(samLinear, input.Tex) * w0;
    for (int i = 0; i < 5; i++)
        color += (tx.Sample(samLinear, input.Tex + duv * offset[i]) +
                  tx.Sample(samLinear, input.Tex - duv * offset[i])) * weight[i];
    return color;
}
