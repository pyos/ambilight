struct VS_INPUT {
    float4 Pos : POSITION;
    float4 Clr : COLOR;
    float2 Tex : TEXCOORD;
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float4 Clr : COLOR;
    float2 Tex : TEXCOORD;
};

VS_OUTPUT id_vertex(VS_INPUT input) {
    return input;
}
