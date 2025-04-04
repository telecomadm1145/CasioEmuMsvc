#include "Spi.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <vector>
#include <filesystem>

// SD Card commands
enum class SdCommand : uint8_t {
	CMD0 = 0,	 // GO_IDLE_STATE
	CMD8 = 8,	 // SEND_IF_COND
	CMD9 = 9,	 // SEND_CSD
	CMD10 = 10,	 // SEND_CID
	CMD16 = 16,	 // SET_BLOCKLEN
	CMD17 = 17,	 // READ_SINGLE_BLOCK
	CMD24 = 24,	 // WRITE_BLOCK
	CMD55 = 55,	 // APP_CMD
	CMD58 = 58,	 // READ_OCR
	ACMD41 = 41, // SD_SEND_OP_COND
};

class FakeSdCard {
public:
	ISpiProvider* spi;

	FakeSdCard(ISpiProvider* spi) : spi(spi), state(State::WaitingCommand) {
		if (!std::filesystem::exists("sdcard.img")) {
			imageData.resize(0);
			std::cout << "[FakeSdCard][Warn] No sdcard.img, disabling...\n";
		}
		else {
			spi->SetRecvHandler([this](uint8_t d) { OnRead(d); });
			LoadImage("sdcard.img");
			std::cout << "[FakeSdCard][Info] Loaded sdcard.img.\n";
		}
	}

private:
	enum class State {
		WaitingCommand,
		ReadingCommand,
		SendingResponse,
		ReadingData,
		WritingData,
		SendingData
	};

	// Card status
	State state;
	std::vector<uint8_t> imageData;
	std::array<uint8_t, 6> commandBuffer;
	uint8_t commandIndex = 0;
	uint32_t currentBlock = 0;
	std::vector<uint8_t> blockBuffer;
	size_t dataCounter = 0;
	bool isAppCmd = false;

	// R1 response bits
	static constexpr uint8_t R1_IDLE = 0x01;
	static constexpr uint8_t R1_ILLEGAL_CMD = 0x04;

	void LoadImage(const std::string& filename) {
		std::ifstream file(filename, std::ios::binary);
		if (file) {
			file.seekg(0, std::ios::end);
			size_t size = file.tellg();
			file.seekg(0, std::ios::beg);
			imageData.resize(size);
			file.read(reinterpret_cast<char*>(imageData.data()), size);
		}
		else {
			// Create empty 1MB image if file doesn't exist
			imageData.resize(1024 * 1024, 0xFF);
		}
	}

	void SaveImage(const std::string& filename) {
		std::ofstream file(filename, std::ios::binary);
		if (file) {
			file.write(reinterpret_cast<const char*>(imageData.data()), imageData.size());
		}
	}

	void OnRead(uint8_t data) {
		switch (state) {
		case State::WaitingCommand:
			if (data == 0xFF) {
				return; // Ignore dummy bytes
			}
			if ((data & 0xC0) == 0x40) { // Command start bit
				state = State::ReadingCommand;
				commandBuffer[0] = data;
				commandIndex = 1;
			}
			break;

		case State::ReadingCommand:
			commandBuffer[commandIndex++] = data;
			if (commandIndex == 6) {
				ProcessCommand();
				state = State::WaitingCommand;
			}
			break;

		case State::ReadingData:
			blockBuffer.push_back(data);
			if (blockBuffer.size() == 512) {
				// Write block to image
				if (currentBlock * 512 + 512 <= imageData.size()) {
					std::copy(blockBuffer.begin(), blockBuffer.end(),
						imageData.begin() + currentBlock * 512);
					SaveImage("sdcard.img");
				}
				// Send success response
				spi->Send(0x05); // Data accepted
				state = State::WaitingCommand;
			}
			break;

		default:
			break;
		}
	}

	void ProcessCommand() {
		SdCommand cmd = static_cast<SdCommand>(commandBuffer[0] & 0x3F);
		uint32_t argument = (static_cast<uint32_t>(commandBuffer[1]) << 24) |
							(static_cast<uint32_t>(commandBuffer[2]) << 16) |
							(static_cast<uint32_t>(commandBuffer[3]) << 8) |
							static_cast<uint32_t>(commandBuffer[4]);

		if (isAppCmd) {
			ProcessAppCommand(cmd, argument);
			isAppCmd = false;
			return;
		}
		// std::cout << "CMD" << (int)cmd << "\n";
		switch (cmd) {
		case SdCommand::CMD0:
			spi->Send(R1_IDLE); // Enter idle state
			break;

		case SdCommand::CMD8:
			// Send interface condition response (R7)
			spi->Send(R1_IDLE);
			spi->Send(0x00);
			spi->Send(0x00);
			spi->Send(0x01); // Voltage accepted
			spi->Send(0xAA); // Echo back pattern
			break;

		case SdCommand::CMD9: // SEND_CSD
			SendCSD();
			break;

		case SdCommand::CMD17: // READ_SINGLE_BLOCK
			currentBlock = argument;
			if (currentBlock * 512 >= imageData.size()) {
				spi->Send(R1_ILLEGAL_CMD);
				break;
			}
			spi->Send(0x00); // Response
			spi->Send(0xFE); // Start block token
			// Send block data
			for (size_t i = 0; i < 512; i++) {
				spi->Send(imageData[currentBlock * 512 + i]);
			}
			// Send CRC (dummy)
			spi->Send(0xFF);
			spi->Send(0xFF);
			break;

		case SdCommand::CMD24: // WRITE_BLOCK
			currentBlock = argument;
			if (currentBlock * 512 >= imageData.size()) {
				spi->Send(R1_ILLEGAL_CMD);
				break;
			}
			spi->Send(0x00); // Response
			blockBuffer.clear();
			state = State::ReadingData;
			break;

		case SdCommand::CMD55:
			isAppCmd = true;
			spi->Send(R1_IDLE);
			break;

		case SdCommand::CMD58: // READ_OCR
			spi->Send(R1_IDLE);
			spi->Send(0x40); // OCR register (3.3V)
			spi->Send(0x00);
			spi->Send(0x00);
			spi->Send(0x00);
			break;

		default:
			spi->Send(R1_ILLEGAL_CMD);
			break;
		}
	}

	void ProcessAppCommand(SdCommand cmd, uint32_t argument) {
		switch (cmd) {
		case SdCommand::ACMD41:
			spi->Send(0x00); // Not in idle state anymore
			break;
		default:
			spi->Send(R1_ILLEGAL_CMD);
			break;
		}
	}

	void SendCSD() {
		spi->Send(0x00); // Response
		spi->Send(0xFE); // Start block token

		// CSD structure version 2.0
		spi->Send(0x40); // CSD_STRUCTURE [127:120]
		spi->Send(0x0E); // Reserved [119:112]
		spi->Send(0x00); // TAAC [111:104]
		spi->Send(0x32); // NSAC [103:96]
		spi->Send(0x5A); // TRAN_SPEED [95:88]
		spi->Send(0x5B); // CCC [87:80]
		spi->Send(0x59); // READ_BL_LEN etc [79:72]

		// Calculate size information
		uint32_t sizeKB = imageData.size() / 1024;
		uint32_t cSize = (sizeKB / 512) - 1; // In 512KB units

		spi->Send((cSize >> 16) & 0x3F); // C_SIZE [71:64]
		spi->Send((cSize >> 8) & 0xFF);	 // C_SIZE [63:56]
		spi->Send(cSize & 0xFF);		 // C_SIZE [55:48]

		// Reserved and format specific bits
		spi->Send(0x00);
		spi->Send(0x00);
		spi->Send(0x00);
		spi->Send(0x00);
		spi->Send(0x00);

		// CRC (dummy)
		spi->Send(0xFF);
		spi->Send(0xFF);
	}
};