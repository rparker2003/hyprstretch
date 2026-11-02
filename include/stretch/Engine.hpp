#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/ipc/s1/S1.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenTypes.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Stretch
{
  class Engine
  {
  public:
    explicit Engine(HANDLE handle_);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void start();
    std::string toggle(const std::vector<std::string>& outputs_, const std::string& target_ = {});
    std::string stretch(const std::vector<std::string>& outputs_, const std::string& target_ = {});
    std::string restoreWindow(const std::string& target_ = {});
    std::string status() const;
    std::optional<CBox> frame(PHLWINDOW window_) const;
    PHLWINDOW occupant(PHLWORKSPACE workspace_) const;
    PHLWINDOW release(PHLWORKSPACE workspace_);
    void reclaim(PHLWINDOW window_);
    void scheduleUpdate();

  private:
    struct Session
    {
      PHLWINDOWREF _window;
      std::vector<std::string> _outputs;
      std::vector<PHLMONITORREF> _monitors;
      CBox _frame;
      CBox _savedFrame;
      Fullscreen::SFullscreenMode _savedMode;
      bool _savedLayoutMode = false;
      bool _floating = false;
      std::set<MONITORID> _released;
    };

    HANDLE _handle;
    std::map<uint64_t, Session> _sessions;
    std::map<MONITORID, uint64_t> _leases;
    std::map<MONITORID, PHLWORKSPACEREF> _masked;
    std::set<uint64_t> _transitions;
    std::vector<CHyprSignalListener> _subscriptions;
    SP<IPC::Socket1::SCommand> _command;
    std::vector<std::string> _luaFunctions;
    inline static Engine* _luaOwner = nullptr;
    wl_event_source* _timer = nullptr;
    bool _started = false;

    std::optional<std::string> resolve(Session& session_) const;
    void restore(uint64_t id_);
    void rebuildLeases();
    void updateGeometry(PHLWINDOW window_);
    void mask(PHLWORKSPACE workspace_, bool covered_);
    void settle();
    IPC::Socket1::SResponse command(const IPC::Socket1::SRequest& request_);
    std::string perform(const std::string& action_, const std::string& arguments_);
    PHLWINDOW target(const std::string& selector_) const;
    int luaCommand(lua_State* state_, const std::string& verb_);
    static int luaToggle(lua_State* state_);
    static int luaStretch(lua_State* state_);
    static int luaRestore(lua_State* state_);
  };

  namespace Hooks
  {
    void install(HANDLE handle_, Engine& engine_);
    void remove(HANDLE handle_);
  }
}
