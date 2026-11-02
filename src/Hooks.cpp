#include "stretch/Engine.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/fullscreen/handler/FullscreenHandler.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/state/MonitorPositionController.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace Stretch::Hooks
{
  namespace
  {
    enum class Site : size_t
    {
      Query,
      Window,
      Modes,
      Position,
      Maintain,
      Paint,
      Focus,
      Workspace,
      Arrangement,
      Count
    };

    std::array<CFunctionHook*, static_cast<size_t>(Site::Count)> detours{};
    Engine* engine = nullptr;

    class RendererEntry : public Render::IHyprRenderer
    {
    public:
      static auto draw()
      {
        return &RendererEntry::renderWindow;
      }
    };

    template<class Function>
    Function previous(Site site_)
    {
      return reinterpret_cast<Function>(detours.at(static_cast<size_t>(site_))->m_original);
    }

    bool query(Fullscreen::CFullscreenController* controller_, PHLWORKSPACE workspace_, std::optional<bool> covering_)
    {
      return (covering_.value_or(true) && engine->occupant(workspace_)) || previous<decltype(&query)>(Site::Query)(controller_, workspace_, covering_);
    }

    PHLWINDOW window(Fullscreen::CFullscreenController* controller_, PHLWORKSPACE workspace_, std::optional<bool> covering_)
    {
      if (covering_.value_or(true))
      {
        if (const auto occupant = engine->occupant(workspace_))
        {
          return occupant;
        }
      }
      return previous<decltype(&window)>(Site::Window)(controller_, workspace_, covering_);
    }

    Fullscreen::SFullscreenMode modes(Fullscreen::CFullscreenController* controller_, PHLWORKSPACE workspace_, std::optional<bool> covering_)
    {
      if (covering_.value_or(true) && engine->occupant(workspace_))
      {
        return {.internal = Fullscreen::FSMODE_FULLSCREEN, .client = Fullscreen::FSMODE_NONE};
      }
      return previous<decltype(&modes)>(Site::Modes)(controller_, workspace_, covering_);
    }

    void position(Fullscreen::IFullscreenHandler* handler_, SP<Layout::ITarget> target_)
    {
      const auto rectangle = engine->frame(target_ ? target_->window() : nullptr);
      if (!rectangle)
      {
        previous<decltype(&position)>(Site::Position)(handler_, target_);
        return;
      }
      Fullscreen::controller()->m_windowPosSettingQueued = true;
      target_->setPositionGlobal(*rectangle);
    }

    void maintain(Fullscreen::IFullscreenHandler* handler_)
    {
      const auto target = handler_->getFullscreen();
      const auto owner = target ? target->window() : nullptr;
      const auto rectangle = engine->frame(owner);
      if (!rectangle)
      {
        previous<decltype(&maintain)>(Site::Maintain)(handler_);
        return;
      }
      const auto desired = rectangle->copy().round();
      if (owner->position(Desktop::View::IGeometric::GEOMETRIC_GOAL) != desired.pos() || owner->size(Desktop::View::IGeometric::GEOMETRIC_GOAL) != desired.size())
      {
        Fullscreen::controller()->m_windowPosSettingQueued = true;
        target->setPositionGlobal(*rectangle);
      }
    }

    void paint(Render::IHyprRenderer* renderer_, Render::CRenderContext& context_, PHLMONITOR monitor_, PHLWORKSPACE workspace_,
               const Time::steady_tp& time_, Render::eSceneMode scene_)
    {
      previous<decltype(&paint)>(Site::Paint)(renderer_, context_, monitor_, workspace_, time_, scene_);
      if (scene_ == Render::eSceneMode::MONITOR)
      {
        if (const auto occupant = engine->occupant(workspace_))
        {
          // NOTE: Workspace-only captures must retain their own contents rather than a different workspace's stretched window.
          (renderer_->*RendererEntry::draw())(context_, occupant, monitor_, occupant->presentation().renderPresentation(scene_), time_, false, Render::RENDER_PASS_ALL, false, false);
        }
      }
    }

    void focus(Desktop::CFocusState* state_, PHLWINDOW window_, Desktop::eFocusReason reason_, SP<CWLSurfaceResource> surface_, bool cycle_)
    {
      if (window_ && !window_->isFloating())
      {
        engine->release(window_->m_workspace);
      }
      previous<decltype(&focus)>(Site::Focus)(state_, window_, reason_, surface_, cycle_);
    }

    void workspace(Monitor::CMonitor* monitor_, const PHLWORKSPACE& destination_, bool internal_, bool keepPointer_, bool keepFocus_)
    {
      PHLWINDOW released;
      if (!internal_ && destination_ && destination_->type() == Workspace::eWorkspaceType::NORMAL)
      {
        released = engine->release(monitor_->m_activeWorkspace);
      }
      previous<decltype(&workspace)>(Site::Workspace)(monitor_, destination_, internal_, keepPointer_, keepFocus_);
      if (released && !keepFocus_ && Desktop::focusState()->window() == released)
      {
        const auto next = destination_->getFocusCandidate();
        if (next)
        {
          Desktop::focusState()->fullWindowFocus(next, Desktop::FOCUS_REASON_WORKSPACE_CHANGE);
        }
        else
        {
          Desktop::focusState()->rawWindowFocus(nullptr, Desktop::FOCUS_REASON_WORKSPACE_CHANGE);
        }
      }
    }

    void arrangement(const State::CMonitorPositionController* controller_, std::span<const SP<Monitor::IMonitorArrangeable>> monitors_, bool zeroScaling_)
    {
      previous<decltype(&arrangement)>(Site::Arrangement)(controller_, monitors_, zeroScaling_);
      if (zeroScaling_ || monitors_.empty())
      {
        return;
      }
      // NOTE: Translate XWayland monitor positions by the bounding-box minimum so any desktop layout
      // (including negative coordinates or vertically stacked auxiliary monitors) preserves its exact
      // relative arrangement in non-negative X11 space.
      double minX = monitors_.front()->position().x;
      double minY = monitors_.front()->position().y;
      for (const auto& monitor : monitors_)
      {
        minX = std::min(minX, monitor->position().x);
        minY = std::min(minY, monitor->position().y);
      }
      for (const auto& monitor : monitors_)
      {
        monitor->setXWaylandPosition(Vector2D{monitor->position().x - minX, monitor->position().y - minY});
      }
    }

    struct Definition
    {
      Site _site;
      const char* _name;
      const char* _signature;
      const char* _overload;
      const void* _replacement;
    };
  }

  void install(HANDLE handle_, Engine& engine_)
  {
    engine = &engine_;
    const std::array definitions{
      Definition{Site::Query, "hasFullscreen", "CFullscreenController::hasFullscreen(", "Workspace::CHLWorkspace", reinterpret_cast<const void*>(&query)},
      Definition{Site::Window, "getFullscreenWindow", "CFullscreenController::getFullscreenWindow(", "Workspace::CHLWorkspace", reinterpret_cast<const void*>(&window)},
      Definition{Site::Modes, "getFullscreenModes", "CFullscreenController::getFullscreenModes(", "Workspace::CHLWorkspace", reinterpret_cast<const void*>(&modes)},
      Definition{Site::Position, "setTargetSizeAndPosition", "IFullscreenHandler::setTargetSizeAndPosition(", "", reinterpret_cast<const void*>(&position)},
      Definition{Site::Maintain, "syncTargetSizeAndPosition", "IFullscreenHandler::syncTargetSizeAndPosition(", "", reinterpret_cast<const void*>(&maintain)},
      Definition{Site::Paint, "renderWorkspaceWindowsFullscreen", "IHyprRenderer::renderWorkspaceWindowsFullscreen(", "Render::eSceneMode", reinterpret_cast<const void*>(&paint)},
      Definition{Site::Focus, "fullWindowFocus", "CFocusState::fullWindowFocus(", "", reinterpret_cast<const void*>(&focus)},
      Definition{Site::Workspace, "changeWorkspace", "CMonitor::changeWorkspace(", "Workspace::CHLWorkspace", reinterpret_cast<const void*>(&workspace)},
      Definition{Site::Arrangement, "arrange", "CMonitorPositionController::arrange(", "", reinterpret_cast<const void*>(&arrangement)}
    };
    std::array<void*, definitions.size()> addresses{};
    for (size_t index = 0; index < definitions.size(); ++index)
    {
      const auto& definition = definitions[index];
      size_t count = 0;
      for (const auto& symbol : HyprlandAPI::findFunctionsByName(handle_, definition._name))
      {
        if (symbol.demangled.contains(definition._signature) && symbol.demangled.contains(definition._overload))
        {
          addresses[index] = symbol.address;
          ++count;
        }
      }
      if (count != 1)
      {
        throw std::runtime_error(std::string("Unsupported Hyprland hook signature: ") + definition._signature);
      }
    }
    for (size_t index = 0; index < definitions.size(); ++index)
    {
      const auto& definition = definitions[index];
      auto*& detour = detours.at(static_cast<size_t>(definition._site));
      detour = HyprlandAPI::createFunctionHook(handle_, addresses[index], definition._replacement);
      if (!detour || !detour->hook())
      {
        throw std::runtime_error(std::string("Could not attach Hyprland hook: ") + definition._name);
      }
    }
  }

  void remove(HANDLE handle_)
  {
    for (auto it = detours.rbegin(); it != detours.rend(); ++it)
    {
      if (*it)
      {
        if (!HyprlandAPI::removeFunctionHook(handle_, *it))
        {
          HyprlandAPI::addNotification(handle_, "Hyprstretch hook cleanup failed; restart Hyprland", CHyprColor{1.0, 0.2, 0.2, 1.0}, 10000);
        }
        *it = nullptr;
      }
    }
    engine = nullptr;
  }
}
