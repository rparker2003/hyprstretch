#include "stretch/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace Stretch
{
  std::expected<Rectangle, std::string> horizontalRegion(std::span<const Display> displays_, bool sameHeight_)
  {
    if (displays_.empty())
    {
      return std::unexpected("No monitors were selected");
    }
    std::vector<const Display*> row;
    std::set<std::string> names;
    for (const auto& display : displays_)
    {
      if (!names.insert(display.name).second)
      {
        return std::unexpected("Repeated monitor: " + display.name);
      }
      if (!std::isfinite(display.x) || !std::isfinite(display.y) || !std::isfinite(display.width) || !std::isfinite(display.height) ||
          !std::isfinite(display.scale) || display.width <= 0 || display.height <= 0 || display.scale <= 0)
      {
        return std::unexpected("Invalid geometry for monitor: " + display.name);
      }
      row.push_back(&display);
    }
    std::ranges::sort(row, [](const Display* left_, const Display* right_)
    {
      return left_->x < right_->x;
    });
    const auto& first = *row.front();
    double edge = first.x;
    double height = first.height;
    for (const auto* display : row)
    {
      if (std::abs(display->x - edge) > 0.01 || std::abs(display->y - first.y) > 0.01)
      {
        return std::unexpected("Monitors must form a horizontal row without gaps or overlaps");
      }
      if (sameHeight_ && (std::abs(display->height - first.height) > 0.01 || std::abs(display->scale - first.scale) > 0.001))
      {
        return std::unexpected("Selected monitors must have equal logical heights and scales");
      }
      edge += display->width;
      height = std::max(height, display->height);
    }
    return Rectangle{first.x, first.y, edge - first.x, height};
  }
}
