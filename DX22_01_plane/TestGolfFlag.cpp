#include	"TestGolfFlag.h"

#include<iostream>
using namespace DirectX::SimpleMath;

//=======================================
//初期化処理
//=======================================
void TestGolfFlag::Init()
{
	// 頂点データ
	m_Vertices.resize(14);//調点数

	//Z+方向が上、X+方向が右

	//六角形（上面）
	m_Vertices[0].position = Vector3(0, 10, 5);				//上
	m_Vertices[1].position = Vector3(4.33, 10, 2.5);		//右上
	m_Vertices[2].position = Vector3(4.33, 10, -2.5);		//右下
	m_Vertices[3].position = Vector3(0, 10, -5);			//下
	m_Vertices[4].position = Vector3(-4.33, 10, -2.5);		//左下
	m_Vertices[5].position = Vector3(-4.33, 10, 2.5);		//左上
	m_Vertices[6].position = Vector3(0, 10, 0);				//中心

	//六角形（下面）
	m_Vertices[7].position = Vector3(0, -10, 5);			//上
	m_Vertices[8].position = Vector3(4.33, -10, 2.5);		//右上
	m_Vertices[9].position = Vector3(4.33, -10, -2.5);		//右下
	m_Vertices[10].position = Vector3(0, -10, -5);			//下
	m_Vertices[11].position = Vector3(-4.33, -10, -2.5);	//左下
	m_Vertices[12].position = Vector3(-4.33, -10, 2.5);		//左上
	m_Vertices[13].position = Vector3(0, -10, 0);			//中心




	//m_Vertices[7].position = Vector3(-10, -10, 10);		//左奥下


	//m_Vertices[8].position = Vector3(0, 20, 0);

	/*
	●各頂点の位置
	上面（上がZ+、右がX+）
	0 1
	2 3

	下面
	7 6
	4 5
	*/

	m_Vertices[0].color = Color(0, 1, 1, 1);
	m_Vertices[1].color = Color(1, 1, 1, 1);
	m_Vertices[2].color = Color(1, 1, 1, 1);
	m_Vertices[3].color = Color(1, 1, 1, 1);
	m_Vertices[4].color = Color(1, 1, 1, 1);
	m_Vertices[5].color = Color(1, 1, 1, 1);
	m_Vertices[6].color = Color(1, 1, 1, 1);

	m_Vertices[7].color = Color(1, 1, 1, 1);
	m_Vertices[8].color = Color(1, 1, 1, 1);
	m_Vertices[9].color = Color(1, 1, 1, 1);
	m_Vertices[10].color = Color(1, 1, 1, 1);
	m_Vertices[11].color = Color(1, 1, 1, 1);
	m_Vertices[12].color = Color(1, 1, 1, 1);
	m_Vertices[13].color = Color(1, 1, 1, 1);



	m_Vertices[0].uv = Vector2(0, 0);
	m_Vertices[1].uv = Vector2(0, 0);
	m_Vertices[2].uv = Vector2(0, 0);
	m_Vertices[3].uv = Vector2(0, 0);
	m_Vertices[4].uv = Vector2(0, 0);
	m_Vertices[5].uv = Vector2(0, 0);
	m_Vertices[6].uv = Vector2(0, 0);

	m_Vertices[7].uv = Vector2(0, 0);
	m_Vertices[8].uv = Vector2(0, 0);
	m_Vertices[9].uv = Vector2(0, 0);
	m_Vertices[10].uv = Vector2(0, 0);
	m_Vertices[11].uv = Vector2(0, 0);
	m_Vertices[12].uv = Vector2(0, 0);
	m_Vertices[13].uv = Vector2(0, 0);


	// 頂点バッファ生成
	m_VertexBuffer.Create(m_Vertices);

	// インデックデータ
	m_Indices.resize(75);

	//TRIANGELELISTで生成する時、行ごとに一つの三角形
	//時計回りに指定しないと、ポリゴンの裏で消えてしまう
	m_Indices = {
		//上面
		0,1,6,
		1,2,6,
		2,3,6,
		3,4,6,
		4,5,6,
		5,0,6,

		//下面
		8,7,13,
		9,8,13,
		10,9,13,
		11,10,13,
		12,11,13,
		7,12,13,

		//側面
		1,0,7,
		1,7,8,

		2,1,8,
		2,8,9,

		3,2,9,
		3,9,10,

		4,3,10,
		4,10,11,

		5,4,11,
		5,11,12,

		0,5,12,
		0,12,7
		
	};

	// インデックスバッファ生成
	m_IndexBuffer.Create(m_Indices);

	// シェーダオブジェクト生成
	m_Shader.Create("shader/unlitTextureVS.hlsl", "shader/unlitTexturePS.hlsl");
}

//=======================================
//更新処理
//=======================================
void TestGolfFlag::Update()
{
	//回転する
	m_Rotation.y += 0.03f;
	//m_Rotation.x += 0.02f;
	//m_Rotation.z += 0.02f;

	//移動する
	//m_Position.y += 0.01f;
	//m_Position.x+= 0.01f;
	//m_Position.z+= 0.01f;

	//iostreamをインクルードすることでデバック用のウィンドウに表示できる
	//std::cout << "X軸：" << m_Rotation.x << std::endl;
}

//=======================================
//描画処理
//=======================================
void TestGolfFlag::Draw(Camera* cam)
{
	//カメラを選択する
	cam->SetCamera();

	// SRT情報作成
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);

	Matrix worldmtx;
	worldmtx = s * r * t;
	Renderer::SetWorldMatrix(&worldmtx); // GPUにセット

	// 描画の処理
	ID3D11DeviceContext* devicecontext;
	devicecontext = Renderer::GetDeviceContext();

	// トポロジーをセット（プリミティブタイプ）
	//頂点の結び方
	/*

	・TRIANGLESTRIP：頂点番号Xとしたとき、x, x - 1, x - 2の三頂点で三角形を作る)
	・TRIANGELIST　：三つの頂点を三角形にして結んでいく（表側からみて時計回りに指定する必要あり）

	*/
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_Shader.SetGPU();
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();

	devicecontext->DrawIndexed(
		m_Indices.size(),	// 描画するインデックス数
		0,					// 最初のインデックスバッファの位置
		0);
}

//=======================================
//終了処理
//=======================================
void TestGolfFlag::Uninit()
{

}
