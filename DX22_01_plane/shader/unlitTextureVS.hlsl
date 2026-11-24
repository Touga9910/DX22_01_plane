#include "common.hlsl"

/// <summary>
/// 頂点をクリップ空間へ変換し、UV変換行列を適用した座標と頂点色をピクセルシェーダーへ渡す。
/// </summary>
PS_IN main(in VS_IN input)
{
    PS_IN output;
	
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);

    output.pos = mul(input.pos, wvp);
    
    //UV座標を移動させる
    float4 uv;
    uv.xy = input.tex;          //行列掛け算のためfloat型に映す
    uv.z = 0.0f;        
    uv.w = 1.0f;
    uv = mul(uv, matrixTex);    //UV座標と移動行列を掛け算
    output.tex = uv.xy;         //掛け算の結果を送信用変数にセット
   
    output.col = input.col;
	
    return output;
}

