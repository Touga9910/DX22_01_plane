//二つのhlslファイルの共通点をまとめたもの。
//これ単体で動くことはない（エントリポイントがないから動かない）ので、ビルドから除外しないとエラーが出る

cbuffer WorldBuffer : register(b0)
{
	matrix World;
}
cbuffer ViewBuffer : register(b1)
{
	matrix View;
}
cbuffer ProjectionBuffer : register(b2)
{
	matrix Projection;
}

struct VS_IN
{
    float4 pos : POSITION0;
	float4 nrm : NORMAL0;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
};

struct PS_IN
{
	float4 pos : SV_POSITION;
	float4 col : COLOR0;
	float2 tex : TEXCOORD0;
};

struct LIGHT
{
    bool Enable;        // 使用するか否か
    bool3 Dummy;        // アライメント調整用
    float4 Direction;   // 方向
    float4 Diffuse;     // 拡散反射用の光の強さ
    float4 Ambient;     // 環境光用の光の強さ
};

cbuffer LightBuffer : register(b3)  //三番目の定数バッファに光源情報
{
    LIGHT Light;
}

struct MATERIAL
{
    float4 Ambient; // 環境光用の光の強さ
    float4 Diffuse; // 拡散反射用の光の強さ
    float4 Specular;
    float4 Emission;
    float Shininess;
    bool TextureEnable;
    bool2 Dummy;
};

cbuffer MaterialBuffer : register(b4) //四番目の定数バッファにマテリアル情報
{
    MATERIAL Material;
}

cbuffer TextureBuffer : register(b5) //五番目の定数バッファにUV情報
{
    matrix matrixTex;
}
