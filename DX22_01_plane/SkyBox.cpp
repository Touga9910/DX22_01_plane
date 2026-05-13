#include "SkyBox.h"
#include "Game.h"


#include "stb_image.h""

using namespace DirectX::SimpleMath;

//=======================================
//初期化処理
//=======================================
/*
void SkyBox::Init()
{
	// 頂点データ
	// 単一の四角形（三角形2枚、頂点6つ）を作成
	m_Vertices.resize(6);

	// X-Z平面上に配置 (底面)
	const float HALF_SIZE = 0.5f;
	const Vector3 NORMAL_INWARD = Vector3(0, -1, 0); // 法線を内側 (下) に向ける

	// 巻き順を調整し、法線を (-Y) 方向に設定することで、内側から見たときに正常に見えるようにする。

	// 四角形の四隅の座標
	// 左上: (-0.5, 0, 0.5)
	// 右上: ( 0.5, 0, 0.5)
	// 左下: (-0.5, 0,-0.5)
	// 右下: ( 0.5, 0,-0.5)

	// ■ 1枚目の三角形 (左上, 右上, 左下)
	m_Vertices[0].position = Vector3(-HALF_SIZE, 0, HALF_SIZE); // V0 (左上)
	m_Vertices[1].position = Vector3(HALF_SIZE, 0, HALF_SIZE); // V1 (右上)
	m_Vertices[2].position = Vector3(-HALF_SIZE, 0, -HALF_SIZE); // V2 (左下)

	// ■ 2枚目の三角形 (左下, 右上, 右下)
	m_Vertices[3].position = Vector3(-HALF_SIZE, 0, -HALF_SIZE); // V3 (左下)
	m_Vertices[4].position = Vector3(HALF_SIZE, 0, HALF_SIZE); // V4 (右上)
	m_Vertices[5].position = Vector3(HALF_SIZE, 0, -HALF_SIZE); // V5 (右下)

	// カラーとUV (テクスチャ座標)
	m_Vertices[0].color = Color(1, 1, 1, 1);
	m_Vertices[1].color = Color(1, 1, 1, 1);
	m_Vertices[2].color = Color(1, 1, 1, 1);
	m_Vertices[3].color = Color(1, 1, 1, 1);
	m_Vertices[4].color = Color(1, 1, 1, 1);
	m_Vertices[5].color = Color(1, 1, 1, 1);

	// UV座標 (テクスチャ全体を使うため、0-1の範囲)
	m_Vertices[0].uv = Vector2(0, 0); // V0 左上
	m_Vertices[1].uv = Vector2(1, 0); // V1 右上
	m_Vertices[2].uv = Vector2(0, 1); // V2 左下
	m_Vertices[3].uv = Vector2(0, 1); // V3 左下 (重複)
	m_Vertices[4].uv = Vector2(1, 0); // V4 右上 (重複)
	m_Vertices[5].uv = Vector2(1, 1); // V5 右下

	// ★ 法線を内側 (下向き) に固定 ★
	m_Vertices[0].normal = NORMAL_INWARD;
	m_Vertices[1].normal = NORMAL_INWARD;
	m_Vertices[2].normal = NORMAL_INWARD;
	m_Vertices[3].normal = NORMAL_INWARD;
	m_Vertices[4].normal = NORMAL_INWARD;
	m_Vertices[5].normal = NORMAL_INWARD;

	// -----------------------------------------------------------------
	// ★ 法線ベクトルの再計算は、このシンプルな正方形では不要 ★
	// 法線計算のループは不要です。
	// -----------------------------------------------------------------


	// 頂点バッファ生成
	m_VertexBuffer.Create(m_Vertices);

	// インデックデータ
	m_Indices.resize(6 * 6);

	// 頂点バッファと同じ順番でインデックスを設定
	m_Indices[0] = 0;
	m_Indices[1] = 1;
	m_Indices[2] = 2;
	m_Indices[3] = 3;
	m_Indices[4] = 4;
	m_Indices[5] = 5;


	// インデックスバッファ生成
	m_IndexBuffer.Create(m_Indices);

	// シェーダオブジェクト生成 (既存のものを流用)
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	// テクスチャロード
	bool sts = m_Texture.Load("assets/texture/SkyBox.jpg"); // 底面用のテクスチャ
	assert(sts == true);

	//マテリアル情報取得
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true;
	m_Material->Create(mtrl);

	// スカイボックスとして使用するため、位置は Update でカメラに合わせる。
	// スケールは Skybox::Update() で 10000.0f などに設定することを推奨。
	m_Position = Vector3::Zero;
	m_Scale = Vector3::One;
}
*/

// Skybox::Init() の内部に実装することを想定

using namespace DirectX::SimpleMath;

void SkyBox::Init()
{
	// 頂点データ (6面 x 6頂点 = 36頂点)
	m_Vertices.resize(36);

	// インデックスデータ (36インデックス)
	m_Indices.resize(36);

	// 立方体の一辺の半分のサイズ
	const float HALF_SIZE = 0.5f;

	// -----------------------------------------------------------------
	// ★ 頂点座標の定義 (原点中心) ★
	// -----------------------------------------------------------------

	// 8つの角の座標 (P0からP7)
	// P0: (-0.5, -0.5, -0.5) // 後ろ、下、左
	// P1: ( 0.5, -0.5, -0.5) // 後ろ、下、右
	// P2: (-0.5,  0.5, -0.5) // 後ろ、上、左
	// P3: ( 0.5,  0.5, -0.5) // 後ろ、上、右
	// P4: (-0.5, -0.5,  0.5) // 前、下、左
	// P5: ( 0.5, -0.5,  0.5) // 前、下、右
	// P6: (-0.5,  0.5,  0.5) // 前、上、左
	// P7: ( 0.5,  0.5,  0.5) // 前、上、右

	// Vector3 の配列として定義すると、コードがスッキリします
	const Vector3 P[8] = {
		{-HALF_SIZE, -HALF_SIZE, -HALF_SIZE},
		{ HALF_SIZE, -HALF_SIZE, -HALF_SIZE},
		{-HALF_SIZE,  HALF_SIZE, -HALF_SIZE},
		{ HALF_SIZE,  HALF_SIZE, -HALF_SIZE},
		{-HALF_SIZE, -HALF_SIZE,  HALF_SIZE},
		{ HALF_SIZE, -HALF_SIZE,  HALF_SIZE},
		{-HALF_SIZE,  HALF_SIZE,  HALF_SIZE},
		{ HALF_SIZE,  HALF_SIZE,  HALF_SIZE},
	};

	// -----------------------------------------------------------------
	// ★ 法線ベクトルの定義 (内側向き) ★
	// -----------------------------------------------------------------
	const Vector3 N_UP = Vector3(0, 1, 0);
	const Vector3 N_DOWN = Vector3(0, -1, 0);
	const Vector3 N_FRONT = Vector3(0, 0, -1); // 前面は Z 軸負方向が内側
	const Vector3 N_BACK = Vector3(0, 0, 1);  // 背面は Z 軸正方向が内側
	const Vector3 N_RIGHT = Vector3(-1, 0, 0); // 右面は X 軸負方向が内側
	const Vector3 N_LEFT = Vector3(1, 0, 0);  // 左面は X 軸正方向が内側

	// -----------------------------------------------------------------
	// ★ UV座標の定義 ★
	// -----------------------------------------------------------------
	const Vector2 UV[4] = {
		{0, 1}, // 左下 (0,1)
		{1, 1}, // 右下 (1,1)
		{0, 0}, // 左上 (0,0)
		{1, 0}, // 右上 (1,0)
	};

	int vertexIndex = 0;

	// 各面を定義するラムダ関数（コードの重複を避けるため）
	auto setFace = [&](const Vector3& p1, const Vector3& p2, const Vector3& p3,
		const Vector3& p4, const Vector3& normal, int uv_tl, int uv_tr, int uv_bl, int uv_br)
		{
			// 巻き順は、法線方向が内側を向くように調整してください。
			// ここでは、外側から見たとき時計回りになるように設定します。

			// 1枚目の三角形 (時計回り: P1, P2, P3)
			m_Vertices[vertexIndex + 0].position = p1; // V0
			m_Vertices[vertexIndex + 1].position = p2; // V1
			m_Vertices[vertexIndex + 2].position = p3; // V2

			// 2枚目の三角形 (時計回り: P3, P4, P1)
			m_Vertices[vertexIndex + 3].position = p3; // V3 (V2と同じ)
			m_Vertices[vertexIndex + 4].position = p4; // V4
			m_Vertices[vertexIndex + 5].position = p1; // V5 (V0と同じ)

			for (int i = 0; i < 6; ++i)
			{
				m_Vertices[vertexIndex + i].normal = normal;
				m_Vertices[vertexIndex + i].color = Color(1, 1, 1, 1);
			}

			// UV座標の割り当て
			m_Vertices[vertexIndex + 0].uv = UV[uv_tl]; // V0
			m_Vertices[vertexIndex + 1].uv = UV[uv_tr]; // V1
			m_Vertices[vertexIndex + 2].uv = UV[uv_bl]; // V2
			m_Vertices[vertexIndex + 3].uv = UV[uv_bl]; // V3
			m_Vertices[vertexIndex + 4].uv = UV[uv_br]; // V4
			m_Vertices[vertexIndex + 5].uv = UV[uv_tl]; // V5

			// インデックスを頂点と同じ番号で埋める
			for (int i = 0; i < 6; ++i) {
				m_Indices[vertexIndex + i] = vertexIndex + i;
			}

			vertexIndex += 6;
		};

	// -----------------------------------------------------------------
	// ★ 6面の定義 (法線が内側を向くように巻き順を調整) ★
	// -----------------------------------------------------------------

	// 1. 天井 (上向き) - 内側法線: N_DOWN (0, -1, 0)
	// P6(左上), P7(右上), P2(左下), P3(右下)
	setFace(P[6], P[7], P[2], P[3], N_DOWN, 2, 3, 0, 1);

	// 2. 床 (下向き) - 内側法線: N_UP (0, 1, 0)
	// P0(左下), P1(右下), P4(左上), P5(右上)
	setFace(P[0], P[1], P[4], P[5], N_UP, 0, 1, 2, 3);

	// 3. 前面 (Z+向き) - 内側法線: N_FRONT (0, 0, -1)
	// P4(左下), P5(右下), P6(左上), P7(右上)
	setFace(P[4], P[5], P[6], P[7], N_FRONT, 0, 1, 2, 3);

	// 4. 背面 (Z-向き) - 内側法線: N_BACK (0, 0, 1)
	// P1(右下), P0(左下), P3(右上), P2(左上)
	setFace(P[1], P[0], P[3], P[2], N_BACK, 1, 0, 3, 2);

	// 5. 右面 (X+向き) - 内側法線: N_RIGHT (-1, 0, 0)
	// P5(下), P1(奥), P7(上), P3(奥上)
	setFace(P[5], P[1], P[7], P[3], N_RIGHT, 0, 1, 2, 3);

	// 6. 左面 (X-向き) - 内側法線: N_LEFT (1, 0, 0)
	// P0(奥), P4(手前), P2(奥上), P6(手前上)
	setFace(P[0], P[4], P[2], P[6], N_LEFT, 1, 0, 3, 2);


	// -----------------------------------------------------------------
	// ★ バッファの作成とその他の設定 ★
	// -----------------------------------------------------------------

	// 頂点バッファ生成
	m_VertexBuffer.Create(m_Vertices);

	// インデックスバッファ生成
	m_IndexBuffer.Create(m_Indices);

	// シェーダオブジェクト生成
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	// テクスチャロード (6面共通のテクスチャを使う場合)
	bool sts = m_Texture.Load("assets/texture/SkyBox.jpg");
	assert(sts == true);

	// マテリアル情報
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true;
	m_Material->Create(mtrl);

	// スカイボックスとして必要な巨大なスケールを設定（InitまたはUpdateで）
	const float SKY_SCALE = 0.0f;
	m_Scale.x = m_Scale.y = m_Scale.z = SKY_SCALE;

	m_Position = Vector3::Zero;
	m_Rotation = Vector3::Zero;
}

//=======================================
//更新処理
//=======================================
void SkyBox::Update()
{
	//カメラの位置を取得
	Camera* cam = Game::GetInstance()->GetCamera();

	//位置をカメラと常に同じにする
	m_Position = cam->GetPosition();
}

//=======================================
//描画処理
//=======================================

void SkyBox::Draw(Camera* cam)
{

	//Renderer::SetDepthEnable(true);

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

	//Renderer::SetDepthEnable(false);
}

//=======================================
//終了処理
//=======================================
void SkyBox::Uninit()
{

}