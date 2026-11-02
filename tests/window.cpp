#include <xcb/xcb.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <unistd.h>

int main(int argc_, char** argv_)
{
  int screenIndex = 0;
  std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(xcb_connect(nullptr, &screenIndex), xcb_disconnect);
  if (!connection || xcb_connection_has_error(connection.get()))
  {
    std::cerr << "Cannot connect to the test X server\n";
    return 1;
  }
  auto screen = xcb_setup_roots_iterator(xcb_get_setup(connection.get()));
  for (int index = 0; index < screenIndex; ++index)
  {
    xcb_screen_next(&screen);
  }
  const auto window = xcb_generate_id(connection.get());
  const uint32_t attributes[]{0x3275d7, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY};
  xcb_create_window(connection.get(), XCB_COPY_FROM_PARENT, window, screen.data->root, 0, 0, 640, 480, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen.data->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, attributes);
  const char* title = argc_ > 1 ? argv_[1] : "stretch-fixture-x11";
  xcb_change_property(connection.get(), XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, std::strlen(title), title);
  const char identity[] = "stretch-fixture\0StretchFixture";
  xcb_change_property(connection.get(), XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(identity), identity);
  const char pidName[] = "_NET_WM_PID";
  std::unique_ptr<xcb_intern_atom_reply_t, decltype(&std::free)> pidAtom(
    xcb_intern_atom_reply(connection.get(), xcb_intern_atom(connection.get(), false, sizeof(pidName) - 1, pidName), nullptr), std::free);
  if (!pidAtom)
  {
    return 1;
  }
  const uint32_t pid = getpid();
  xcb_change_property(connection.get(), XCB_PROP_MODE_REPLACE, window, pidAtom->atom, XCB_ATOM_CARDINAL, 32, 1, &pid);
  xcb_map_window(connection.get(), window);
  xcb_flush(connection.get());
  while (auto* raw = xcb_wait_for_event(connection.get()))
  {
    std::unique_ptr<xcb_generic_event_t, decltype(&std::free)> event(raw, std::free);
    const auto type = event->response_type & 0x7f;
    if (type == XCB_EXPOSE || type == XCB_CONFIGURE_NOTIFY)
    {
      xcb_clear_area(connection.get(), false, window, 0, 0, 0, 0);
      xcb_flush(connection.get());
    }
  }
  return xcb_connection_has_error(connection.get()) ? 1 : 0;
}
