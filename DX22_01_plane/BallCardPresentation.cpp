#include "BallCardPresentation.h"
#include "PlayerBallData.h"
#include "PlayerBallText.h"
#include "IllustratedUi.h"
#include "Renderer.h"
#include "imgui/imgui.h"
#include "stb_image.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <wrl/client.h>

namespace BallCardPresentation
{
namespace
{
	struct BallCardTexture
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		int width = 0;
		int height = 0;
		// 枠ごとの透明余白を除いた表示領域。通常画像は全体を使う。
		ImVec2 uvMin = ImVec2(0.0f, 0.0f);
		ImVec2 uvMax = ImVec2(1.0f, 1.0f);

		/// <summary>
		/// テクスチャのビューと正の画像寸法が揃っているかを確認する。
		/// </summary>
		bool IsValid() const noexcept
		{
			return view != nullptr && width > 0 && height > 0;
		}
	};

	class BallCardArtResources final
	{
	public:
		/// <summary>
		/// ボールカードの背景・枠・アイコンの画像を初回だけ読み込む。
		/// </summary>
		void Load()
		{
			if (m_LoadAttempted)
			{
				return;
			}
			m_LoadAttempted = true;

			LoadTexture("assets/UI/Cards/card_base.png", m_Base);
			LoadTexture("assets/UI/Cards/frame_generic.png", m_Frames[CategoryIndex(BallCategory::Standard)]);
			LoadTexture("assets/UI/Cards/frame_heavy.png", m_Frames[CategoryIndex(BallCategory::Heavy)]);
			LoadTexture("assets/UI/Cards/frame_pierce.png", m_Frames[CategoryIndex(BallCategory::Pierce)]);
			LoadTexture("assets/UI/Cards/frame_reflect.png", m_Frames[CategoryIndex(BallCategory::Bounce)]);
			LoadTexture("assets/UI/Cards/frame_anchor.png", m_Frames[CategoryIndex(BallCategory::Anchor)]);
			LoadTexture("assets/UI/Cards/card_heavy.png", m_Icons[CategoryIndex(BallCategory::Heavy)]);
			LoadTexture("assets/UI/Cards/card_pierce.png", m_Icons[CategoryIndex(BallCategory::Pierce)]);
			LoadTexture("assets/UI/Cards/card_reflect.png", m_Icons[CategoryIndex(BallCategory::Bounce)]);
			LoadTexture("assets/UI/Cards/card_anchor_green.png", m_Icons[CategoryIndex(BallCategory::Anchor)]);
		}

		/// <summary>
		/// カードの画像リソースと読み込み済みフラグを初期状態に戻す。
		/// </summary>
		void Reset()
		{
			m_Base = {};
			m_Frames = {};
			m_Icons = {};
			m_LoadAttempted = false;
		}

		/// <summary>
		/// 有効なカード背景テクスチャへのポインターを返す。
		/// </summary>
		const BallCardTexture* Base() const noexcept
		{
			return m_Base.IsValid() ? &m_Base : nullptr;
		}

		/// <summary>
		/// ボール種別に対応する有効なカード枠テクスチャへのポインターを返す。
		/// </summary>
		const BallCardTexture* Frame(BallCategory category) const noexcept
		{
			const BallCardTexture& texture = m_Frames[CategoryIndex(category)];
			return texture.IsValid() ? &texture : nullptr;
		}

		/// <summary>
		/// ボール種別に対応する有効なアイコンテクスチャへのポインターを返す。
		/// </summary>
		const BallCardTexture* Icon(BallCategory category) const noexcept
		{
			const BallCardTexture& texture = m_Icons[CategoryIndex(category)];
			return texture.IsValid() ? &texture : nullptr;
		}

	private:
		/// <summary>
		/// ボール種別を枠とアイコンの配列インデックスに変換する。
		/// </summary>
		static constexpr std::size_t CategoryIndex(BallCategory category) noexcept
		{
			switch (category)
			{
			case BallCategory::Heavy: return 1;
			case BallCategory::Pierce: return 2;
			case BallCategory::Bounce: return 3;
			case BallCategory::Anchor: return 4;
			default: return 0;
			}
		}

		/// <summary>
		/// カード画像を読み込み、描画に使うDirect3Dのテクスチャビューを作る。
		/// </summary>
		static bool LoadTexture(const char* path, BallCardTexture& destination)
		{
			int channels = 0;
			unsigned char* pixels = stbi_load(
				path,
				&destination.width,
				&destination.height,
				&channels,
				STBI_rgb_alpha);
			if (pixels == nullptr)
			{
				std::cerr << "Ball card texture load failed: " << path << '\n';
				destination = {};
				return false;
			}

			// Trim only empty/flat outer rows and columns.  This removes exported
			// transparent padding and opaque black gutters without looking inside
			// the artwork, so frame ornaments and antialiased edges stay intact.
			{
				const bool frameTexture =
					std::string(path).find("frame_") != std::string::npos;
				int left = destination.width;
				int top = destination.height;
				int right = -1;
				int bottom = -1;
				for (int y = 0; y < destination.height; ++y)
				{
					for (int x = 0; x < destination.width; ++x)
					{
						const std::size_t offset =
							(static_cast<std::size_t>(y) * destination.width + x) * 4u;
						const bool visible = pixels[offset + 3u] >= 32u;
						const bool nearBlack = pixels[offset] <= 3u &&
							pixels[offset + 1u] <= 3u && pixels[offset + 2u] <= 3u;
						if (!visible || (!frameTexture && nearBlack))
							continue;
						left = (std::min)(left, x);
						top = (std::min)(top, y);
						right = (std::max)(right, x);
						bottom = (std::max)(bottom, y);
					}
				}
				if (right >= left && bottom >= top)
				{
					// 縁のアンチエイリアスを切り落とさないように2px残す。
					left = (std::max)(0, left - 2);
					top = (std::max)(0, top - 2);
					right = (std::min)(destination.width - 1, right + 2);
					bottom = (std::min)(destination.height - 1, bottom + 2);
					destination.uvMin = ImVec2(
						static_cast<float>(left) / destination.width,
						static_cast<float>(top) / destination.height);
					destination.uvMax = ImVec2(
						static_cast<float>(right + 1) / destination.width,
						static_cast<float>(bottom + 1) / destination.height);
				}
			}

			ID3D11Device* device = Renderer::GetDevice();
			if (device == nullptr)
			{
				stbi_image_free(pixels);
				destination = {};
				return false;
			}

			D3D11_TEXTURE2D_DESC description{};
			description.Width = static_cast<UINT>(destination.width);
			description.Height = static_cast<UINT>(destination.height);
			description.MipLevels = 1;
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_IMMUTABLE;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			D3D11_SUBRESOURCE_DATA initialData{};
			initialData.pSysMem = pixels;
			initialData.SysMemPitch = description.Width * 4;

			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			HRESULT result = device->CreateTexture2D(
				&description,
				&initialData,
				texture.GetAddressOf());
			if (SUCCEEDED(result))
			{
				result = device->CreateShaderResourceView(
					texture.Get(),
					nullptr,
					destination.view.GetAddressOf());
			}
			stbi_image_free(pixels);
			if (FAILED(result))
			{
				std::cerr << "Ball card texture GPU upload failed: " << path << '\n';
				destination = {};
				return false;
			}
			return true;
		}

		bool m_LoadAttempted = false;
		BallCardTexture m_Base;
		std::array<BallCardTexture, 5> m_Frames{};
		std::array<BallCardTexture, 5> m_Icons{};
	};

	/// <summary>
	/// ボールカードの画像を保有する共有リソースを返す。
	/// </summary>
	BallCardArtResources& GetBallCardArtResources()
	{
		static BallCardArtResources resources;
		return resources;
	}

	/// <summary>
	/// Direct3DのテクスチャビューをImGuiで使う参照に変換する。
	/// </summary>
	ImTextureRef MakeTextureReference(const BallCardTexture& texture)
	{
		return ImTextureRef(static_cast<ImTextureID>(
			reinterpret_cast<std::uintptr_t>(texture.view.Get())));
	}

	/// <summary>
	/// 画像の縦横比を保ち、指定範囲を埋めるように画像を切り抜いて描画する。
	/// </summary>
	void DrawTextureCover(
		ImDrawList* drawList,
		const BallCardTexture& texture,
		const ImVec2& minimum,
		const ImVec2& maximum,
		ImU32 tint = IM_COL32_WHITE)
	{
		if (drawList == nullptr || !texture.IsValid())
			return;

		const float targetWidth = (std::max)(1.0f, maximum.x - minimum.x);
		const float targetHeight = (std::max)(1.0f, maximum.y - minimum.y);
		const float uvWidth = texture.uvMax.x - texture.uvMin.x;
		const float uvHeight = texture.uvMax.y - texture.uvMin.y;
		const float sampleWidth = (std::max)(1.0f, texture.width * uvWidth);
		const float sampleHeight = (std::max)(1.0f, texture.height * uvHeight);
		const float sourceAspect = sampleWidth / sampleHeight;
		const float targetAspect = targetWidth / targetHeight;
		ImVec2 uvMinimum = texture.uvMin;
		ImVec2 uvMaximum = texture.uvMax;

		if (targetAspect > sourceAspect)
		{
			const float visibleHeight = sourceAspect / targetAspect;
			const float cropped = uvHeight * (1.0f - visibleHeight) * 0.5f;
			uvMinimum.y += cropped;
			uvMaximum.y -= cropped;
		}
		else
		{
			const float visibleWidth = targetAspect / sourceAspect;
			const float cropped = uvWidth * (1.0f - visibleWidth) * 0.5f;
			uvMinimum.x += cropped;
			uvMaximum.x -= cropped;
		}

		drawList->AddImage(
			MakeTextureReference(texture),
			minimum,
			maximum,
			uvMinimum,
			uvMaximum,
			tint);
	}

	/// <summary>
	/// 画像の縦横比を保ち、指定範囲の中央に画像全体を収めて描画する。
	/// </summary>
	void DrawTextureContain(
		ImDrawList* drawList,
		const BallCardTexture& texture,
		const ImVec2& minimum,
		const ImVec2& maximum,
		ImU32 tint = IM_COL32_WHITE)
	{
		if (drawList == nullptr || !texture.IsValid())
		{
			return;
		}

		const float areaWidth = (std::max)(1.0f, maximum.x - minimum.x);
		const float areaHeight = (std::max)(1.0f, maximum.y - minimum.y);
		const float sampleWidth = (std::max)(1.0f,
			texture.width * (texture.uvMax.x - texture.uvMin.x));
		const float sampleHeight = (std::max)(1.0f,
			texture.height * (texture.uvMax.y - texture.uvMin.y));
		const float scale = (std::min)(
			areaWidth / sampleWidth,
			areaHeight / sampleHeight);
		const ImVec2 drawnSize(
			sampleWidth * scale,
			sampleHeight * scale);
		const ImVec2 offset(
			(areaWidth - drawnSize.x) * 0.5f,
			(areaHeight - drawnSize.y) * 0.5f);
		drawList->AddImage(
			MakeTextureReference(texture),
			ImVec2(minimum.x + offset.x, minimum.y + offset.y),
			ImVec2(minimum.x + offset.x + drawnSize.x, minimum.y + offset.y + drawnSize.y),
			texture.uvMin,
			texture.uvMax,
			tint);
	}

	struct BallCardLayout
	{
		static constexpr float AspectWidth = 3.0f;
		static constexpr float AspectHeight = 4.0f;
		static constexpr float NameLeft = 0.12f;
		static constexpr float NameTop = 0.066f;
		static constexpr float NameRight = 0.88f;
		static constexpr float NameBottom = 0.160f;
		static constexpr float RoleLeft = 0.265f;
		static constexpr float RoleTop = 0.170f;
		static constexpr float RoleRight = 0.735f;
		static constexpr float RoleBottom = 0.280f;
		static constexpr float DescriptionLeft = 0.09f;
		static constexpr float DescriptionTop = 0.535f;
		static constexpr float DescriptionRight = 0.91f;
	};
}

/// <summary>
/// 共有のカード画像リソースを読み込む。
/// </summary>
void LoadResources() { GetBallCardArtResources().Load(); }

/// <summary>
/// 共有のカード画像リソースを解放する。
/// </summary>
void ResetResources() { GetBallCardArtResources().Reset(); }

/// <summary>
/// ボール種別のアイコンを表示し、必要に応じてグレースケール画像を使う。
/// </summary>
void DrawBallIcon(const PlayerBallData& ball, const ImVec2& position, const ImVec2& size, bool grayscale)
{
    if (grayscale && ball.category != BallCategory::Standard)
    {
        const char* path = ball.category == BallCategory::Heavy ? "assets/UI/Cards/card_heavy.png" :
            ball.category == BallCategory::Pierce ? "assets/UI/Cards/card_pierce.png" :
            ball.category == BallCategory::Bounce ? "assets/UI/Cards/card_reflect.png" : "assets/UI/Cards/card_anchor_green.png";
        IllustratedUi::Image(ImGui::GetWindowDrawList(), path, position,
            ImVec2(position.x + size.x, position.y + size.y), IM_COL32_WHITE, true, true);
        return;
    }
    auto& art = GetBallCardArtResources();
    art.Load();
    auto* draw = ImGui::GetWindowDrawList();
    if (const auto* icon = art.Icon(ball.category))
        DrawTextureContain(draw, *icon, position, ImVec2(position.x + size.x, position.y + size.y));
    else
    {
        auto color = PlayerBallText::GetColor(ball);
        if (grayscale) color = {0.6f, 0.6f, 0.6f};
        const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
        const float radius = (std::min)(size.x, size.y) * 0.43f;
        draw->AddCircleFilled(center, radius + 3, grayscale ? IM_COL32(35, 35, 35, 255) : IM_COL32(40, 26, 47, 255));
        draw->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1)));
        draw->AddCircle(center, radius, grayscale ? IM_COL32(220, 220, 220, 255) : IM_COL32(255, 233, 170, 255), 0, 2);
        draw->AddCircleFilled(ImVec2(center.x - radius * 0.3f, center.y - radius * 0.35f),
            radius * 0.23f, IM_COL32(255, 255, 255, 220));
    }
}

	/// <summary>
	/// ボール選択カードに能力やステータスを表示し、選択や詳細表示の操作を返す。
	/// </summary>
	BallCardInteraction DrawBallSelectionCard(
		const PlayerBallData& ball,
		int index,
		int attack,
		const ImVec2& position,
		const ImVec2& size,
		bool selected,
		bool held)
	{
		BallCardInteraction interaction;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		BallCardArtResources& art = GetBallCardArtResources();
		art.Load();
		const auto ballColor = PlayerBallText::GetColor(ball);
		const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
			ImVec4(ballColor[0], ballColor[1], ballColor[2], 1.0f));
		const ImU32 borderColor = selected
			? IM_COL32(255, 221, 118, 255)
			: (held ? IM_COL32(91, 222, 205, 255) : IM_COL32(92, 105, 126, 210));
		const ImVec2 cardMax(position.x + size.x, position.y + size.y);
		const float layoutScale = size.x / 252.0f;
		const float readableScale = (std::max)(0.78f, layoutScale);
		const float cornerRadius = 12.0f * layoutScale;
		const float footerHeight = 30.0f * readableScale;

		drawList->AddRectFilled(
			ImVec2(position.x + 5.0f, position.y + 7.0f),
			ImVec2(cardMax.x + 5.0f, cardMax.y + 7.0f),
			IM_COL32(0, 0, 0, 142),
			cornerRadius);
		drawList->PushClipRect(position, cardMax, true);
		if (const BallCardTexture* base = art.Base())
		{
			// 2:3背景はUVを中央クロップし、円形模様を歪ませない。
			DrawTextureCover(drawList, *base, position, cardMax);
		}
		else
		{
			drawList->AddRectFilled(
				position,
				cardMax,
				IM_COL32(10, 31, 34, 246),
				cornerRadius);
		}

		if (const BallCardTexture* icon = art.Icon(ball.category))
		{
			DrawTextureContain(
				drawList,
				*icon,
				ImVec2(
					position.x + size.x * 0.18f,
					position.y + size.y * 0.18f),
				ImVec2(
					cardMax.x - size.x * 0.18f,
					position.y + size.y * 0.53f));
		}

		if (const BallCardTexture* frame = art.Frame(ball.category))
		{
			// Map the complete trimmed frame to the canonical 3:4 card.  A cover
			// crop here used to cut category-specific corner decorations.
			drawList->AddImage(
				MakeTextureReference(*frame), position, cardMax,
				frame->uvMin, frame->uvMax, IM_COL32_WHITE);
		}
		else
		{
			drawList->AddRect(position, cardMax, accent, cornerRadius, 0, 3.0f);
		}

		if (selected || held)
		{
			drawList->AddRect(
				ImVec2(position.x + 3.0f, position.y + 3.0f),
				ImVec2(cardMax.x - 3.0f, cardMax.y - 3.0f),
				borderColor,
				cornerRadius,
				0,
				selected ? 3.0f : 5.0f);
		}
		drawList->PopClipRect();

		ImGui::PushID(index);
		ImGui::SetCursorScreenPos(position);
		ImGui::InvisibleButton(
			"ball_card_select",
			ImVec2(size.x, size.y));
		const bool cardHovered = ImGui::IsItemHovered();
		interaction.select = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		interaction.openDetails = cardHovered &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Right);

		const ImVec2 footerPosition(position.x, cardMax.y - footerHeight);
		if (cardHovered)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			if (!selected)
			{
				drawList->AddRect(
					ImVec2(position.x + 2.0f, position.y + 2.0f),
					ImVec2(cardMax.x - 2.0f, cardMax.y - 2.0f),
					held ? IM_COL32(126, 255, 236, 255) : IM_COL32(224, 239, 255, 245),
					cornerRadius,
					0,
					held ? 3.0f : 2.0f);
			}
		}
		if (selected)
		{
			drawList->AddRect(
				ImVec2(position.x + 1.0f, position.y + 1.0f),
				ImVec2(cardMax.x - 1.0f, cardMax.y - 1.0f),
				IM_COL32(255, 231, 145, 255),
				cornerRadius,
				0,
				4.0f);
		}

		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize();
		char text[160]{};

		const auto addText = [drawList, font](
			float textSize,
			const ImVec2& textPosition,
			ImU32 color,
			const char* value,
			float wrapWidth = 0.0f)
		{
			if (value == nullptr)
			{
				return;
			}
			drawList->AddText(
				font,
				textSize,
				ImVec2(textPosition.x + 1.0f, textPosition.y + 1.0f),
				IM_COL32(0, 0, 0, 210),
				value,
				nullptr,
				wrapWidth);
			drawList->AddText(
				font,
				textSize,
				textPosition,
				color,
				value,
				nullptr,
				wrapWidth);
		};
		const auto addCenteredTextInRegion = [&](
			float requestedTextSize,
			const ImVec2& regionMinimum,
			const ImVec2& regionMaximum,
			float y,
			ImU32 color,
			const char* value)
		{
			const float availableWidth =
				(std::max)(1.0f, regionMaximum.x - regionMinimum.x);
			ImVec2 measured = font->CalcTextSizeA(
				requestedTextSize,
				(std::numeric_limits<float>::max)(),
				0.0f,
				value);
			float textSize = requestedTextSize;
			if (measured.x > availableWidth)
			{
				textSize = requestedTextSize * availableWidth / measured.x;
				measured = font->CalcTextSizeA(
					textSize,
					(std::numeric_limits<float>::max)(),
					0.0f,
					value);
			}
			addText(
				textSize,
				ImVec2(
					regionMinimum.x + (availableWidth - measured.x) * 0.5f,
					y),
				color,
				value);
		};
		const auto addWrappedTextFitted = [&addText, font](
			float requestedTextSize,
			float minimumTextSize,
			const ImVec2& regionMinimum,
			const ImVec2& regionMaximum,
			ImU32 color,
			const std::string& value)
		{
			const float availableWidth = (std::max)(
				1.0f,
				regionMaximum.x - regionMinimum.x);
			const float availableHeight = (std::max)(
				1.0f,
				regionMaximum.y - regionMinimum.y);
			float textSize = requestedTextSize;
			while (textSize > minimumTextSize)
			{
				const ImVec2 measured = font->CalcTextSizeA(
					textSize,
					(std::numeric_limits<float>::max)(),
					availableWidth,
					value.c_str());
				if (measured.y <= availableHeight)
				{
					break;
				}
				textSize = (std::max)(minimumTextSize, textSize - 0.5f);
			}
			addText(
				textSize,
				regionMinimum,
				color,
				value.c_str(),
				availableWidth);
		};

		drawList->PushClipRect(position, cardMax, true);

		// 大きな上部プレートにはボール名だけ、下の小さなプレートには主な役割だけ。
		// 座標をカードの縦横比3:4に対する比率で定義し、全軸共通にする。
		const ImVec2 nameMinimum(
			position.x + size.x * BallCardLayout::NameLeft,
			position.y + size.y * BallCardLayout::NameTop);
		const ImVec2 nameMaximum(
			position.x + size.x * BallCardLayout::NameRight,
			position.y + size.y * BallCardLayout::NameBottom);
		drawList->PushClipRect(nameMinimum, nameMaximum, true);
		addCenteredTextInRegion(
			fontSize * 1.02f * readableScale,
			nameMinimum,
			nameMaximum,
			position.y + size.y * 0.088f,
			IM_COL32(255, 255, 255, 255),
			PlayerBallText::GetName(ball.definitionId));
		drawList->PopClipRect();

		// 役割が「[利用] [決着]」のように複数あっても、小枠には先頭だけ出す。
		// 詳細画面側では従来どおりすべての役割を見られる。
		std::string primaryRole = PlayerBallText::GetRoles(ball.definitionId);
		const std::size_t roleEnd = primaryRole.find(']');
		if (roleEnd != std::string::npos)
			primaryRole.resize(roleEnd + 1);

		const ImVec2 roleMinimum(
			position.x + size.x * BallCardLayout::RoleLeft,
			position.y + size.y * BallCardLayout::RoleTop);
		const ImVec2 roleMaximum(
			position.x + size.x * BallCardLayout::RoleRight,
			position.y + size.y * BallCardLayout::RoleBottom);
		drawList->PushClipRect(roleMinimum, roleMaximum, true);
		addCenteredTextInRegion(
			fontSize * 0.82f * readableScale,
			roleMinimum,
			roleMaximum,
			position.y + size.y * 0.209f,
			IM_COL32(235, 246, 255, 255),
			primaryRole.c_str());
		drawList->PopClipRect();

		const ImVec2 descriptionMinimum(
			position.x + size.x * BallCardLayout::DescriptionLeft,
			position.y + size.y * BallCardLayout::DescriptionTop);
		const ImVec2 descriptionMaximum(
			position.x + size.x * BallCardLayout::DescriptionRight,
			footerPosition.y - 3.0f * layoutScale);
		const float descriptionPadding = size.x * 0.025f;
		const float descriptionTextLeft =
			descriptionMinimum.x + descriptionPadding;
		const float descriptionTextRight =
			descriptionMaximum.x - descriptionPadding;
		drawList->AddRectFilled(
			descriptionMinimum,
			descriptionMaximum,
			IM_COL32(2, 11, 15, 168),
			7.0f * layoutScale);
		drawList->PushClipRect(descriptionMinimum, descriptionMaximum, true);

		const float attackTextY = descriptionMinimum.y + descriptionPadding;
		sprintf_s(text, PlayerBallText::Utf8(u8"攻撃 %d  /  追突 %d"), attack, ball.status.impactAttack);
		addText(
			fontSize * 0.76f * readableScale,
			ImVec2(descriptionTextLeft, attackTextY),
			IM_COL32(247, 229, 163, 255),
			text);
		const float dividerY = attackTextY + fontSize * 0.86f * readableScale;
		drawList->AddLine(
			ImVec2(descriptionTextLeft, dividerY),
			ImVec2(descriptionTextRight, dividerY),
			IM_COL32(92, 111, 121, 190),
			1.0f * layoutScale);
		const std::string shortDescription =
			PlayerBallText::GetShortDescription(ball);
		addWrappedTextFitted(
			fontSize * 0.76f * readableScale,
			fontSize * 0.54f * readableScale,
			ImVec2(descriptionTextLeft, dividerY + 4.0f * readableScale),
			ImVec2(descriptionTextRight, descriptionMaximum.y - descriptionPadding),
			IM_COL32(216, 228, 235, 255),
			shortDescription);
		drawList->PopClipRect();

		const ImU32 footerBackground = selected
			? IM_COL32(184, 137, 38, 235)
			: (held
				? IM_COL32(23, 122, 116, 245)
				: (cardHovered
					? IM_COL32(58, 74, 96, 245)
					: IM_COL32(12, 31, 39, 232)));
		drawList->AddRectFilled(
			footerPosition,
			cardMax,
			footerBackground,
			cornerRadius,
			ImDrawFlags_RoundCornersBottom);
		const char* footerText = PlayerBallText::Utf8(u8"左:決定 / 右:詳細");
		const float footerFontSize = fontSize * 0.78f * readableScale;
		const ImVec2 footerTextSize = font->CalcTextSizeA(
			footerFontSize,
			(std::numeric_limits<float>::max)(),
			0.0f,
			footerText);
		addText(
			footerFontSize,
			ImVec2(
				position.x + (size.x - footerTextSize.x) * 0.5f,
				footerPosition.y + (footerHeight - footerTextSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 255),
			footerText);

		drawList->PopClipRect();
		ImGui::PopID();
		return interaction;
	}
	/// <summary>
	/// 用語を強調表示し、マウスを重ねたときに説明を表示する。
	/// </summary>
	void DrawTermTooltip(const char* term, const char* explanation)
	{
		// 水色＋下線：ホバーできる文字であることを常時示す。
		ImGui::TextColored(ImVec4(0.28f, 0.91f, 1.0f, 1.0f), "%s  [?]", term);
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(minimum.x, maximum.y - 1.0f),
			ImVec2(maximum.x, maximum.y - 1.0f),
			IM_COL32(75, 221, 255, 215),
			1.3f);
		if (!ImGui::IsItemHovered()) return;
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
		ImGui::TextColored(ImVec4(0.28f, 0.91f, 1.0f, 1.0f), "%s", term);
		ImGui::Separator();
		ImGui::TextWrapped("%s", explanation);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	/// <summary>
	/// ボール固有の能力、現在の数値、次回強化の情報を詳細ポップアップに表示する。
	/// </summary>
	void DrawBallDetailPopup(const PlayerBallData& ball, int effectiveAttack)
	{
		const BallStatus& status = ball.status;
		const std::string& id = ball.definitionId;
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();

		// 既存の戦闘中カード描画をそのまま使い、同じ縦横比3:4で拡大表示する。
		// 下部の「閉じる」ボタンのために約52px残す。
		float cardHeight = (std::min)(400.0f, (std::max)(300.0f, available.y - 52.0f));
		float cardWidth = cardHeight * (3.0f / 4.0f);
		const float gap = 16.0f;
		if (available.x - cardWidth - gap < 170.0f)
		{
			cardWidth = (std::max)(160.0f, available.x - gap - 170.0f);
			cardHeight = cardWidth * (4.0f / 3.0f);
		}
		const float termPanelWidth = (std::max)(170.0f, available.x - cardWidth - gap);

		// 画像素材は従来のDrawBallSelectionCardがそのまま描画する。
		// 詳細画面内のクリックは選択へ反映しない。
		(void)DrawBallSelectionCard(
			ball, -10000, effectiveAttack, start,
			ImVec2(cardWidth, cardHeight), false, false);

		// 元のカードの右側に用語パネルを出す。
		ImGui::SetCursorScreenPos(ImVec2(start.x + cardWidth + gap, start.y));
		if (ImGui::BeginChild("##detail_card_terms",
			ImVec2(termPanelWidth, cardHeight), ImGuiChildFlags_Borders))
		{
			ImGui::TextUnformatted("専門用語・仕様");
			ImGui::TextDisabled("水色の文字をマウスオーバー");
			ImGui::Separator();

			int termCount = 0;
			auto term = [&](const char* label, const char* explanation)
				{
					DrawTermTooltip(label, explanation);
					++termCount;
				};

			// 共通の表示用語
			term("追突力", "このショット中に敵球同士が衝突したとき、両方の敵に与える基本ダメージ。追加効果と敵の軽減は別途適用します。");
			term("シールド", "このターン中だけHPの代わりにダメージを受ける追加耐久。敵の攻撃後、ターン終了時に消えます。");
			term("攻撃力", "敵への通常攻撃で使用する基本攻撃力。強化や一部の追加効果で増加します。");

			// 重量軸
			if (ball.category == BallCategory::Heavy)
			{
				term("重量スタック", "敵の移動距離や軽量化球の接触などによって獲得する共有資源。連鎖衝撃や衝突強化に使用します。獲得にはショット単位の上限があります。");
			}
			if (id == "player_heavy")
				term("重量による押し出し", "質量と押し出し倍率を利用して敵を動かします。重量球は敵の移動距離から重量スタックを生成します。");
			if (id == "player_chain_impact")
				term("衝撃波", "命中した敵以外の周囲の敵に追加ダメージ。最も近い敵を最大2体対象にします。最初の発動では重量スタックを最大5消費し、1個ごとに衝撃波ダメージが1増えます。");
			if (id == "player_collision_boost")
				term("衝突強化", "次のショット中の敵同士の衝突ダメージを強化します。重量スタックを3消費できる場合は強い倍率を使い、不足時は弱い1.10倍の効果を使います。");
			if (id == "player_lightweight")
				term("軽量化", "当てた敵の次のノックバックを強める効果。敵への接触により重量スタックも少量獲得します。");

			// 貫通軸
			if (ball.category == BallCategory::Pierce)
				term("貫通スタック", "異なる敵への貫通で獲得する共有資源。ミニオン召喚時に消費します。所持上限は15です。");
			if (id == "player_pierce" || id == "player_refractive_pierce" ||
				id == "player_pierce_finisher" || id == "player_minion_finisher")
				term("貫通", "敵に当たっても通常衝突のように押し返さず、球が通り抜けます。貫通回数と貫通後の速度維持率に制限があります。");
			if (id == "player_pierce" || id == "player_refractive_pierce" ||
				id == "player_minion_finisher")
				term("貫通痕", "貫通した位置と方向を記録した軌跡です。最大3本保持し、ミニオンを発射する位置・方向として使います。");
			if (id == "player_refractive_pierce")
				term("屈折", "貫通後に、本来の衝突なら敵が飛んでいく方向へ、自球の進行方向を変化させる効果です。");
			if (id == "player_pierce_finisher")
				term("貫通決着", "そのショットで異なる敵を貫通した数に応じ、以降の命中時の追加ダメージを増加させます。");
			if (id == "player_minion_finisher")
				term("ミニオン", "過去の貫通痕からショット開始時に発射する攻撃球。1体につき基本3スタックまで攻撃力・ノックバックが強化されます。");
			if (id == "player_minion_finisher")
				term("過充填", "10～15スタック消費でミニオンをさらに強化します。実際に存在する貫通痕の本数に応じて、効果がない余分な消費は防止します。");

			// 反射軸
			if (ball.category == BallCategory::Bounce)
				term("反射スタック", "有効な壁反射などで蓄積する共有資源。バウンド球の強化反射や連続跳弾球に使用します。");
			if (id == "player_bounce" || id == "player_cushion_charge" ||
				id == "player_reflection_setup")
				term("有効反射", "壁に当たって進行方向が変わった反射。蓄積球による生成や、セットアップ効果の発動条件になります。");
			if (id == "player_ricochet_finisher")
				term("ピンボール", "反射スタック6を消費すると最初に当てた敵を対象にする特殊状態。敵の衝突回数に応じて追加ダメージ段階が進みます。");
			if (id == "player_reflection_setup")
				term("セットアップ", "このボールのショット終了時に準備し、次のショットの最初の有効な壁反射で反射スタックを追加します。");

			// アンカー軸
			if (ball.category == BallCategory::Anchor)
				term("Enemy Anchor", "敵ごとに蓄積するアンカースタック。シールドへの変換や錨回収球の追加ダメージに使用します。");
			if (id == "player_anchor" || id == "player_anchor_finisher" ||
				id == "player_stop_shield")
				term("Player Anchor", "敵のアンカースタックを消費すると、5個分ごとに1個得られるプレイヤー側の資源。アンカーの付与量や回収ダメージを強化します。");
			if (id == "player_stop_shield")
				term("シールド", "接触した敵のEnemy Anchorを消費して獲得します。次の敵攻撃によるダメージを吸収します。");
			if (id == "player_anchor_finisher")
				term("錨回収", "接触した敵のEnemy Anchorをすべて消費し、直接ダメージに変換。一定個数以上消費すると周囲にも範囲攻撃が発生します。");
			if (id == "player_origin_anchor")
				term("始点復帰", "ショット開始時の位置を記憶し、次のターン開始時に元の場所へ戻る能力です。");

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextUnformatted("現在の数値");
			ImGui::Text("攻撃力：%d", effectiveAttack);
			ImGui::Text("質量：%.1f", status.mass);
			if (id == "player_chain_impact")
				ImGui::Text("衝撃波範囲：%.0f", status.chainImpactRadius);
			if (id == "player_minion_finisher")
				ImGui::TextUnformatted("消費上限：15（貫通痕に応じ制限）");
			if (id == "player_collision_boost")
				ImGui::Text("衝突倍率：x%.2f", status.heavyEnemyCollisionMultiplier);
			if (id == "player_ricochet_finisher")
				ImGui::Text("ダメージ段階：%d衝突ごと / 速度維持：%d%%",
					status.pinballCollisionsPerStage,
					static_cast<int>(std::lround(status.pinballSpeedRetention * 100.0f)));
			if (id == "player_stop_shield")
				ImGui::Text("錨1個：%d / シールド上限：%d",
					status.stopShieldPerStack, status.stopShieldCapacity);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextUnformatted("次回強化");
			const std::string preview = PlayerBallText::GetUpgradePreview(ball);
			ImGui::TextWrapped("%s", preview.c_str());
		}
		ImGui::EndChild();

		// 呼び出し側の「閉じる」ボタンをカードの下へ配置する。
		ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + cardHeight + 12.0f));
	}
}
