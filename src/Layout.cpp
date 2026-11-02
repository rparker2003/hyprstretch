#include "stretch/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace Stretch
{
  namespace
  {
    // NOTE: Allow up to 2 logical pixels of tolerance so fractional scaling (e.g. 1.25x, 1.33x, 1.5x)
    // and integer monitor position rounding do not falsely reject contiguous horizontal rows.
    constexpr double geometryEpsilon = 2.0;
  }

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
      if (std::abs(display->x - edge) > geometryEpsilon || std::abs(display->y - first.y) > geometryEpsilon)
      {
        return std::unexpected("Monitors must form a horizontal row without gaps or overlaps");
      }
      if (sameHeight_ && std::abs(display->height - first.height) > geometryEpsilon)
      {
        return std::unexpected("Selected monitors must have equal logical heights");
      }
      edge = display->x + display->width;
      height = std::max(height, display->height);
    }
    return Rectangle{first.x, first.y, edge - first.x, height};
  }
}
