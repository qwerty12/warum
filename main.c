#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/netfilter_ipv4.h>

#include <libnetfilter_queue/libnetfilter_queue.h>
#include <libnetfilter_queue/libnetfilter_queue_tcp.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#define DEFAULT_QUEUE_NUM 0
#define DEFAULT_WINDOW_SIZE 40

#include <config.h>

#ifdef DBUS_ENABLE
#include <pk.qwerty12.warum.h>

enum {
    PROP_0, // GObject reserved
    DUMMY_PROP, // for future expansion of non-DBus properties specific to this object
    PROP_LAST,

    PROP_ENABLED, // overridden parent property - must match the order of declared properties in the XML
};

static gboolean init_nf(gpointer);
static void deinit_nf(gpointer);

// Yes, I know the following should really be in a separate header file
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
G_DECLARE_FINAL_TYPE(BusWarumPropoverride, bus_warum_propoverride, BUS_WARUM, PROPOVERRIDE, BusWarumSkeleton)
#pragma GCC diagnostic pop
#define BUS_WARUM_TYPE_PROPOVERRIDE (bus_warum_propoverride_get_type())

struct _BusWarumPropoverride {
    BusWarumSkeleton parent_instance;
};

G_DEFINE_TYPE(BusWarumPropoverride, bus_warum_propoverride, BUS_TYPE_WARUM_SKELETON)

static void bus_warum_propoverride_init(BusWarumPropoverride *bus G_GNUC_UNUSED){}

static void bus_warum_propoverride_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    if (prop_id > PROP_LAST) {
        GParamSpec *parent;

        if (!(parent = g_object_class_find_property(bus_warum_propoverride_parent_class, pspec->name)))
            return;

        prop_id -= PROP_LAST;

        G_OBJECT_CLASS(bus_warum_propoverride_parent_class)->get_property(object, prop_id, value, parent);
    }
}

static void bus_warum_propoverride_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GParamSpec *parent;

    if (prop_id == PROP_ENABLED) {
        gpointer ctx;
        gboolean parent_enabled;
        g_object_get(object, "enabled", &parent_enabled, NULL);

        if (parent_enabled == g_value_get_boolean(value))
            return;

        ctx = g_object_get_data(object, "data");
        if (g_value_get_boolean(value)) {
            if (!init_nf(ctx))
                return;
        } else {
            deinit_nf(ctx);
        }
    }

    if (prop_id < PROP_LAST)
        return;

    // get original GParamSpec
    if (!(parent = g_object_class_find_property(bus_warum_propoverride_parent_class, pspec->name)))
        return;

    // sync with parent's prop_id for the overriden property
    prop_id -= PROP_LAST;

    G_OBJECT_CLASS(bus_warum_propoverride_parent_class)->set_property(object, prop_id, value, parent);
}

static void bus_warum_propoverride_set_enabled_original(BusWarumPropoverride *object, gboolean value)
{
    const guint prop_id = PROP_ENABLED - PROP_LAST;
    GParamSpec *parent = g_object_class_find_property(bus_warum_propoverride_parent_class, "enabled");
    GValue val = G_VALUE_INIT;
    g_value_set_boolean(g_value_init(&val, G_TYPE_BOOLEAN), value);
    G_OBJECT_CLASS(bus_warum_propoverride_parent_class)->set_property(G_OBJECT(object), prop_id, &val, parent);
    g_value_unset(&val);
}

G_GNUC_UNUSED static void bus_warum_propoverride_class_init(BusWarumPropoverrideClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->get_property = bus_warum_propoverride_get_property;
    gobject_class->set_property = bus_warum_propoverride_set_property;
    g_object_class_override_property(gobject_class, PROP_ENABLED, "enabled");
}

BusWarumPropoverride *bus_warum_propoverride_new(void) {
    return g_object_new(BUS_WARUM_TYPE_PROPOVERRIDE, NULL);
}
#endif

typedef struct
{
    int qnum;
    int wsize;
    guint q_watch_id;
#ifdef DBUS_ENABLE
    gboolean enable_dbus;
    gboolean start_disabled;
    guint owner_id;
#endif
    struct nfq_handle *h;
    struct nfq_q_handle *qh;
    GIOChannel *channel;
    GMainLoop *loop;
#ifdef DBUS_ENABLE
    BusWarumPropoverride *interface;
#endif
} ctx_data;

#ifdef DBUS_ENABLE
static void on_name_lost(GDBusConnection *connection G_GNUC_UNUSED, const gchar *name, gpointer user_data)
{
    ctx_data *ctx = (ctx_data*) user_data;
    g_printerr("Cannot register service name %s on the system bus, exiting\n", name);
    g_main_loop_quit(ctx->loop);
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name G_GNUC_UNUSED, gpointer user_data)
{
    ctx_data *ctx = (ctx_data*) user_data;

    ctx->interface = bus_warum_propoverride_new();
    g_object_set_data(G_OBJECT(ctx->interface), "data", user_data);
    g_object_freeze_notify(G_OBJECT(ctx->interface));
    bus_warum_propoverride_set_enabled_original(ctx->interface, !ctx->start_disabled);
    g_object_thaw_notify(G_OBJECT(ctx->interface));

    if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(ctx->interface), connection, "/", NULL)) {
        g_printerr("Could not export D-Bus interface, exiting\n");
        g_main_loop_quit(ctx->loop);
    }
}

static void deinit_dbus(ctx_data *ctx)
{
    if (ctx->interface) {
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(ctx->interface));
        g_object_unref(ctx->interface);
        ctx->interface = NULL;
    }
    if (ctx->owner_id) {
        g_bus_unown_name(ctx->owner_id);
        ctx->owner_id = 0;
    }
}

static void init_dbus(ctx_data *ctx)
{
    ctx->owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
                                   "pk.qwerty12.warum",
                                   G_BUS_NAME_OWNER_FLAGS_NONE,
                                   NULL,
                                   on_name_acquired,
                                   on_name_lost,
                                   ctx,
                                   NULL);
}
#endif

static void deinit_nf(gpointer user_data)
{
    ctx_data *ctx = (ctx_data*) user_data;
    if (ctx->q_watch_id) {
        g_source_remove(ctx->q_watch_id);
        ctx->q_watch_id = 0;
    }
    if (ctx->channel) {
        g_io_channel_unref(ctx->channel);
        ctx->channel = NULL;
    }
    if (ctx->qh) {
        nfq_destroy_queue(ctx->qh);
        ctx->qh = NULL;
    }
    if (ctx->h) {
        nfq_close(ctx->h);
        ctx->h = NULL;
    }
}

static void cleanup(ctx_data *ctx)
{
#ifdef DBUS_ENABLE
    deinit_dbus(ctx);
#endif
    deinit_nf(ctx);
    if (ctx->loop) {
        g_main_loop_unref(ctx->loop);
        ctx->loop = NULL;
    }
}

static gboolean on_sigint(gpointer data_ptr)
{
    g_main_loop_quit((GMainLoop*) data_ptr);
    return G_SOURCE_REMOVE;
}

static inline gboolean proto_check_ipv4(unsigned const char *data, int len) {
    return len>=20 && (data[0] & 0xF0)==0x40 && len>=((data[0] & 0x0F)<<2);
}

// move to transport protocol
static inline void proto_skip_ipv4(unsigned char **data, int *len)
{
    int l;

    l = (**data & 0x0F)<<2;
    *data += l;
    *len -= l;
}

static inline gboolean proto_check_tcp(unsigned const char *data, int len)  {
    return G_LIKELY(len>=20) && len>=((data[12] & 0xF0)>>2);
}

static inline void proto_skip_tcp(unsigned char **data,int *len)
{
    int l;
    l = ((*data)[12] & 0xF0)>>2;
    *data += l;
    *len -= l;
}

static inline gboolean tcp_synack_segment(const struct tcphdr *tcphdr)
{
    return  tcphdr->urg == 0 &&
            tcphdr->ack == 1 &&
            tcphdr->psh == 0 &&
            tcphdr->rst == 0 &&
            tcphdr->syn == 1 &&
            tcphdr->fin == 0;
}

static gboolean processPacketData(unsigned char *data, int len, const ctx_data *ctx, gboolean ethertype_ipv4)
{
    struct iphdr *iphdr = NULL;
    struct tcphdr *tcphdr = NULL;
    uint8_t proto;

    if (G_UNLIKELY(!ethertype_ipv4) || !proto_check_ipv4(data, len))
        return FALSE;

    iphdr = (struct iphdr *) data;
    proto = iphdr->protocol;
    proto_skip_ipv4(&data, &len);

    if (proto != IPPROTO_TCP || !proto_check_tcp(data, len))
        return FALSE;

    tcphdr = (struct tcphdr *) data;
    proto_skip_tcp(&data, &len);
    if (G_LIKELY(tcp_synack_segment(tcphdr))) {
        tcphdr->window = g_htons(ctx->wsize);
        nfq_tcp_compute_checksum_ipv4(tcphdr, iphdr);
        return TRUE;
    }

    return FALSE;
}

static int on_handle_packet(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg G_GNUC_UNUSED, struct nfq_data *nfad, void *data)
{
    unsigned char *payload;
    uint32_t id;
    int len = nfq_get_payload(nfad, &payload);
    struct nfqnl_msg_packet_hdr *ph = nfq_get_msg_packet_hdr(nfad);
    const ctx_data *ctx = (ctx_data*) data;
    gboolean ethertype_ipv4;

    if (ph) {
        id = g_ntohl(ph->packet_id);
        if (G_UNLIKELY(ph->hook != NF_IP_LOCAL_IN))
            goto skip;
        ethertype_ipv4 = g_ntohs(ph->hw_protocol) == ETHERTYPE_IP;
    } else {
        id = 0;
        ethertype_ipv4 = TRUE;
    }

    if (len >= (int) sizeof(struct iphdr))
    {
        if (processPacketData(payload, len, ctx, ethertype_ipv4))
            return nfq_set_verdict(qh, id, NF_ACCEPT, len, payload);
    }

skip:
    return nfq_set_verdict(qh, id, NF_ACCEPT, 0, NULL);
}

static gboolean on_data(GIOChannel *source, GIOCondition condition G_GNUC_UNUSED, gpointer data)
{
    const ctx_data *ctx = (ctx_data*) data;
    gsize rv;
    gchar buf[4096] __attribute__ ((aligned));
    GError *error = NULL;

    if (G_UNLIKELY(condition != G_IO_IN)) {
        g_printerr("Error in %s\n, exiting\n", G_STRFUNC);
        goto err;
    }

    switch (g_io_channel_read_chars(source, buf, sizeof(buf), &rv, &error))
    {
        case G_IO_STATUS_NORMAL:
        {
            int r = nfq_handle_packet(ctx->h, buf, rv);
            if (r)
                g_critical("nfq_handle_packet error %d\n", r);
            return TRUE;
        }
        case G_IO_STATUS_ERROR:
            g_printerr("error in g_io_channel_read_chars() (%s), exiting\n", error->message);
            g_error_free(error);
            goto err;
        default:
            break;
    }

    return TRUE;

err:
    g_main_loop_quit(ctx->loop);
    return FALSE;
}

static gboolean init_nf(gpointer user_data)
{
    ctx_data *ctx = (ctx_data*) user_data;
    int fd;

    if (ctx->h)
        return TRUE;

    g_debug("opening library handle");
    if (!(ctx->h = nfq_open())) {
        g_critical("error during nfq_open()");
        return FALSE;
    }

    g_debug("unbinding existing nf_queue handler for AF_INET (if any)");
    if (nfq_unbind_pf(ctx->h, AF_INET) < 0)
        g_warning("error during first nfq_unbind_pf()");

    g_debug("binding nfnetlink_queue as nf_queue handler for AF_INET");
    if (nfq_bind_pf(ctx->h, AF_INET) < 0) {
        g_critical("error during nfq_bind_pf()");
        goto free;
    }

    g_debug("binding this socket to queue '%u'", ctx->qnum);
    if (!(ctx->qh = nfq_create_queue(ctx->h, (uint16_t) ctx->qnum, on_handle_packet, ctx))) {
        g_critical("error during nfq_create_queue()");
        goto free;
    }

    g_debug("setting copy_packet mode");
    if (nfq_set_mode(ctx->qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
        g_critical("can't set packet_copy mode");
        goto free;
    }

    if ((fd = nfq_fd(ctx->h)) != -1) {
        setsockopt(fd, SOL_NETLINK, NETLINK_NO_ENOBUFS, (int[]) {1}, sizeof(int));
        if (!(ctx->channel = g_io_channel_unix_new(fd))) {
            g_critical("failed to create GIOChannel");
            goto free;
        }
        g_io_channel_set_encoding(ctx->channel, NULL, NULL);
        g_io_channel_set_buffered(ctx->channel, FALSE);

        if (!(ctx->q_watch_id = g_io_add_watch(ctx->channel, G_IO_IN | G_IO_HUP | G_IO_ERR, on_data, ctx))) {
            g_critical("failed to add nfq fd monitor");
            goto free;
        }
    }

    return TRUE;

free:
    deinit_nf(ctx);
    return FALSE;
}

static gboolean parse_args(int *argc, char **argv[], ctx_data *ctx)
{
    GError *error = NULL;
    GOptionEntry opt_entries[] =
    {
        { "qnum", 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &ctx->qnum, "Attach to nfqueue N (defaults to " G_STRINGIFY(DEFAULT_QUEUE_NUM) ")", "N" },
        { "wsize", 'w', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &ctx->wsize, "Set window size of HTTPS packets to W (defaults to " G_STRINGIFY(DEFAULT_WINDOW_SIZE) ")", "W" },
#ifdef DBUS_ENABLE
        { "dbus", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &ctx->enable_dbus, "Register on the system bus (defaults to no)", NULL },
        { "disable", 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &ctx->start_disabled, "Start in a disabled state (defaults to no)", NULL },
#endif
        { NULL, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, NULL, NULL, NULL }
    };
    GOptionContext *context = g_option_context_new("- change window size of IPv4 HTTPS SYN & ACK packets");

    g_option_context_add_main_entries(context, opt_entries, NULL);
    if (!g_option_context_parse(context, argc, argv, &error))
    {
        g_printerr("option parsing failed: %s\n", error->message);
        return FALSE;
    }
    else if (ctx->qnum < 0 || ctx->qnum > 65535)
    {
        g_printerr("value for option \"--qnum\" out of range (0-65535)\n");
        return FALSE;
    }
    else if (ctx->wsize < 0 || ctx->wsize > 65535)
    {
        g_printerr("value for option \"--wsize\" out of range (0-65535)\n");
        return FALSE;
    }

    g_option_context_free(context);
    return TRUE;
}

int main(int argc, char *argv[])
{
    ctx_data ctx =
    {
        .qnum = DEFAULT_QUEUE_NUM,
        .wsize = DEFAULT_WINDOW_SIZE,
        0
    };

    if (!parse_args(&argc, &argv, &ctx))
        return EXIT_FAILURE;

    ctx.loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, on_sigint, ctx.loop);
    g_unix_signal_add(SIGTERM, on_sigint, ctx.loop);

#ifdef DBUS_ENABLE
    if (!ctx.start_disabled)
#endif
        if (!init_nf(&ctx))
            return EXIT_FAILURE;

#ifdef DBUS_ENABLE
    if (ctx.enable_dbus)
        init_dbus(&ctx);
#endif

    g_main_loop_run(ctx.loop);

    cleanup(&ctx);
    return EXIT_SUCCESS;
}
