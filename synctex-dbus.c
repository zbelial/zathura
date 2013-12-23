/* See LICENSE file for license and copyright information */

#include "synctex-dbus.h"
#include "synctex.h"
#include "macros.h"
#include "zathura.h"
#include "document.h"
#include <girara/utils.h>
#include <gio/gio.h>
#include <sys/types.h>
#include <unistd.h>

G_DEFINE_TYPE(ZathuraSynctexDbus, zathura_synctex_dbus, G_TYPE_OBJECT)

/* template for bus name */
static const char DBUS_NAME_TEMPLATE[] = "org.pwmt.zathura.PID-%d";
/* template for object path */
const char DBUS_OBJPATH[] = "/org/pwmt/zathura/synctex";

typedef struct private_s {
  zathura_t* zathura;
  GDBusNodeInfo* introspection_data;
  GDBusConnection* connection;
  guint owner_id;
  guint registration_id;
} private_t;

#define GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE((obj), ZATHURA_TYPE_SYNCTEX_DBUS, \
                               private_t))

/* Introspection data for the service we are exporting */
static const char SYNCTEX_DBUS_INTROSPECTION[] =
  "<node>\n"
  "  <interface name='org.pwmt.zathura.Synctex'>\n"
  "    <method name='View'>\n"
  "      <arg type='s' name='position' direction='in' />\n"
  "      <arg type='b' name='return' direction='out' />\n"
  "    </method>\n"
  "    <property type='s' name='filename' access='read' />\n"
  "  </interface>\n"
  "</node>";

static const GDBusInterfaceVTable interface_vtable;

static bool synctex_dbus_view(ZathuraSynctexDbus* synctex_view,
    const char* position);

static void
finalize(GObject* object)
{
  ZathuraSynctexDbus* synctex_dbus = ZATHURA_SYNCTEX_DBUS(object);
  private_t* priv                  = GET_PRIVATE(synctex_dbus);

  if (priv->connection != NULL && priv->registration_id > 0) {
    g_dbus_connection_unregister_object(priv->connection, priv->registration_id);
  }

  if (priv->owner_id > 0) {
    g_bus_unown_name(priv->owner_id);
  }

  if (priv->introspection_data != NULL) {
    g_dbus_node_info_unref(priv->introspection_data);
  }

  G_OBJECT_CLASS(zathura_synctex_dbus_parent_class)->finalize(object);
}

static void
zathura_synctex_dbus_class_init(ZathuraSynctexDbusClass* class)
{
  /* initialize methods */
  class->view = synctex_dbus_view;

  /* add private members */
  g_type_class_add_private(class, sizeof(private_t));

  /* overwrite methods */
  GObjectClass* object_class = G_OBJECT_CLASS(class);
  object_class->finalize     = finalize;
}

static void
zathura_synctex_dbus_init(ZathuraSynctexDbus* synctex_dbus)
{
  private_t* priv          = GET_PRIVATE(synctex_dbus);
  priv->zathura            = NULL;
  priv->introspection_data = NULL;
  priv->connection         = NULL;
  priv->owner_id           = 0;
  priv->registration_id    = 0;
}

static void
bus_acquired(GDBusConnection* connection, const gchar* name, void* data)
{
  girara_debug("Bus acquired at '%s'.", name);

  ZathuraSynctexDbus* dbus = data;
  private_t* priv = GET_PRIVATE(dbus);

  GError* error = NULL;
  priv->registration_id = g_dbus_connection_register_object(connection,
      DBUS_OBJPATH, priv->introspection_data->interfaces[0],
      &interface_vtable, dbus, NULL, &error);

  if (priv->registration_id == 0) {
    girara_warning("Failed to register object on D-Bus connection: %s",
        error->message);
    g_error_free(error);
    return;
  }

  priv->connection = connection;
}

static void
name_acquired(GDBusConnection* UNUSED(connection), const gchar* name,
    void* UNUSED(data))
{
  girara_debug("Acquired '%s' on session bus.", name);
}

static void
name_lost(GDBusConnection* UNUSED(connection), const gchar* name,
    void* UNUSED(data))
{
  girara_debug("Lost connection or failed to acquire '%s' on session bus.",
      name);
}

ZathuraSynctexDbus*
zathura_synctex_dbus_new(zathura_t* zathura)
{
  GObject* obj = g_object_new(ZATHURA_TYPE_SYNCTEX_DBUS, NULL);
  if (obj == NULL) {
    return NULL;
  }

  ZathuraSynctexDbus* synctex_dbus = ZATHURA_SYNCTEX_DBUS(obj);
  private_t* priv                  = GET_PRIVATE(synctex_dbus);
  priv->zathura                    = zathura;

  GError* error = NULL;
  priv->introspection_data = g_dbus_node_info_new_for_xml(SYNCTEX_DBUS_INTROSPECTION, &error);
  if (priv->introspection_data == NULL) {
    girara_warning("Failed to parse introspection data: %s", error->message);
    g_error_free(error);
    g_object_unref(obj);
    return NULL;
  }

  char* well_known_name = g_strdup_printf(DBUS_NAME_TEMPLATE, getpid());
  priv->owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
      well_known_name, G_BUS_NAME_OWNER_FLAGS_NONE, bus_acquired,
      name_acquired, name_lost, synctex_dbus, NULL);
  g_free(well_known_name);

  return synctex_dbus;
}

static bool
synctex_dbus_view(ZathuraSynctexDbus* synctex_dbus, const char* position)
{
  private_t* priv = GET_PRIVATE(synctex_dbus);
  return synctex_view(priv->zathura, position);
}

bool
zathura_synctex_dbus_view(ZathuraSynctexDbus* synctex_dbus, const char* position)
{
  g_return_val_if_fail(ZATHURA_IS_SYNCTEX_DBUS(synctex_dbus), false);
  g_return_val_if_fail(position != NULL, false);

  return ZATHURA_SYNCTEX_DBUS_GET_CLASS(synctex_dbus)->view(synctex_dbus,
      position);
}

/* D-Bus handler */

static void
handle_method_call(GDBusConnection* UNUSED(connection),
    const gchar* UNUSED(sender), const gchar* UNUSED(object_path),
    const gchar* UNUSED(interface_name),
    const gchar* method_name, GVariant* parameters,
    GDBusMethodInvocation* invocation, void* data)
{
  ZathuraSynctexDbus* synctex_dbus = data;

  if (g_strcmp0(method_name, "View") == 0) {
      gchar* position = NULL;
      g_variant_get(parameters, "(s)", &position);

      const bool ret = zathura_synctex_dbus_view(synctex_dbus, position);
      g_free(position);

      GVariant* result = g_variant_new("(b)", ret);
      g_dbus_method_invocation_return_value(invocation, result);
  }
}

static GVariant*
handle_get_property(GDBusConnection* UNUSED(connection),
    const gchar* UNUSED(sender), const gchar* UNUSED(object_path),
    const gchar* UNUSED(interface_name), const gchar* property_name,
    GError** UNUSED(error), void* data)
{
  ZathuraSynctexDbus* synctex_dbus = data;
  private_t* priv = GET_PRIVATE(synctex_dbus);

  if (g_strcmp0(property_name, "filename") == 0) {
    return g_variant_new_string(zathura_document_get_path(priv->zathura->document));
  }

  return NULL;
}

static const GDBusInterfaceVTable interface_vtable =
{
  .method_call  = handle_method_call,
  .get_property = handle_get_property,
  .set_property = NULL
};

