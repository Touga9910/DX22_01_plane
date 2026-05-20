#include	"TestCube.h"
#include	"input.h"

#include<iostream>
using namespace DirectX::SimpleMath;

//=======================================
//初期化処理
//=======================================
void TestCube::Init()
{
	// 頂点データ
	m_Vertices.resize(24);//調点数

	/*
	m_Vertices[0].position = Vector3(-10, 10, 10);		//左奥上
	m_Vertices[1].position = Vector3(10, 10, 10);		//右奥上
	m_Vertices[2].position = Vector3(-10, 10, -10);		//左前上
	m_Vertices[3].position = Vector3(10, 10, -10);		//右前上
	m_Vertices[4].position = Vector3(-10, -10, -10);	//左前下
	m_Vertices[5].position = Vector3(10, -10, -10);		//右前下
	m_Vertices[6].position = Vector3(10, -10, 10);		//右奥下
	m_Vertices[7].position = Vector3(-10, -10, 10);		//左奥下

	m_Vertices[0].color = Color(1, 1, 1, 1);
	m_Vertices[1].color = Color(1, 1, 1, 1);
	m_Vertices[2].color = Color(1, 1, 1, 1);
	m_Vertices[3].color = Color(1, 1, 1, 1);
	m_Vertices[4].color = Color(1, 1, 1, 1);
	m_Vertices[5].color = Color(1, 1, 1, 1);
	m_Vertices[6].color = Color(1, 1, 1, 1);
	m_Vertices[7].color = Color(1, 1, 1, 1);

	m_Vertices[0].uv = Vector2(0, 0);
	m_Vertices[1].uv = Vector2(0, 0);
	m_Vertices[2].uv = Vector2(0, 0);
	m_Vertices[3].uv = Vector2(0, 0);
	m_Vertices[4].uv = Vector2(0, 0);
	m_Vertices[5].uv = Vector2(0, 0);
	m_Vertices[6].uv = Vector2(0, 0);
	m_Vertices[7].uv = Vector2(0, 0);
	*/

	//法線ベクトルY+
	m_Vertices[0].position = Vector3(-10, 10, 10);
	m_Vertices[1].position = Vector3(10, 10, 10);
	m_Vertices[2].position = Vector3(-10, 10, -10);
	m_Vertices[3].position = Vector3(10, 10, -10);

	//法線ベクトルZ-
	m_Vertices[4].position = Vector3(-10, 10, -10);
	m_Vertices[5].position = Vector3(10, 10, -10);
	m_Vertices[6].position = Vector3(-10, -10, -10);
	m_Vertices[7].position = Vector3(10, -10, -10);

	//法線ベクトルY-
	m_Vertices[8].position = Vector3(-10, -10, 10);
	m_Vertices[9].position = Vector3(10, -10, 10);
	m_Vertices[10].position = Vector3(-10, -10, -10);
	m_Vertices[11].position = Vector3(10, -10, -10);

	//法線ベクトルZ+
	m_Vertices[12].position = Vector3(-10, 10, 10);
	m_Vertices[13].position = Vector3(10, 10, 10);
	m_Vertices[14].position = Vector3(-10, -10, 10);
	m_Vertices[15].position = Vector3(10, -10, 10);

	//法線ベクトルX+
	m_Vertices[16].position = Vector3(-10, -10, 10);
	m_Vertices[17].position = Vector3(-10, 10, 10);
	m_Vertices[18].position = Vector3(-10, -10, -10);
	m_Vertices[19].position = Vector3(-10, 10, -10);

	//法線ベクトルX-
	m_Vertices[20].position = Vector3(10, -10, -10);
	m_Vertices[21].position = Vector3(10, 10, -10);
	m_Vertices[22].position = Vector3(10, -10, 10);
	m_Vertices[23].position = Vector3(10, 10, 10);


	m_Vertices[0].color = Color(1, 1, 1, 1);
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
	m_Vertices[14].color = Color(1, 1, 1, 1);
	m_Vertices[15].color = Color(1, 1, 1, 1);
	m_Vertices[16].color = Color(1, 1, 1, 1);
	m_Vertices[17].color = Color(1, 1, 1, 1);
	m_Vertices[18].color = Color(1, 1, 1, 1);
	m_Vertices[19].color = Color(1, 1, 1, 1);
	m_Vertices[20].color = Color(1, 1, 1, 1);
	m_Vertices[21].color = Color(1, 1, 1, 1);
	m_Vertices[22].color = Color(1, 1, 1, 1);
	m_Vertices[23].color = Color(1, 1, 1, 1);

	m_Vertices[0].uv = Vector2(0, 0);
	m_Vertices[1].uv = Vector2(0.33f, 0);
	m_Vertices[2].uv = Vector2(0, 0.5f);
	m_Vertices[3].uv = Vector2(0.33f, 0.5f);

	m_Vertices[4].uv = Vector2(0.33f, 0);
	m_Vertices[5].uv = Vector2(0.66f, 0);
	m_Vertices[6].uv = Vector2(0.33f, 0.5f);
	m_Vertices[7].uv = Vector2(0.66f, 0.5f);
	
	m_Vertices[8].uv = Vector2(0.66f, 0.5f);
	m_Vertices[9].uv = Vector2(1.0f, 0.5f);
	m_Vertices[10].uv = Vector2(0.66f, 1.0f);
	m_Vertices[11].uv = Vector2(1.0f, 1.0f);
	
	m_Vertices[12].uv = Vector2(0.33f, 0.5f);
	m_Vertices[13].uv = Vector2(0.66f, 0.5f);
	m_Vertices[14].uv = Vector2(0.33f, 1.0f);
	m_Vertices[15].uv = Vector2(0.66f, 1.0f);
	
	m_Vertices[16].uv = Vector2(0.66f, 0);
	m_Vertices[17].uv = Vector2(1.0f, 0);
	m_Vertices[18].uv = Vector2(0.66, 0.5);
	m_Vertices[19].uv = Vector2(1.0f, 0.5);
	
	m_Vertices[20].uv = Vector2(0, 0.5f);
	m_Vertices[21].uv = Vector2(0.33f, 0.5f);
	m_Vertices[22].uv = Vector2(0, 1.0f);
	m_Vertices[23].uv = Vector2(0.33f, 1.0f);

	m_Vertices[0].normal = Vector3(0, 1, 0);
	m_Vertices[1].normal = Vector3(0, 1, 0);
	m_Vertices[2].normal = Vector3(0, 1, 0);
	m_Vertices[3].normal = Vector3(0, 1, 0);

	m_Vertices[4].normal = Vector3(0, 0, -1);
	m_Vertices[5].normal = Vector3(0, 0, -1);
	m_Vertices[6].normal = Vector3(0, 0, -1);
	m_Vertices[7].normal = Vector3(0, 0, -1);

	m_Vertices[8].normal = Vector3(0, -1, 0);
	m_Vertices[9].normal = Vector3(0, -1, 0);
	m_Vertices[10].normal = Vector3(0, -1, 0);
	m_Vertices[11].normal = Vector3(0, -1, 0);

	m_Vertices[12].normal = Vector3(0, 0, 1);
	m_Vertices[13].normal = Vector3(0, 0, 1);
	m_Vertices[14].normal = Vector3(0, 0, 1);
	m_Vertices[15].normal = Vector3(0, 0, 1);

	m_Vertices[16].normal = Vector3(-1, 0, 0);
	m_Vertices[17].normal = Vector3(-1, 0, 0);
	m_Vertices[18].normal = Vector3(-1, 0, 0);
	m_Vertices[19].normal = Vector3(-1, 0, 0);

	m_Vertices[20].normal = Vector3(1, 0, 0);
	m_Vertices[21].normal = Vector3(1, 0, 0);
	m_Vertices[22].normal = Vector3(1, 0, 0);
	m_Vertices[23].normal = Vector3(1, 0, 0);

	// 頂点バッファ生成
	m_VertexBuffer.Create(m_Vertices);

	// インデックデータ
	m_Indices.resize(36);

	//TRIANGELELISTで生成する時、行ごとに一つの三角形
	//時計回りに指定しないと、ポリゴンの裏で消えてしまう
	m_Indices = {
		0,1,2,
		1,3,2,

		4,5,6,
		5,7,6,

		8,10,9,
		9,10,11,

		12,14,13,
		13,14,15,

		16,17,18,
		17,19,18,

		20,21,22,
		21,23,22

		/*
		//Y+面 0123
		0,1,2,
		1,3,2,
		//Z-面 2345
		2,3,4,
		4,3,5,
		//Y-面 4567
		4,5,6,
		6,7,4,
		//Z+面 0176
		0,7,1,
		1,7,6,
		//X-面 0247
		0,4,7,
		0,2,4,
		//X+面 1356
		1,6,3,
		3,6,5,
		*/
		
	};


	m_Position.x -= 20;

	// インデックスバッファ生成
	m_IndexBuffer.Create(m_Indices);

	// シェーダオブジェクト生成
	//m_Shader.Create("shader/unlitTextureVS.hlsl", "shader/unlitTexturePS.hlsl");
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	//テクスチャロード
	bool sts = m_Texture.Load("assets/texture/dice.png");
	
	assert(sts == true);	//上で指した画像がない場合エラーを吐く

}

//=======================================
//更新処理
//=======================================
void TestCube::Update()
{
	//回転することでキューブ上になっているかの確認
	m_Rotation.y += 0.01f;
	//m_Rotation.x += 0.01f;
	//m_Rotation.z += 0.01f;

	if (Input::GetKeyPress(VK_W))
	{
		m_Position.z += 1.0f;
		std::cout << "キューブ：上に移動" << std::endl;
	}
	if (Input::GetKeyPress(VK_S))
	{
		m_Position.z -= 1.0f;
		std::cout << "キューブ：下に移動" << std::endl;
	}
	if (Input::GetKeyPress(VK_A))
	{
		m_Position.x -= 1.0f;
		std::cout << "キューブ：右に移動" << std::endl;
	}
	if (Input::GetKeyPress(VK_D))
	{
		m_Position.x += 1.0f;
		std::cout << "キューブ：左に移動" << std::endl;
	}

	//iostreamをインクルードすることでデバック用のウィンドウに表示できる
	//std::cout << "X軸：" << m_Rotation.x << std::endl;
}

//=======================================
//描画処理
//=======================================
void TestCube::Draw(Camera* cam)
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
	
	m_Texture.SetGPU();

	devicecontext->DrawIndexed(
		m_Indices.size(),	// 描画するインデックス数
		0,					// 最初のインデックスバッファの位置
		0);
}

//=======================================
//終了処理
//=======================================
void TestCube::Uninit()
{

}
