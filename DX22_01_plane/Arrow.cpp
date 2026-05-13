#include "Arrow.h"
#include "Collision.h"
#include "Game.h"
#include "Golfball.h"
#include "Camera.h"

using namespace std;
using namespace DirectX::SimpleMath;

//=======================================
// 初期化処理
//=======================================
void Arrow::Init()
{
	// メッシュ読み込み
	StaticMesh staticmesh;

	// 3Dモデルデータ
	std::u8string modelFile = u8"assets/model/arrow/arrow.fbx";

	// テクスチャディレクトリ
	std::string texDirectory = "assets/model/arrow";

	// Meshを読み込む
	std::string tmpStr1(reinterpret_cast<const char*>(modelFile.c_str()), modelFile.size());
	staticmesh.Load(tmpStr1, texDirectory);

	m_MeshRenderer.Init(staticmesh);

	// シェーダオブジェクト生成
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	// サブセット情報取得
	m_subsets = staticmesh.GetSubsets();

	// テクスチャ情報取得
	m_Textures = staticmesh.GetTextures();

	// マテリアル情報取得
	vector<MATERIAL> materials = staticmesh.GetMaterials();

	// マテリアル数分ループ
	for (int i = 0; i < materials.size(); i++)
	{
		// マテリアルオブジェクト生成
		std::unique_ptr<Material> m = std::make_unique<Material>();

		// マテリアル情報をセット
		m->Create(materials[i]);

		// マテリアルオブジェクトを配列に追加
		m_Materials.push_back(std::move(m));
	}

	// モデルによってスケールを調整
	m_Scale.x = 2;
	m_Scale.y = 2;
	m_Scale.z = 2;

	m_State = 1;

}

//=======================================
// 更新処理
//=======================================
void Arrow::Update()
{
	if (m_State == 0)return; // 非表示ならreturn

	// ゴルフボールの位置を取得
	vector<GolfBall*> ballpt = Game::GetInstance()->GetObjects<GolfBall>();
	if (ballpt.size() > 0)
	{
		// 矢印の位置を更新
		m_Position = ballpt[0]->GetPosition();
	}

	GolfBall* ball = nullptr;
	if (ballpt.size() > 0)
	{
		ball = ballpt[0];
	}

	// 方向選択（横）
	if (m_State == 1)
	{
		m_Rotation.x = 0;
		m_Scale.z = 3; // 長さを固定
		if (Input::GetKeyPress(VK_D))m_Rotation.y += 0.03f;
		if (Input::GetKeyPress(VK_A))m_Rotation.y -= 0.03f;

		//m_Rotation.y = Camera::GetInstance().GetCameraDirection();
	}
	// 方向選択（縦）
	else if (m_State == 2)
	{
		m_Scale.z = 3;
		if (Input::GetKeyPress(VK_W))m_Rotation.x += 0.03f;
		if (Input::GetKeyPress(VK_S))m_Rotation.x -= 0.03f;
		//m_Rotation.x += 0.03f;

		//if (m_Rotation.x > 4.0)m_Rotation.x = 0;
	}
	// パワー選択なら
	else if (m_State == 3)
	{
		// 大きさを変更させる
		m_Scale.z += 0.04f;
		if (m_Scale.z > 4)m_Scale.z = 1;
	}

	if ((m_State == 1 || m_State == 2 || m_State == 3) && ball != nullptr)
	{
		// 現在の矢印が示すベクトル（速度・パワー）を取得
		Vector3 currentShotVector = GetVector();

		// 前回のベクトルと比べて差がほぼなかったら計算しない
		if ((currentShotVector - m_PrevVector).LengthSquared() > 0.001f)
		{
			// ボールオブジェクトに予測弾道の生成を要求
			// ボールは発射前なので、GetVector()の値を初速として渡す
			ball->GeneratePreTrajectory(currentShotVector);

			//現在のベクトルを前回のものとして保存しておく
			m_PrevVector = currentShotVector;
		}
	}
}

//=======================================
// 描画処理
//=======================================
void Arrow::Draw(Camera* cam)
{
	if (m_State == 0)return; // 非表示ならreturn

	//カメラを選択する
	cam->SetCamera();

	// SRT情報作成
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);

	Matrix worldmtx;
	worldmtx = s * r * t;
	Renderer::SetWorldMatrix(&worldmtx); // GPUにセット

	m_Shader.SetGPU();

	// インデックスバッファ・頂点バッファをセット
	m_MeshRenderer.BeforeDraw();

	//マテリアル数分ループ 
	for (int i = 0; i < m_subsets.size(); i++)
	{
		// マテリアルをセット(サブセット情報の中にあるマテリアルインデックスを使用)
		m_Materials[m_subsets[i].MaterialIdx]->SetGPU();

		if (m_Materials[m_subsets[i].MaterialIdx]->isTextureEnable())
		{
			m_Textures[m_subsets[i].MaterialIdx]->SetGPU();
		}

		m_MeshRenderer.DrawSubset(
			m_subsets[i].IndexNum, // 描画するインデックス数
			m_subsets[i].IndexBase, // 最初のインデックスバッファの位置	
			m_subsets[i].VertexBase); // 頂点バッファの最初から使用
	}
}

//=======================================
// 終了処理
//=======================================
void Arrow::Uninit()
{

}


//状態の設定
void Arrow::SetState(int s)
{
	m_State = s;
}

// 矢印のベクトルを取得
Vector3 Arrow::GetVector()
{
	//矢印の初期状態の向き
	Vector3 res = { 0, 0, -1 };

	// ベクトルを回転
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	res = Vector3::Transform(res, r);

	//矢印の長さ(パワー)を掛ける
	res *= m_Scale.z;

	return res;
}
