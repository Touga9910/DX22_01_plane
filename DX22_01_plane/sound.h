#pragma once

#include <xaudio2.h>

// XAudio2で管理するサウンドの識別子を定義する。
// 各列挙値はSoundクラスの配列インデックスとして使用する。
typedef enum
{
	SOUND_LABEL_BGM000 = 0,		// サンプルBGM
	//SOUND_LABEL_BGM001,		// BGM
	//SOUND_LABEL_SE000,		// SE
	//SOUND_LABEL_SE001,		// SE

	SOUND_LABEL_MAX,			// 登録されているサウンド数。配列サイズとして使用する
} SOUND_LABEL;

// WAVファイルの読み込みとXAudio2による再生・停止・再開を管理する。
// SOUND_LABELを指定することで、登録済みの各サウンドを操作する。
class Sound {
private:
	// -------------------------
	// サウンド設定
	// -------------------------

	// 各サウンドのファイルパスとループ再生設定を保持する。
	typedef struct
	{
		LPCSTR filename;	// 読み込む音声ファイルのパス
		bool bLoop;			// trueの場合はループ再生、falseの場合は1回のみ再生する
	} PARAM;

	// SOUND_LABELごとの音声ファイルと再生方法を定義する。
	PARAM m_param[SOUND_LABEL_MAX] =
	{
		{"asset/BGM/sample000.wav", true},	// サンプルBGM。ループ再生する
		//		{"asset/BGM/○○○.wav", true},		// BGM
		//		{"asset/SE/○○○.wav", false},  	// SE。ループ再生しない
		//		{"asset/SE/○○○.wav", false},		// SE。ループ再生しない
	};

	// -------------------------
	// XAudio2・音声データ
	// -------------------------

	IXAudio2* m_pXAudio2 = NULL;							// XAudio2本体を管理するインターフェース
	IXAudio2MasteringVoice* m_pMasteringVoice = NULL;		// 最終的な音声出力を行うマスタリングボイス
	IXAudio2SourceVoice* m_pSourceVoice[SOUND_LABEL_MAX];	// SOUND_LABELごとの音声再生用ソースボイス
	WAVEFORMATEXTENSIBLE m_wfx[SOUND_LABEL_MAX];			// SOUND_LABELごとのWAVフォーマット情報
	XAUDIO2_BUFFER m_buffer[SOUND_LABEL_MAX];				// XAudio2へ渡すSOUND_LABELごとの再生バッファ情報
	BYTE* m_DataBuffer[SOUND_LABEL_MAX];					// WAVファイルから読み込んだ音声データを保持するバッファ

	// -------------------------
	// WAVファイル読み込み
	// -------------------------

	// WAVファイル内から指定されたチャンクを検索し、
	// 見つかったチャンクのサイズとデータ位置を取得する。
	HRESULT FindChunk(HANDLE, DWORD, DWORD&, DWORD&);

	// WAVファイルの指定位置から、指定サイズ分のデータを読み込む。
	HRESULT ReadChunkData(HANDLE, void*, DWORD, DWORD);

public:
	// -------------------------
	// 初期化・終了
	// -------------------------

	// XAudio2の生成、WAVファイルの読み込み、再生用ボイスの準備を行う。
	// ゲームループ開始前に呼び出す。
	HRESULT Init(void);

	// 生成したXAudio2関連リソースと読み込んだ音声データを解放する。
	// ゲームループ終了後に呼び出す。
	void Uninit(void);

	// -------------------------
	// サウンド再生制御
	// -------------------------

	void Play(SOUND_LABEL label);		// 指定したサウンドを再生する
	void Stop(SOUND_LABEL label);		// 指定したサウンドを停止する
	void Resume(SOUND_LABEL label);		// 停止している指定サウンドの再生を再開する
};
