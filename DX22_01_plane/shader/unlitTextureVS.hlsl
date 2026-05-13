#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
	
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);

    output.pos = mul(input.pos, wvp);
    
    //UVÀ•W‚ğˆÚ“®‚³‚¹‚é
    float4 uv;
    uv.xy = input.tex;          //s—ñŠ|‚¯Z‚Ì‚½‚ßfloatŒ^‚É‰f‚·
    uv.z = 0.0f;        
    uv.w = 1.0f;
    uv = mul(uv, matrixTex);    //UVÀ•W‚ÆˆÚ“®s—ñ‚ğŠ|‚¯Z
    output.tex = uv.xy;         //Š|‚¯Z‚ÌŒ‹‰Ê‚ğ‘—M—p•Ï”‚ÉƒZƒbƒg
   
    //output.tex = input.tex;
    output.col = input.col;
	
    return output;
}

