#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace Stretch
{
  struct Display
  {
    std::string name;
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    double scale = 1;
  };

  struct Rectangle
  {
    double x;
    double y;
    double width;
    double height;
  };

  std::expected<Rectangle, std::string> horizontalRegion(std::span<const Display> displays_, bool sameHeight_ = true);
}
