#include "stretch/Layout.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
  void require(bool condition_, const char* description_)
  {
    if (!condition_)
    {
      throw std::runtime_error(description_);
    }
    std::cout << "PASS " << description_ << '\n';
  }
}

int main()
{
  using Stretch::Display;
  std::vector<Display> displays{{"right", 1280, 0, 1280, 720, 1}, {"left", -1280, 0, 1280, 720, 1}, {"center", 0, 0, 1280, 720, 1}};
  auto region = Stretch::horizontalRegion(displays);
  require(region && region->x == -1280 && region->width == 3840 && region->height == 720, "three monitors with negative origin and unordered input");
  displays[0].x = 1400;
  require(!Stretch::horizontalRegion(displays), "gap rejected");
  displays[0].x = 1200;
  require(!Stretch::horizontalRegion(displays), "overlap rejected");
  displays[0].x = 1280;
  displays[0].y = 100;
  require(!Stretch::horizontalRegion(displays), "misaligned top edges rejected");
  displays[0].y = 0;
  displays[0].scale = 2;
  require(!Stretch::horizontalRegion(displays), "mixed scales rejected for stretching");
  displays[0].scale = 1;
  displays[0].height = 1080;
  require(!Stretch::horizontalRegion(displays), "mixed heights rejected for stretching");
  region = Stretch::horizontalRegion(displays, false);
  require(region && region->height == 1080, "coordinate ordering accepts an adjacent taller display");
  displays[0].height = 720;
  displays[0].name = "left";
  require(!Stretch::horizontalRegion(displays), "duplicate monitor names rejected");
  displays[0].name = "right";
  displays[0].width = std::numeric_limits<double>::quiet_NaN();
  require(!Stretch::horizontalRegion(displays), "non-finite geometry rejected");
  require(!Stretch::horizontalRegion({}), "empty selection rejected");
}
