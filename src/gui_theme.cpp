#include "gui_theme.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dbus/dbus.h>
#endif

namespace vibescriber {

bool system_prefers_dark()
{
#ifdef _WIN32
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return result == ERROR_SUCCESS && value == 0;
#else
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!connection) {
        dbus_error_free(&error);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(connection, FALSE);
    DBusMessage* request = dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "ReadOne");
    if (!request) {
        dbus_connection_close(connection);
        dbus_connection_unref(connection);
        dbus_error_free(&error);
        return false;
    }
    const char* setting_namespace = "org.freedesktop.appearance";
    const char* key = "color-scheme";
    const bool appended = dbus_message_append_args(request,
        DBUS_TYPE_STRING, &setting_namespace, DBUS_TYPE_STRING, &key,
        DBUS_TYPE_INVALID);
    DBusMessage* reply = appended
        ? dbus_connection_send_with_reply_and_block(connection, request, 250, &error)
        : nullptr;
    dbus_message_unref(request);
    bool dark = false;
    if (reply) {
        DBusMessageIter outer;
        if (dbus_message_iter_init(reply, &outer)
            && dbus_message_iter_get_arg_type(&outer) == DBUS_TYPE_VARIANT) {
            DBusMessageIter inner;
            dbus_message_iter_recurse(&outer, &inner);
            if (dbus_message_iter_get_arg_type(&inner) == DBUS_TYPE_UINT32) {
                dbus_uint32_t value = 0;
                dbus_message_iter_get_basic(&inner, &value);
                dark = value == 1;
            }
        }
        dbus_message_unref(reply);
    }
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
    dbus_error_free(&error);
    return dark;
#endif
}

} // namespace vibescriber
