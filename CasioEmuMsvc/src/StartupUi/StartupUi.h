#pragma once

#include "ModelResourceStore.h"

#include <memory>
#include <string>

struct StartupSelection {
	std::string model_path;
	std::shared_ptr<casioemu::ModelResourceStore> resources;
};

StartupSelection sui_loop();
