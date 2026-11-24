#include "sound.h"

#ifdef _XBOX // ビッグエンディアン
#define fourccRIFF 'RIFF'
#define fourccDATA 'data'
#define fourccFMT 'fmt '
#define fourccWAVE 'WAVE'
#define fourccXWMA 'XWMA'
#define fourccDPDS 'dpds'
#endif
#ifndef _XBOX // リトルエンディアン
#define fourccRIFF 'FFIR'
#define fourccDATA 'atad'
#define fourccFMT ' tmf'
#define fourccWAVE 'EVAW'
#define fourccXWMA 'AMWX'
#define fourccDPDS 'sdpd'
#endif

/// <summary>
/// XAudio2を初期化し、登録済みのWAVファイルを読み込んで再生用のバッファとボイスを用意する。
/// </summary>
HRESULT Sound::Init()
{
	HRESULT hr;

	HANDLE hFile;
	DWORD  dwChunkSize;
	DWORD  dwChunkPosition;
	DWORD  filetype;

	// COMの初期化
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		CoUninitialize();
		return -1;
	}

	/**** XAudio2を生成する ****/
	hr = XAudio2Create(&m_pXAudio2, 0);		// 第二引数は､動作フラグ デバッグモードの指定(現在は未使用なので0にする)
	if (FAILED(hr)) {
		CoUninitialize();
		return -1;
	}

	/**** マスタリングボイスを生成する ****/
	hr = m_pXAudio2->CreateMasteringVoice(&m_pMasteringVoice);			// 今回はＰＣのデフォルト設定に任せている
	if (FAILED(hr)) {
		if (m_pXAudio2)	m_pXAudio2->Release();
		CoUninitialize();
		return -1;
	}

	/**** サウンドを初期化する ****/
	for (int i = 0; i < SOUND_LABEL_MAX; i++)
	{
		memset(&m_wfx[i], 0, sizeof(WAVEFORMATEXTENSIBLE));
		memset(&m_buffer[i], 0, sizeof(XAUDIO2_BUFFER));

		hFile = CreateFileA(m_param[i].filename, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE) {
			return HRESULT_FROM_WIN32(GetLastError());
		}
		if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			return HRESULT_FROM_WIN32(GetLastError());
		}

		// RIFFチャンクからファイル形式を読み取り、WAVE形式か確認する
		FindChunk(hFile, fourccRIFF, dwChunkSize, dwChunkPosition);
		ReadChunkData(hFile, &filetype, sizeof(DWORD), dwChunkPosition);
		if (filetype != fourccWAVE)		return S_FALSE;

		FindChunk(hFile, fourccFMT, dwChunkSize, dwChunkPosition);
		ReadChunkData(hFile, &m_wfx[i], dwChunkSize, dwChunkPosition);

		// fourccDATAチャンクの内容を音声データバッファへ格納する。
		FindChunk(hFile, fourccDATA, dwChunkSize, dwChunkPosition);
		m_DataBuffer[i] = new BYTE[dwChunkSize];
		ReadChunkData(hFile, m_DataBuffer[i], dwChunkSize, dwChunkPosition);

		CloseHandle(hFile);

		// 	サブミットボイスで利用するサブミットバッファの設定
		m_buffer[i].AudioBytes = dwChunkSize;
		m_buffer[i].pAudioData = m_DataBuffer[i];
		m_buffer[i].Flags = XAUDIO2_END_OF_STREAM;
		if (m_param[i].bLoop)
			m_buffer[i].LoopCount = XAUDIO2_LOOP_INFINITE;
		else
			m_buffer[i].LoopCount = 0;

		m_pXAudio2->CreateSourceVoice(&m_pSourceVoice[i], &(m_wfx[i].Format));
	}

	return hr;
}

/// <summary>
/// 音声の再生を止め、バッファ、各ボイス、XAudio2、COMのリソースを解放する。
/// </summary>
void Sound::Uninit(void)
{
	for (int i = 0; i < SOUND_LABEL_MAX; i++)
	{
		if (m_pSourceVoice[i])
		{
			m_pSourceVoice[i]->Stop(0);
			m_pSourceVoice[i]->FlushSourceBuffers();
			m_pSourceVoice[i]->DestroyVoice();			// オーディオグラフからソースボイスを削除
			delete[]  m_DataBuffer[i];
		}
	}

	m_pMasteringVoice->DestroyVoice();

	if (m_pXAudio2) m_pXAudio2->Release();

	// COMの破棄
	CoUninitialize();
}

/// <summary>
/// 指定した音声のソースボイスを作り直し、バッファをキューに入れて再生する。
/// </summary>
void Sound::Play(SOUND_LABEL label)
{
	IXAudio2SourceVoice*& pSV = m_pSourceVoice[(int)label];

	if (pSV != nullptr)
	{
		pSV->DestroyVoice();
		pSV = nullptr;
	}

	// ソースボイス作成
	m_pXAudio2->CreateSourceVoice(&pSV, &(m_wfx[(int)label].Format));
	pSV->SubmitSourceBuffer(&(m_buffer[(int)label]));	// ボイスキューに新しいオーディオバッファーを追加

	// 再生
	pSV->Start(0);

}

/// <summary>
/// 指定した音声のソースボイスにバッファが残っていれば、再生を停止する。
/// </summary>
void Sound::Stop(SOUND_LABEL label)
{
	if (m_pSourceVoice[(int)label] == NULL) return;

	XAUDIO2_VOICE_STATE xa2state;
	m_pSourceVoice[(int)label]->GetState(&xa2state);
	if (xa2state.BuffersQueued)
	{
		m_pSourceVoice[(int)label]->Stop(0);
	}
}

/// <summary>
/// 指定した音声のソースボイスの再生を再開する。
/// </summary>
void Sound::Resume(SOUND_LABEL label)
{
	IXAudio2SourceVoice*& pSV = m_pSourceVoice[(int)label];
	pSV->Start();
}



/// <summary>
/// RIFFファイル内から指定された識別子のチャンクを探し、サイズとデータ開始位置を返す。
/// </summary>
HRESULT Sound::FindChunk(HANDLE hFile, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition)
{
	HRESULT hr = S_OK;
	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());
	DWORD dwChunkType;
	DWORD dwChunkDataSize;
	DWORD dwRIFFDataSize = 0;
	DWORD dwFileType;
	DWORD bytesRead = 0;
	DWORD dwOffset = 0;
	while (hr == S_OK)
	{
		DWORD dwRead;
		if (0 == ReadFile(hFile, &dwChunkType, sizeof(DWORD), &dwRead, NULL))
			hr = HRESULT_FROM_WIN32(GetLastError());
		if (0 == ReadFile(hFile, &dwChunkDataSize, sizeof(DWORD), &dwRead, NULL))
			hr = HRESULT_FROM_WIN32(GetLastError());
		switch (dwChunkType)
		{
		case fourccRIFF:
			dwRIFFDataSize = dwChunkDataSize;
			dwChunkDataSize = 4;
			if (0 == ReadFile(hFile, &dwFileType, sizeof(DWORD), &dwRead, NULL))
				hr = HRESULT_FROM_WIN32(GetLastError());
			break;
		default:
			if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, dwChunkDataSize, NULL, FILE_CURRENT))
				return HRESULT_FROM_WIN32(GetLastError());
		}
		dwOffset += sizeof(DWORD) * 2;
		if (dwChunkType == fourcc)
		{
			dwChunkSize = dwChunkDataSize;
			dwChunkDataPosition = dwOffset;
			return S_OK;
		}
		dwOffset += dwChunkDataSize;
		if (bytesRead >= dwRIFFDataSize) return S_FALSE;
	}
	return S_OK;
}

/// <summary>
/// 指定したファイル位置から所定のバイト数を読み込み、出力バッファに格納する。
/// </summary>
HRESULT Sound::ReadChunkData(HANDLE hFile, void* buffer, DWORD buffersize, DWORD bufferoffset)
{
	HRESULT hr = S_OK;
	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, bufferoffset, NULL, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());
	DWORD dwRead;
	if (0 == ReadFile(hFile, buffer, buffersize, &dwRead, NULL))
		hr = HRESULT_FROM_WIN32(GetLastError());
	return hr;
}
