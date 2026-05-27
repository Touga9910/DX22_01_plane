#include "Ground.h"
#include "stb_image.h"

using namespace DirectX::SimpleMath;//namespaceでDirectX::SimpleMathを名づけることで記述量を減らしている

//=======================================
//初期化処理
//=======================================
void Ground::Init()
{
	// 頂点データ
	m_SizeX = 1;
	m_SizeZ = 1;

	m_Vertices.resize(6 * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;
			m_Vertices[n + 0].position = Vector3(-0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 1].position = Vector3(0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 2].position = Vector3(-0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 3].position = Vector3(-0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 4].position = Vector3(0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 5].position = Vector3(0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);

			m_Vertices[n + 0].color = Color(1, 1, 1, 1);
			m_Vertices[n + 1].color = Color(1, 1, 1, 1);
			m_Vertices[n + 2].color = Color(1, 1, 1, 1);
			m_Vertices[n + 3].color = Color(1, 1, 1, 1);
			m_Vertices[n + 4].color = Color(1, 1, 1, 1);
			m_Vertices[n + 5].color = Color(1, 1, 1, 1);

			m_Vertices[n + 0].uv = Vector2(0,0);
			m_Vertices[n + 1].uv = Vector2(1,0);
			m_Vertices[n + 2].uv = Vector2(0,1);
			m_Vertices[n + 3].uv = Vector2(0,1);
			m_Vertices[n + 4].uv = Vector2(1,0);
			m_Vertices[n + 5].uv = Vector2(1,1);

			m_Vertices[n + 0].normal = Vector3(0, 1, 0);
			m_Vertices[n + 1].normal = Vector3(0, 1, 0);
			m_Vertices[n + 2].normal = Vector3(0, 1, 0);
			m_Vertices[n + 3].normal = Vector3(0, 1, 0);
			m_Vertices[n + 4].normal = Vector3(0, 1, 0);
			m_Vertices[n + 5].normal = Vector3(0, 1, 0);

		}
	}

	/*
	//読み込む画像ファイルのパス
	const char* filename = "assets/texture/terain.png";

	//画像データを格納するポインタ
	unsigned char* imageData = nullptr;
	int width, height, channels;

	//グレースケール（1チャネル）で画像を読み込む
	imageData = stbi_load(filename, &width, &height, &channels, 1);
	if (imageData)
	{
		for (int z = 0; z <= m_SizeZ; z++)
		{
			for (int x = 0; x <= m_SizeX; x++)
			{
				//高さの計算
				int picX = (int)(1 + x * (float)(width - 2) / m_SizeX);//左右１ピクセルを無視
				int picY = (int)(1 + z * (float)(height - 2) / m_SizeZ);//上下１ピクセルを無視

				unsigned char pixelValue = imageData[picY * width + picX];
				float h = (float)pixelValue / 5.0f;//Y座標

				//頂点に高さを代入
				int n = z * m_SizeX * 6 + x * 6;
				if (x != m_SizeX && z != m_SizeZ)
				{
					m_Vertices[n].position.y = h;
				}

				if (x != 0 && z != m_SizeZ)//左隣のポリゴン
				{
					m_Vertices[n - 2].position.y = h;
					m_Vertices[n - 5].position.y = h;
				}

				if (x != m_SizeX && z != 0)//上隣のポリゴン
				{
					m_Vertices[n - m_SizeX * 6 + 2].position.y = h;
					m_Vertices[n - m_SizeX * 6 + 3].position.y = h;
				}

				if (x != 0 && z != 0)//左上隣のポリゴン
				{
					m_Vertices[n - m_SizeX * 6 - 1].position.y = h;
					
				}
			}
		}
		//メモリを解放
		stbi_image_free(imageData);
	}
	
	//法線ベクトルを更新
	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;

			//二つのベクトルを計算
			Vector3 v1 = m_Vertices[n + 1].position - m_Vertices[n + 0].position;
			Vector3 v2 = m_Vertices[n + 2].position - m_Vertices[n + 0].position;
			Vector3 normal = v1.Cross(v2);	//外積を計算
			normal.Normalize();				//正規化
			
			m_Vertices[n + 0].normal = normal;
			m_Vertices[n + 1].normal = normal;
			m_Vertices[n + 2].normal = normal;

			//二つのベクトルを計算
			v1 = m_Vertices[n + 4].position - m_Vertices[n + 3].position;
			v2 = m_Vertices[n + 5].position - m_Vertices[n + 3].position;
			normal = v1.Cross(v2);			//外積を計算
			normal.Normalize();				//正規化

			m_Vertices[n + 3].normal = normal;
			m_Vertices[n + 4].normal = normal;
			m_Vertices[n + 5].normal = normal;
		}
	}
	*/

	// 頂点バッファ生成
	m_VertexBuffer.Create(m_Vertices);

	// インデックデータ
	m_Indices.resize(6 * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;

			m_Indices[n + 0] = n + 0;
			m_Indices[n + 1] = n + 1;
			m_Indices[n + 2] = n + 2;
			m_Indices[n + 3] = n + 3;
			m_Indices[n + 4] = n + 4;
			m_Indices[n + 5] = n + 5;
		}
	}

	// インデックスバッファ生成
	m_IndexBuffer.Create(m_Indices);

	// シェーダオブジェクト生成
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	// テクスチャロード
	bool sts = m_Texture.Load("assets/texture/field.jpg");
	assert(sts == true);	//上で指した画像がない場合エラーを吐く

	//マテリアル情報取得
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true;//テクスチャを使うか否かのフラグ
	m_Material->Create(mtrl);

	m_Position.y = -100.0f;
	m_Scale.x = 600.0f;
	m_Scale.z = 600.0f;
}

//=======================================
//更新処理
//=======================================
void Ground::Update()
{
	//例：画像の移動、回転、拡大縮小
	//m_Position.x += 0.2f;	//移動
	//m_Scale.y += 1.0f;	//拡大縮小
	//m_Rotation.x+= 0.05f;	//回転
}

//=======================================
//描画処理
//=======================================
void Ground::Draw(Camera* cam)
{
	//カメラを選択する
	cam->SetCamera();

	// SRT情報作成
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.x, m_Rotation.y, m_Rotation.z);//宇宙空間のように上下の境目のない場合だと、回転にバグが生じる可能性が高まる(基本はこれでいい)
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);

	Matrix worldmtx;
	worldmtx = s * r * t;
	Renderer::SetWorldMatrix(&worldmtx); // GPUにセット

	// 描画の処理
	ID3D11DeviceContext* devicecontext;
	devicecontext = Renderer::GetDeviceContext();

	// トポロジーをセット（プリミティブタイプ）
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_Shader.SetGPU();
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();

	m_Texture.SetGPU();
	m_Material->SetGPU();

	devicecontext->DrawIndexed(
		m_Indices.size(),	// 描画するインデックス数
		0,					// 最初のインデックスバッファの位置
		0);
}

//=======================================
//終了処理
//=======================================
void Ground::Uninit()
{

}

//=======================================
//頂点情報を取得
//=======================================
std::vector<VERTEX_3D>Ground::GetVertices()
{
	std::vector<VERTEX_3D>res;
	res.resize(m_Vertices.size());

	//頂点情報を変換
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);
	Matrix worldmtx = s * r * t;

	//ワールド変換してデータを代入
	for (int i = 0; i < m_Vertices.size(); i++)
	{
		res[i].position = Vector3::Transform(m_Vertices[i].position, worldmtx);
		res[i].normal = Vector3::Transform(m_Vertices[i].normal, worldmtx);
		res[i].color = m_Vertices[i].color;
		res[i].uv = m_Vertices[i].uv;
	}
	return res;
}