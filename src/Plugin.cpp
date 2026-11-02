#include "stretch/Engine.hpp"

#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
  std::unique_ptr<Stretch::Engine> engine;
}

APICALL EXPORT std::string PLUGIN_API_VERSION()
{
  return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle_)
{
  if (std::string_view{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash())
  {
    throw std::runtime_error("Hyprstretch must be rebuilt for the running Hyprland version");
  }
  auto candidate = std::make_unique<Stretch::Engine>(handle_);
  candidate->start();
  engine = std::move(candidate);
  return {"hyprstretch", "Manual stretch and restore across selected monitors", "Ryan Parker (AI-assisted development)", "0.2.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
  engine.reset();
}
