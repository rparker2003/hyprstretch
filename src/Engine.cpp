#include "stretch/Engine.hpp"
#include "stretch/Layout.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/LayerState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowFullscreenPolicy.hpp>
#include <hyprland/src/desktop/view/window/WindowGroupMembership.hpp>
#include <hyprland/src/desktop/view/window/WindowMetadata.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorLayoutController.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <charconv>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Stretch
{
  using namespace Fullscreen;

  Engine::Engine(HANDLE handle_) : _handle(handle_)
  {
  }

  Engine::~Engine()
  {
    _subscriptions.clear();
    if (_timer)
    {
      wl_event_source_remove(_timer);
    }
    while (!_sessions.empty())
    {
      restore(_sessions.begin()->first);
    }
    if (_command)
    {
      HyprlandAPI::unregisterHyprCtlCommand(_handle, _command);
    }
    for (const auto& name : _luaFunctions)
    {
      HyprlandAPI::removeLuaFunction(_handle, "hyprstretch", name);
    }
    _luaOwner = nullptr;
    Hooks::remove(_handle);
    if (_started)
    {
      State::monitorLayoutController()->arrange();
    }
  }

  void Engine::start()
  {
    Hooks::install(_handle, *this);
    _started = true;
    auto& events = Event::bus()->m_events;
    _subscriptions.push_back(events.window.active.listen([this](PHLWINDOW window_, Desktop::eFocusReason)
    {
      reclaim(window_);
    }));
    _subscriptions.push_back(events.window.close.listen([this](PHLWINDOW window_)
    {
      if (window_ && _sessions.erase(window_->metadata().stableID()))
      {
        rebuildLeases();
      }
    }));
    _subscriptions.push_back(events.window.fullscreen.listen([this](PHLWINDOW window_)
    {
      if (!window_)
      {
        return;
      }
      const auto id = window_->metadata().stableID();
      if (!_transitions.contains(id) && controller()->getFullscreenModes(window_).internal != FSMODE_FULLSCREEN)
      {
        _sessions.erase(id);
      }
      rebuildLeases();
    }));
    _subscriptions.push_back(events.window.open.listen([this](PHLWINDOW window_)
    {
      if (window_ && !window_->isFloating())
      {
        release(window_->m_workspace);
      }
    }));
    _subscriptions.push_back(events.window.openLate.listen([this](PHLWINDOW)
    {
      rebuildLeases();
    }));
    _subscriptions.push_back(events.window.moveToWorkspace.listen([this](PHLWINDOW, PHLWORKSPACE)
    {
      scheduleUpdate();
    }));
    _subscriptions.push_back(events.window.pin.listen([this](PHLWINDOW)
    {
      scheduleUpdate();
    }));
    _subscriptions.push_back(events.window.floating.listen([this](PHLWINDOW)
    {
      scheduleUpdate();
    }));
    _subscriptions.push_back(events.workspace.active.listen([this](PHLWORKSPACE)
    {
      rebuildLeases();
    }));
    _subscriptions.push_back(events.workspace.moveToMonitor.listen([this](PHLWORKSPACE, PHLMONITOR)
    {
      scheduleUpdate();
    }));
    _subscriptions.push_back(events.monitor.removed.listen([this](PHLMONITOR)
    {
      scheduleUpdate();
    }));
    _subscriptions.push_back(events.monitor.layoutChanged.listen([this]
    {
      scheduleUpdate();
    }));
    _subscriptions.push_back(events.layer.opened.listen([this](PHLLS)
    {
      rebuildLeases();
    }));
    _command = HyprlandAPI::registerHyprCtlCommand(_handle, IPC::Socket1::SCommand{
      .name = "hyprstretch",
      .match = IPC::Socket1::COMMAND_MATCH_PREFIX,
      .handler = [this](const auto& request_) { return command(request_); }
    });
    if (!_command)
    {
      throw std::runtime_error("Could not register the hyprstretch command");
    }
    // NOTE: Register plugin:hyprstretch:outputs so users can optionally configure default monitor names in hyprland.conf or Lua.
    _defaultOutputs = Config::Values::makeConfigValue<Config::Values::String>(
      "plugin:hyprstretch:outputs",
      "Optional space-separated default monitor names for hyprstretch",
      "");
    if (!HyprlandAPI::addConfigValueV2(_handle, _defaultOutputs))
    {
      throw std::runtime_error("Could not register config value plugin:hyprstretch:outputs");
    }
    _luaOwner = this;
    const std::pair<const char*, PLUGIN_LUA_FN> functions[]{{"toggle", luaToggle}, {"stretch", luaStretch}, {"restore", luaRestore}};
    for (const auto& [name, function] : functions)
    {
      if (!HyprlandAPI::addLuaFunction(_handle, "hyprstretch", name, function))
      {
        throw std::runtime_error(std::string("Could not register Lua function hyprstretch.") + name);
      }
      _luaFunctions.push_back(name);
    }
    State::monitorLayoutController()->arrange();
  }

  std::optional<std::string> Engine::resolve(Session& session_) const
  {
    const auto window = session_._window.lock();
    if (!window || !window->mapped() || !window->m_workspace || window->m_workspace->type() != Workspace::eWorkspaceType::NORMAL ||
        (window->m_state & Desktop::View::WINDOW_STATE_PINNED) || window->grouping().group())
    {
      return "Use an unpinned, ungrouped window on a regular workspace";
    }
    if (session_._outputs.empty())
    {
      return "Select at least one monitor";
    }
    std::vector<Display> displays;
    std::vector<PHLMONITORREF> selected;
    bool containsWindow = false;
    std::optional<Vector2D> translation;
    // NOTE: Hoisted outside the loop so we only query monitorState() once per resolution.
    const auto& monitors = State::monitorState()->monitors();
    for (const auto& name : session_._outputs)
    {
      const auto found = std::ranges::find_if(monitors, [&](const auto& monitor_)
      {
        return monitor_->m_name == name && monitor_->m_enabled;
      });
      if (found == monitors.end())
      {
        return "Monitor is not connected: " + name;
      }
      const auto& monitor = *found;
      containsWindow |= monitor == window->m_monitor.lock();
      displays.push_back({name, monitor->m_position.x, monitor->m_position.y, monitor->m_size.x, monitor->m_size.y, monitor->m_scale});
      selected.emplace_back(monitor);
      if (window->backend().isX11())
      {
        const auto difference = monitor->m_position - monitor->m_xwaylandPosition;
        if (monitor->m_xwaylandScale != 1.F || (translation && (difference - *translation).size() > 2.0))
        {
          return "XWayland coordinates do not match the selected desktop row; disable force_zero_scaling";
        }
        translation = difference;
      }
    }
    const auto rectangle = horizontalRegion(displays);
    if (!rectangle)
    {
      return rectangle.error();
    }
    if (!containsWindow)
    {
      return "Include the monitor containing this application";
    }
    session_._frame = CBox{rectangle->x, rectangle->y, rectangle->width, rectangle->height};
    session_._monitors = std::move(selected);
    return std::nullopt;
  }

  PHLWINDOW Engine::target(const std::string& selector_) const
  {
    if (selector_.empty())
    {
      return Desktop::focusState()->window();
    }
    std::string_view address = selector_;
    if (address.starts_with("address:"))
    {
      address.remove_prefix(8);
    }
    if (address.starts_with("0x"))
    {
      address.remove_prefix(2);
    }
    uintptr_t value = 0;
    const auto result = std::from_chars(address.data(), address.data() + address.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != address.data() + address.size())
    {
      return nullptr;
    }
    for (const auto& window : Desktop::windowState()->windows())
    {
      if (reinterpret_cast<uintptr_t>(window.get()) == value && window->mapped())
      {
        return window;
      }
    }
    return nullptr;
  }

  std::string Engine::restoreWindow(const std::string& target_)
  {
    const auto window = target(target_);
    if (!window)
    {
      return "No mapped application matches the target";
    }
    restore(window->metadata().stableID());
    return {};
  }

  std::string Engine::toggle(const std::vector<std::string>& outputs_, const std::string& target_)
  {
    const auto window = target(target_);
    if (!window)
    {
      return "No mapped application matches the target";
    }
    if (_sessions.contains(window->metadata().stableID()))
    {
      return restoreWindow(target_);
    }
    return stretch(outputs_, target_);
  }

  std::string Engine::stretch(const std::vector<std::string>& outputs_, const std::string& target_)
  {
    const auto window = target(target_);
    if (!window || !window->mapped())
    {
      return "Focus an application first";
    }
    const auto id = window->metadata().stableID();
    const bool existingSession = _sessions.contains(id);
    std::optional<Session> previousSession;
    Session session;
    if (existingSession)
    {
      previousSession = _sessions.at(id);
      session = *previousSession;
      session._released.clear();
    }
    session._window = window;
    session._outputs = outputs_;
    if (session._outputs.empty() && _defaultOutputs)
    {
      // NOTE: Fall back to plugin:hyprstretch:outputs when configured before auto-discovering the horizontal row.
      std::istringstream configured(_defaultOutputs->value());
      std::string name;
      while (configured >> name)
      {
        session._outputs.push_back(name);
      }
    }
    if (session._outputs.empty())
    {
      const auto anchor = window->m_monitor.lock();
      if (!anchor)
      {
        return "Application has no monitor";
      }
      for (const auto& monitor : State::monitorState()->monitors())
      {
        // NOTE: Ignore disabled monitors and match logical top edge and height within 2px for fractional scaling.
        if (monitor->m_enabled && std::abs(monitor->m_position.y - anchor->m_position.y) <= 2.0 &&
            std::abs(monitor->m_size.y - anchor->m_size.y) <= 2.0)
        {
          session._outputs.push_back(monitor->m_name);
        }
      }
    }
    if (auto error = resolve(session))
    {
      return *error;
    }
    for (const auto& [otherId, other] : _sessions)
    {
      if (otherId == id)
      {
        continue;
      }
      for (const auto& name : session._outputs)
      {
        if (std::ranges::find(other._outputs, name) != other._outputs.end())
        {
          return "Another stretched application uses monitor " + name;
        }
      }
    }
    if (!existingSession)
    {
      session._savedFrame = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
      session._savedMode = controller()->getFullscreenModes(window);
      session._savedLayoutMode = controller()->layoutManagedFS(window);
      session._floating = window->isFloating();
    }
    _sessions.insert_or_assign(id, std::move(session));
    _transitions.insert(id);
    Hyprutils::Utils::CScopeGuard transition([&] { _transitions.erase(id); });
    // NOTE: TRANSFER keeps client fullscreen independent of the compositor's stretched presentation.
    controller()->setFullscreenMode(window, FSMODE_FULLSCREEN, _sessions.at(id)._savedMode.client, false, FULLSCREEN_MUTATION_TRANSFER);
    if (controller()->getFullscreenModes(window).internal != FSMODE_FULLSCREEN)
    {
      // NOTE: Roll back to the prior active session if updating an existing stretch failed, otherwise restore.
      if (previousSession)
      {
        _sessions.insert_or_assign(id, std::move(*previousSession));
        rebuildLeases();
      }
      else
      {
        restore(id);
      }
      return "Hyprland declined the fullscreen transition";
    }
    updateGeometry(window);
    rebuildLeases();
    return {};
  }

  void Engine::restore(uint64_t id_)
  {
    auto entry = _sessions.extract(id_);
    if (entry.empty())
    {
      return;
    }
    const auto& session = entry.mapped();
    const auto window = session._window.lock();
    rebuildLeases();
    if (!window || !window->mapped())
    {
      return;
    }
    _transitions.insert(id_);
    Hyprutils::Utils::CScopeGuard transition([&] { _transitions.erase(id_); });
    controller()->setFullscreenMode(window, session._savedMode.internal, session._savedMode.client, session._savedLayoutMode, FULLSCREEN_MUTATION_TRANSFER);
    if (session._floating && session._savedMode.internal == FSMODE_NONE && window->layoutTarget())
    {
      CBox restoredFrame = session._savedFrame;
      const auto center = restoredFrame.middle();
      const bool onEnabledMonitor = std::ranges::any_of(State::monitorState()->monitors(), [&](const auto& monitor_)
      {
        return monitor_->m_enabled &&
               center.x >= monitor_->m_position.x && center.x < monitor_->m_position.x + monitor_->m_size.x &&
               center.y >= monitor_->m_position.y && center.y < monitor_->m_position.y + monitor_->m_size.y;
      });
      if (!onEnabledMonitor)
      {
        if (const auto currentMonitor = window->m_monitor.lock())
        {
          // NOTE: Clamp floating restore geometry onto the surviving monitor when its original monitor was unplugged.
          restoredFrame.w = std::min(restoredFrame.w, currentMonitor->m_size.x);
          restoredFrame.h = std::min(restoredFrame.h, currentMonitor->m_size.y);
          restoredFrame.x = currentMonitor->m_position.x + (currentMonitor->m_size.x - restoredFrame.w) / 2.0;
          restoredFrame.y = currentMonitor->m_position.y + (currentMonitor->m_size.y - restoredFrame.h) / 2.0;
        }
      }
      window->layoutTarget()->setPositionGlobal(restoredFrame);
    }
    else if (window->m_workspace && window->m_workspace->space())
    {
      window->m_workspace->space()->recalculate();
    }
  }

  std::optional<CBox> Engine::frame(PHLWINDOW window_) const
  {
    // NOTE: Fast-path exit when no sessions are active to keep hot layout/fullscreen hooks zero-overhead.
    if (_sessions.empty() || !window_ || !window_->mapped())
    {
      return std::nullopt;
    }
    const auto entry = _sessions.find(window_->metadata().stableID());
    if (entry == _sessions.end() || controller()->getFullscreenModes(window_).internal != FSMODE_FULLSCREEN)
    {
      return std::nullopt;
    }
    return entry->second._frame;
  }

  PHLWINDOW Engine::occupant(PHLWORKSPACE workspace_) const
  {
    // NOTE: Fast-path exit when no secondary monitors are leased to keep hot render/query hooks zero-overhead.
    if (_leases.empty() || !workspace_ || workspace_->type() != Workspace::eWorkspaceType::NORMAL)
    {
      return nullptr;
    }
    const auto monitor = workspace_->m_monitor.lock();
    if (!monitor || monitor->m_activeWorkspace != workspace_)
    {
      return nullptr;
    }
    const auto lease = _leases.find(monitor->m_id);
    if (lease == _leases.end())
    {
      return nullptr;
    }
    const auto session = _sessions.find(lease->second);
    return session == _sessions.end() ? nullptr : session->second._window.lock();
  }

  void Engine::mask(PHLWORKSPACE workspace_, bool covered_)
  {
    if (!workspace_)
    {
      return;
    }
    const bool hide = covered_ || controller()->hasFullscreen(workspace_);
    for (const auto& window : Desktop::windowState()->windows())
    {
      if (window->m_workspace == workspace_ && !(window->m_state & Desktop::View::WINDOW_STATE_PINNED) && !controller()->isFullscreen(window))
      {
        window->fullscreenPolicy().setAllowedOverFullscreen(!hide);
        window->updateFullscreenInputState();
      }
    }
    const auto targetMonitor = workspace_->m_monitor.lock();
    if (hide)
    {
      for (const auto& layer : Desktop::layerState()->layers())
      {
        if (layer->m_monitor.lock() == targetMonitor && (layer->m_flags & Desktop::View::LAYER_FLAG_ABOVE_FULLSCREEN))
        {
          // NOTE: Track only layers that originally had LAYER_FLAG_ABOVE_FULLSCREEN so unmasking does not promote background/bottom layers.
          layer->m_flags &= ~Desktop::View::LAYER_FLAG_ABOVE_FULLSCREEN;
          if (std::ranges::find_if(_demotedLayers, [&](const auto& ref_) { return ref_.lock() == layer; }) == _demotedLayers.end())
          {
            _demotedLayers.emplace_back(layer);
          }
        }
      }
    }
    else
    {
      std::erase_if(_demotedLayers, [&](const auto& ref_)
      {
        const auto layer = ref_.lock();
        if (!layer)
        {
          return true;
        }
        if (layer->m_monitor.lock() == targetMonitor)
        {
          layer->m_flags |= Desktop::View::LAYER_FLAG_ABOVE_FULLSCREEN;
          return true;
        }
        return false;
      });
    }
    Animation::Workspace::setFullscreenFadeAnimation(workspace_, hide ? Animation::Workspace::ANIMATION_TYPE_IN : Animation::Workspace::ANIMATION_TYPE_OUT);
    if (!hide && workspace_->space())
    {
      workspace_->space()->recalculate();
    }
  }

  void Engine::rebuildLeases()
  {
    auto previousLeases = std::exchange(_leases, {});
    std::map<MONITORID, PHLWORKSPACEREF> next;
    for (const auto& [id, session] : _sessions)
    {
      const auto window = session._window.lock();
      if (!frame(window) || !window->m_workspace || !window->m_workspace->visible())
      {
        continue;
      }
      for (const auto& weak : session._monitors)
      {
        const auto monitor = weak.lock();
        if (monitor && monitor != window->m_monitor.lock() && monitor->m_activeWorkspace && !session._released.contains(monitor->m_id))
        {
          _leases.emplace(monitor->m_id, id);
          next.emplace(monitor->m_id, monitor->m_activeWorkspace);
        }
      }
    }
    auto previous = std::exchange(_masked, next);
    for (const auto& [monitor, workspace] : previous)
    {
      if (!_masked.contains(monitor) || _masked.at(monitor).lock() != workspace.lock())
      {
        mask(workspace.lock(), false);
      }
    }
    for (const auto& [monitor, workspace] : _masked)
    {
      mask(workspace.lock(), true);
    }
    for (const auto& monitor : State::monitorState()->monitors())
    {
      const auto before = previousLeases.find(monitor->m_id);
      const auto after = _leases.find(monitor->m_id);
      if (before != previousLeases.end() && (after == _leases.end() || before->second != after->second))
      {
        g_pHyprRenderer->damageMonitor(monitor);
      }
    }
  }

  PHLWINDOW Engine::release(PHLWORKSPACE workspace_)
  {
    const auto window = occupant(workspace_);
    if (window)
    {
      _sessions.at(window->metadata().stableID())._released.insert(workspace_->monitorID());
      rebuildLeases();
    }
    return window;
  }

  void Engine::reclaim(PHLWINDOW window_)
  {
    if (window_)
    {
      const auto entry = _sessions.find(window_->metadata().stableID());
      if (entry != _sessions.end())
      {
        entry->second._released.clear();
      }
    }
    rebuildLeases();
  }

  void Engine::updateGeometry(PHLWINDOW window_)
  {
    const auto rectangle = frame(window_);
    if (rectangle && window_->layoutTarget())
    {
      controller()->m_windowPosSettingQueued = true;
      window_->layoutTarget()->setPositionGlobal(*rectangle);
    }
  }

  void Engine::scheduleUpdate()
  {
    if (!_timer)
    {
      _timer = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, [](void* engine_)
      {
        static_cast<Engine*>(engine_)->settle();
        return 0;
      }, this);
    }
    if (!_timer || wl_event_source_timer_update(_timer, 150) != 0)
    {
      HyprlandAPI::addNotification(_handle, "Hyprstretch could not monitor layout changes; restoring stretched windows", CHyprColor{1.0, 0.2, 0.2, 1.0}, 8000);
      while (!_sessions.empty())
      {
        restore(_sessions.begin()->first);
      }
    }
  }

  void Engine::settle()
  {
    std::vector<uint64_t> invalid;
    for (auto& [id, session] : _sessions)
    {
      const auto window = session._window.lock();
      if (resolve(session) || !window || window->isFloating() != session._floating)
      {
        invalid.push_back(id);
      }
      else
      {
        updateGeometry(window);
      }
    }
    for (auto id : invalid)
    {
      restore(id);
    }
    rebuildLeases();
  }

  std::string Engine::status() const
  {
    std::ostringstream result;
    result << "Hyprstretch 1.0.1; manual sessions: " << _sessions.size() << '\n';
    for (const auto& monitor : State::monitorState()->monitors())
    {
      result << std::format("output {} desktop {},{} X11 {},{}\n", monitor->m_name, monitor->m_position.x, monitor->m_position.y,
                            monitor->m_xwaylandPosition.x, monitor->m_xwaylandPosition.y);
    }
    for (const auto& [id, session] : _sessions)
    {
      const auto window = session._window.lock();
      result << std::format("stretch {} title={} rect={},{},{},{} outputs=", id, window ? window->metadata().title() : "closed",
                            session._frame.x, session._frame.y, session._frame.w, session._frame.h);
      for (const auto& name : session._outputs)
      {
        result << name << ' ';
      }
      result << '\n';
    }
    return result.str();
  }

  std::string Engine::perform(const std::string& action_, const std::string& arguments_)
  {
    std::istringstream arguments(arguments_);
    std::vector<std::string> monitors;
    std::string selector, value;
    while (arguments >> value)
    {
      if (value.starts_with("window="))
      {
        if (!selector.empty() || value.size() == 7)
        {
          return "Provide window=ADDRESS once, with a nonempty address";
        }
        selector = value.substr(7);
      }
      else if (value.starts_with("--"))
      {
        return "Unknown option " + value;
      }
      else
      {
        monitors.push_back(value);
      }
    }
    if (action_ == "toggle")
    {
      return toggle(monitors, selector);
    }
    if (action_ == "stretch")
    {
      return stretch(monitors, selector);
    }
    if (action_ == "restore" && monitors.empty())
    {
      return restoreWindow(selector);
    }
    return "Use toggle/stretch [window=ADDRESS] [OUTPUT ...], restore [window=ADDRESS], or status";
  }

  int Engine::luaToggle(lua_State* state_)
  {
    return _luaOwner->luaCommand(state_, "toggle");
  }

  int Engine::luaStretch(lua_State* state_)
  {
    return _luaOwner->luaCommand(state_, "stretch");
  }

  int Engine::luaRestore(lua_State* state_)
  {
    return _luaOwner->luaCommand(state_, "restore");
  }

  int Engine::luaCommand(lua_State* state_, const std::string& verb_)
  {
    const auto reply = [&](const std::string& error_)
    {
      lua_settop(state_, 0);
      lua_pushboolean(state_, error_.empty());
      if (error_.empty())
      {
        lua_pushnil(state_);
      }
      else
      {
        lua_pushlstring(state_, error_.data(), error_.size());
        HyprlandAPI::addNotification(_handle, "Hyprstretch: " + error_, CHyprColor{0.9, 0.6, 0.1, 1.0}, 6000);
      }
      return 2;
    };
    std::vector<std::string> outputs;
    std::string selectedWindow;
    if (lua_gettop(state_) > 1 || (lua_gettop(state_) == 1 && !lua_istable(state_, 1)))
    {
      return reply("Use an optional options table with outputs and window fields");
    }
    if (lua_gettop(state_) == 1)
    {
      lua_pushnil(state_);
      while (lua_next(state_, 1))
      {
        if (lua_type(state_, -2) != LUA_TSTRING || (std::string_view{lua_tostring(state_, -2)} != "outputs" && std::string_view{lua_tostring(state_, -2)} != "window"))
        {
          return reply("Only outputs and window options are supported");
        }
        lua_pop(state_, 1);
      }
      lua_pushliteral(state_, "window");
      lua_rawget(state_, 1);
      if (!lua_isnil(state_, -1))
      {
        if (lua_type(state_, -1) != LUA_TSTRING)
        {
          return reply("window must be a string address");
        }
        selectedWindow = lua_tostring(state_, -1);
        if (selectedWindow.empty())
        {
          return reply("window must not be empty");
        }
      }
      lua_pop(state_, 1);
      lua_pushliteral(state_, "outputs");
      lua_rawget(state_, 1);
      if (!lua_isnil(state_, -1))
      {
        if (!lua_istable(state_, -1) || verb_ == "restore")
        {
          return reply("outputs must be an array for toggle/stretch only");
        }
        const auto length = lua_rawlen(state_, -1);
        if (!length)
        {
          return reply("An explicit output list must not be empty");
        }
        const int table = lua_gettop(state_);
        outputs.resize(length);
        size_t entries = 0;
        lua_pushnil(state_);
        while (lua_next(state_, table))
        {
          if (!lua_isinteger(state_, -2) || lua_tointeger(state_, -2) < 1 || static_cast<size_t>(lua_tointeger(state_, -2)) > length || lua_type(state_, -1) != LUA_TSTRING)
          {
            return reply("outputs must be a consecutive array of monitor-name strings");
          }
          outputs.at(lua_tointeger(state_, -2) - 1) = lua_tostring(state_, -1);
          ++entries;
          lua_pop(state_, 1);
        }
        if (entries != length)
        {
          return reply("The output list must not contain holes");
        }
      }
    }
    if (verb_ == "restore")
    {
      return reply(restoreWindow(selectedWindow));
    }
    return reply(verb_ == "toggle" ? toggle(outputs, selectedWindow) : stretch(outputs, selectedWindow));
  }

  IPC::Socket1::SResponse Engine::command(const IPC::Socket1::SRequest& request_)
  {
    std::istringstream arguments(request_.command);
    std::string root, verb, rest;
    arguments >> root >> verb;
    if (root != "hyprstretch")
    {
      return "error: Unknown plugin command";
    }
    if (verb.empty() || verb == "status")
    {
      return status();
    }
    std::getline(arguments, rest);
    const auto error = perform(verb, rest);
    if (!error.empty())
    {
      HyprlandAPI::addNotification(_handle, "Hyprstretch: " + error, CHyprColor{0.9, 0.6, 0.1, 1.0}, 6000);
      return "error: " + error;
    }
    return "ok";
  }
}
