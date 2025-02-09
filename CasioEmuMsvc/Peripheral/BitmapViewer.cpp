#include "Localization.h"
#include "Ui.hpp"
#include <cmath> // for std::trunc, std::fmod, round

class BitmapViewer : public UIWindow {
public:
	BitmapViewer() : UIWindow("Bitmap") {}

	// 用于输入内存地址的字符缓冲区（以十六进制形式输入）
	char bufaddr[10] = {};
	// 每行显示的像素数，默认 16（可通过滑块调整）
	int width = 16;
	// 每个像素的显示尺寸（像素大小），默认 10
	int size = 10;
	// 位偏移（bit offset），范围 0～7，决定从字节内哪一位开始读取
	int bitOffset = 0;

	// 用于累计鼠标滚轮的变化
	double wheeldelta = 0;

	void RenderCore() override {
		// —— 1. 上部控制区域：地址输入及各个滑块
		auto startY = ImGui::GetCursorPosY();
		ImGui::InputText("BitmapViewer.Address"_lc, bufaddr, sizeof(bufaddr));
		// 从输入框获取起始地址（十六进制）
		uint32_t addr = static_cast<uint32_t>(strtol(bufaddr, nullptr, 16));
		addr = addr & 0xfffff;
		if (ImGui::InputInt("BitmapViewer.Address_2"_lc, (int*)&addr)) {
			sprintf(bufaddr, "%08X", addr);
		}
		ImGui::SliderInt("BitmapViewer.Width"_lc, &width, 1, 256);
		ImGui::SliderInt("BitmapViewer.PixelSize"_lc, &size, 1, 256);
		ImGui::SliderInt("BitmapViewer.BitOffset"_lc, &bitOffset, 0, 7);
		ImGui::Dummy(ImVec2{0, 20});

		// 可用区域高度决定显示多少行
		float availHeight = ImGui::GetContentRegionAvail().y;
		int rows = static_cast<int>(availHeight) / size;
		int numPixels = width * rows;

		// 网格区域尺寸
		ImVec2 gridSize(width * size, rows * size);
		ImGui::Dummy(gridSize);
		// 记录 Dummy 对应的区域（后续绘制均基于此区域）
		ImVec2 startPos = ImGui::GetItemRectMin();
		// 捕获网格区域是否被鼠标悬停（用于区分与滚动条交互）
		bool gridHovered = ImGui::IsItemHovered();

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// —— 绘制位图数据：依次读取每个位对应的数据
		uint8_t currentByte = 0;
		int currentByteIndex = -1;
		for (int i = 0; i < numPixels; ++i) {
			int effectiveBitPos = bitOffset + i;
			int byteIndex = effectiveBitPos / 8;
			int bitIndex = 7 - (effectiveBitPos % 8);

			int col = i % width;
			int row = i / width;
			ImVec2 pixelTopLeft = ImVec2(startPos.x + col * size, startPos.y + row * size);
			ImVec2 pixelBottomRight = ImVec2(pixelTopLeft.x + size, pixelTopLeft.y + size);

			if (byteIndex != currentByteIndex) {
				currentByte = me_mmu->ReadData((addr + byteIndex) & 0xfffff);
				currentByteIndex = byteIndex;
			}
			bool isSet = ((currentByte >> bitIndex) & 1) != 0;
			if (isSet)
				drawList->AddRectFilled(pixelTopLeft, pixelBottomRight, IM_COL32(255, 255, 255, 255));
		}
		ImVec2 endPos = ImVec2(startPos.x + gridSize.x, startPos.y + gridSize.y);
		if (size > 3) {
			// 绘制网格外框
			drawList->AddRect(startPos, endPos, IM_COL32(200, 200, 200, 255));

			// —— 绘制列刻度
			for (int col = 0; col < width; ++col) {
				float x = startPos.x + col * size;
				const int tickLength = 5;
				if (col % 8 == 0) {
					drawList->AddLine(ImVec2(x, startPos.y),
						ImVec2(x, startPos.y - tickLength),
						IM_COL32(255, 255, 255, 255));
					char colLabel[16];
					sprintf(colLabel, "%X", col);
					drawList->AddText(ImVec2(x + 1, startPos.y - tickLength - 12),
						IM_COL32(255, 255, 255, 255), colLabel);
				}
			}
			// —— 绘制行刻度
			for (int row = 0; row < rows; ++row) {
				float y = startPos.y + row * size;
				const int tickLength = 5;
				if (row % 8 == 0) {
					drawList->AddLine(ImVec2(endPos.x, y),
						ImVec2(endPos.x + tickLength, y),
						IM_COL32(255, 255, 255, 255));
					char rowLabel[16];
					sprintf(rowLabel, "%06X", (row + addr) & 0xfffff);
					drawList->AddText(ImVec2(endPos.x + tickLength, y - 6),
						IM_COL32(255, 255, 255, 255), rowLabel);
				}
			}
		}
		// —— 2. 添加“虚拟滚动条”用于快速跳转（此滚动条并非窗口的实际滚动条）
		{
			// 示例中设定内存范围为 1MB（可根据需要调整）
			constexpr uint32_t MEMORY_SIZE = 0x100000;
			// 可见区域包含的位数及字节数（向上取整）
			int visibleBits = width * rows;
			int visibleBytes = (bitOffset + visibleBits + 7) / 8;
			uint32_t maxEffectiveAddr = (MEMORY_SIZE > (uint32_t)visibleBytes) ? MEMORY_SIZE - visibleBytes : 0;

			// 以浮点数表示的“有效地址”，其小数部分表示 bitOffset 的比例
			float effectiveAddr = addr + bitOffset / 8.0f;

			// 定义滚动条的几何区域：放在网格右侧，间隔 10 像素
			float scrollBarWidth = 16.f;
			ImVec2 scrollBarPos = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - scrollBarWidth, ImGui::GetWindowPos().y + startY);
			ImVec2 scrollBarSize = ImVec2(scrollBarWidth - 1.0f, ImGui::GetWindowSize().y - startY - 10.0f);

			// 绘制滚动条背景
			drawList->AddRectFilled(scrollBarPos,
				ImVec2(scrollBarPos.x + scrollBarWidth, scrollBarPos.y + scrollBarSize.y),
				IM_COL32(50, 50, 50, 255));

			// 计算句柄（滑块）的高度：与可见字节数占内存范围的比例有关
			float handleHeight = (visibleBytes / (float)MEMORY_SIZE) * scrollBarSize.y;
			if (handleHeight < 10.0f)
				handleHeight = 10.0f; // 最小高度

			// 计算句柄在滚动条中的垂直位置
			float handlePosY = 0.0f;
			if (maxEffectiveAddr > 0)
				handlePosY = (effectiveAddr / maxEffectiveAddr) * (scrollBarSize.y - handleHeight);

			ImVec2 handleMin = ImVec2(scrollBarPos.x, scrollBarPos.y + handlePosY);
			ImVec2 handleMax = ImVec2(scrollBarPos.x + scrollBarWidth, scrollBarPos.y + handlePosY + handleHeight);
			drawList->AddRectFilled(handleMin, handleMax, IM_COL32(150, 150, 150, 255));

			// 用 InvisibleButton 捕获鼠标在该区域内的点击与拖拽事件
			ImGui::SetCursorScreenPos(scrollBarPos);
			ImGui::InvisibleButton("VirtualScrollbar", scrollBarSize);
			bool scrollbarHovered = ImGui::IsItemHovered();
			if (ImGui::IsItemActive()) {
				float mouseY = ImGui::GetIO().MousePos.y;
				float newHandlePosY = mouseY - scrollBarPos.y - handleHeight / 2;
				// 限制 newHandlePosY 在合理范围内
				if (newHandlePosY < 0.0f)
					newHandlePosY = 0.0f;
				if (newHandlePosY > scrollBarSize.y - handleHeight)
					newHandlePosY = scrollBarSize.y - handleHeight;
				float newEffectiveAddr = (scrollBarSize.y - handleHeight > 0)
											 ? (newHandlePosY / (scrollBarSize.y - handleHeight)) * maxEffectiveAddr
											 : 0.0f;
				// 根据 newEffectiveAddr 还原出新的 addr 与 bitOffset（bitOffset 为 newEffectiveAddr 的小数部分乘 8）
				int newAddr = (int)newEffectiveAddr;
				int newBitOffset = (int)round((newEffectiveAddr - newAddr) * 8.0f);
				if (newBitOffset >= 8) {
					newAddr++;
					newBitOffset -= 8;
				}
				addr = newAddr;
				bitOffset = newBitOffset;
				sprintf(bufaddr, "%08X", addr);
			}
		}

		// —— 3. 鼠标滚轮滚动：仅当鼠标悬停在网格区域（且不在滚动条上时）响应
		if (gridHovered && !ImGui::IsItemActive()) {
			wheeldelta += ImGui::GetIO().MouseWheel;
			if (std::abs(wheeldelta) >= 1.0f) {
				int rowDelta = static_cast<int>(std::trunc(wheeldelta));
				wheeldelta = std::fmod(wheeldelta, 1.0f);
				int bitDelta = width * rowDelta;
				int byteDelta = bitDelta / 8;
				int bitRemainder = bitDelta % 8;

				int newAddr = addr + byteDelta;
				int newBitOffset = bitOffset + bitRemainder;
				// 如果 bitOffset 超出上界，进位
				if (newBitOffset > 7) {
					int carry = newBitOffset / 8;
					newAddr += carry;
					newBitOffset %= 8;
				}
				// 如果 bitOffset 小于 0，则向高位借位
				else if (newBitOffset < 0) {
					int borrow = (-newBitOffset + 7) / 8;
					newAddr = newAddr > borrow ? newAddr - borrow : 0;
					newBitOffset += borrow * 8;
				}
				addr = newAddr;
				bitOffset = newBitOffset;
				sprintf(bufaddr, "%08X", addr);
			}
		}
		// —— 4. 添加键盘滚动处理（上下箭头，PageUp，PageDown）
		// 仅当窗口拥有焦点且未捕获文本输入时响应
		if (ImGui::IsWindowFocused()) {
			int rowDelta = 0;
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				rowDelta = -1;
			else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				rowDelta = 1;
			else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
				rowDelta = -rows;
			else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
				rowDelta = rows;

			if (rowDelta != 0) {
				int bitDelta = width * rowDelta;
				int byteDelta = bitDelta / 8;
				int bitRemainder = bitDelta % 8;

				int newAddr = addr + byteDelta;
				int newBitOffset = bitOffset + bitRemainder;
				if (newBitOffset > 7) {
					int carry = newBitOffset / 8;
					newAddr += carry;
					newBitOffset %= 8;
				}
				else if (newBitOffset < 0) {
					int borrow = (-newBitOffset + 7) / 8;
					newAddr = newAddr > borrow ? newAddr - borrow : 0;
					newBitOffset += borrow * 8;
				}
				addr = newAddr;
				bitOffset = newBitOffset;
				sprintf(bufaddr, "%08X", addr);
			}
		}
	}
};

UIWindow* CreateBitmapViewer() {
	return new BitmapViewer();
}
