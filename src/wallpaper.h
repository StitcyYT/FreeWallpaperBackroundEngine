#pragma once
#include "platform.h"

namespace wallpaper {

bool init(const Platform& platform);
void setSize(const Platform& platform, int width, int height);

}
