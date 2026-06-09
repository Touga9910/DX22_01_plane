#pragma once
#include "Camera.h"
#include "Shader.h"
#include "transform.h"

class Object {
protected:
	// SRT情報（姿勢情報）
	DirectX::SimpleMath::Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

	Transform m_Transform; 

	// 描画の為の情報（見た目に関わる部分）
	Shader m_Shader; // シェーダー

public:
	virtual ~Object() {}	//仮想デストラクタ（派生クラスのデストラクタを発生させるため）

	virtual void Init()=0;
	virtual void Update() = 0;
	virtual void Draw(Camera* cam) = 0;
	virtual void Uninit() = 0;

	// 姿勢情報の取得
	Transform GetTransform() const { return m_Transform; }

	// 位置の取得
	DirectX::SimpleMath::Vector3 GetPosition() const { return m_Position; }
};
