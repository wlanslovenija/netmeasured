/*
 * netmeasured - simple network measurement daemon
 *
 * Copyright (C) 2014 Jernej Kos <jernej@kos.mx>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <netmeasured/probe.h>

#include <libubox/avl.h>
#include <libubox/avl-cmp.h>
#include <libubox/usock.h>
#include <sys/socket.h>
#include <syslog.h>

struct nm_probe {
  /* Probe AVL node */
  struct avl_node avl;
  /* Probe name */
  char *name;
  /* Number of probes sent */
  size_t stats_probes_sent;
  /* Number of probes received */
  size_t stats_probes_rcvd;
  /* Sequence number */
  uint64_t seqno;
  /* Probe interval (in miliseconds) */
  int interval;
  /* Probe next scheduled run */
  struct uloop_timeout sched_timeout;
  /* Probe UDP socket */
  struct uloop_fd sock;
};

/* AVL tree containing all registered probes with probe name as their key */
static struct avl_tree probe_registry;
/* Ubus reply buffer */
static struct blob_buf reply_buf;

enum {
  NMD_D_PROBE,
  __NMD_D_MAX,
};

static const struct blobmsg_policy nm_probe_policy[__NMD_D_MAX] = {
  [NMD_D_PROBE] = { .name = "probe", .type = BLOBMSG_TYPE_STRING },
};

static uint64_t parse_u64(unsigned char *buffer)
{
  uint64_t value = *((uint64_t*) buffer);
  return value;
}

static void put_u64(unsigned char *buffer, uint64_t value)
{
  buffer[0] = value >> 56;
  buffer[1] = value >> 48;
  buffer[2] = value >> 40;
  buffer[3] = value >> 32;
  buffer[4] = value >> 24;
  buffer[5] = value >> 16;
  buffer[6] = value >> 8;
  buffer[7] = value;
}

static void nm_probe_handler(struct uloop_fd *fd, unsigned int events)
{
  struct nm_probe *probe;

  /* Extract the probe where the event occurred */
  probe = container_of(fd, struct nm_probe, sock);

  if (fd->error) {
    fd->error = false;
    return;
  }

  /* Read the probe */
  unsigned char probe_data[128] = {0, };
  if (recv(probe->sock.fd, probe_data, sizeof(probe_data), 0) > 0) {
    /* Validate seqno in probe (if different than current seqno, ignore) */
    if (parse_u64(probe_data) != probe->seqno)
      return;
    probe->stats_probes_rcvd++;
  }
}

static void nm_probe_run(struct uloop_timeout *timeout)
{
  struct nm_probe *probe;

  /* Extract the probe where the timeout ocurred */
  probe = container_of(timeout, struct nm_probe, sched_timeout);
  /* Initiate probe */
  unsigned char probe_data[128] = {0, };
  put_u64(probe_data, probe->seqno);
  if (send(probe->sock.fd, probe_data, sizeof(probe_data), 0) > 0) {
    probe->stats_probes_sent++;
  }
  /* Reschedule probe */
  uloop_timeout_set(&probe->sched_timeout, probe->interval);
}

static void nm_create_probe(const char *name, const char *address, const char *port, int interval)
{
  /* Register the probe */
  struct nm_probe *probe = (struct nm_probe*) malloc(sizeof(struct nm_probe));
  if (!probe) {
    syslog(LOG_ERR, "Failed to create probe entry '%s' (%s:%s).", name, address, port);
    return;
  }
  probe->name = strdup(name);
  probe->interval = interval;
  probe->stats_probes_sent = 0;
  probe->stats_probes_rcvd = 0;
  probe->seqno = 0;

  /* Create the UDP socket */
  probe->sock.cb = &nm_probe_handler;
  probe->sock.fd = usock(USOCK_UDP, address, port);
  if (probe->sock.fd < 0) {
    free(probe->name);
    free(probe);
    syslog(LOG_ERR, "Failed to initialize probe '%s' (%s:%s).", name, address, port);
    return;
  }

  /* Register probe in our probe registry */
  probe->avl.key = probe->name;
  if (avl_insert(&probe_registry, &probe->avl) != 0) {
    free(probe->name);
    free(probe);
    syslog(LOG_WARNING, "Ignoring probe '%s' (%s:%s) because of name conflict!", name, address, port);
    return;
  }

  uloop_fd_add(&probe->sock, ULOOP_READ | ULOOP_ERROR_CB);

  /* Schedule the probe */
  probe->sched_timeout.cb = nm_probe_run;
  uloop_timeout_set(&probe->sched_timeout, interval);

  syslog(LOG_INFO, "Created probe '%s' (%s:%s, interval %d msec).", name, address, port, interval);
}

static int nm_handle_reset_probe(struct ubus_context *ctx, struct ubus_object *obj,
                                 struct ubus_request_data *req, const char *method,
                                 struct blob_attr *msg)
{
  struct blob_attr *tb[__NMD_D_MAX];

  blobmsg_parse(nm_probe_policy, __NMD_D_MAX, tb, blob_data(msg), blob_len(msg));
  if (!tb[NMD_D_PROBE])
    return UBUS_STATUS_INVALID_ARGUMENT;

  /* Handle probe parameter to filter to a specific probe */
  struct nm_probe *probe;
  probe = avl_find_element(&probe_registry, blobmsg_data(tb[NMD_D_PROBE]), probe, avl);
  if (!probe)
    return UBUS_STATUS_NOT_FOUND;

  probe->stats_probes_sent = 0;
  probe->stats_probes_rcvd = 0;
  probe->seqno++;

  blob_buf_init(&reply_buf, 0);
  ubus_send_reply(ctx, req, reply_buf.head);

  return UBUS_STATUS_OK;
}

static int nm_handle_get_probe(struct ubus_context *ctx, struct ubus_object *obj,
                               struct ubus_request_data *req, const char *method,
                               struct blob_attr *msg)
{
  struct blob_attr *tb[__NMD_D_MAX];
  void *c;

  blobmsg_parse(nm_probe_policy, __NMD_D_MAX, tb, blob_data(msg), blob_len(msg));

  if (tb[NMD_D_PROBE]) {
    /* Handle probe parameter to filter to a specific probe */
    struct nm_probe *probe;
    probe = avl_find_element(&probe_registry, blobmsg_data(tb[NMD_D_PROBE]), probe, avl);
    if (!probe)
      return UBUS_STATUS_NOT_FOUND;

    blob_buf_init(&reply_buf, 0);
    c = blobmsg_open_table(&reply_buf, probe->name);
    blobmsg_add_string(&reply_buf, "name", probe->name);
    blobmsg_add_u32(&reply_buf, "interval", probe->interval);
    blobmsg_add_u32(&reply_buf, "sent", probe->stats_probes_sent);
    blobmsg_add_u32(&reply_buf, "rcvd", probe->stats_probes_rcvd);
    blobmsg_add_u32(&reply_buf, "loss", probe->stats_probes_sent - probe->stats_probes_rcvd);
    if (probe->stats_probes_sent > 0)
      blobmsg_add_u32(&reply_buf, "loss_percent", (100 *(probe->stats_probes_sent - probe->stats_probes_rcvd)) / probe->stats_probes_sent);
    else
      blobmsg_add_u32(&reply_buf, "loss_percent", 0);
    blobmsg_close_table(&reply_buf, c);
  } else {
    /* Iterate through all probes and add them to our reply */
    struct nm_probe *probe;
    blob_buf_init(&reply_buf, 0);

    avl_for_each_element(&probe_registry, probe, avl) {
      c = blobmsg_open_table(&reply_buf, probe->name);
      blobmsg_add_string(&reply_buf, "name", probe->name);
      blobmsg_add_u32(&reply_buf, "interval", probe->interval);
      blobmsg_add_u32(&reply_buf, "sent", probe->stats_probes_sent);
      blobmsg_add_u32(&reply_buf, "rcvd", probe->stats_probes_rcvd);
      blobmsg_add_u32(&reply_buf, "loss", probe->stats_probes_sent - probe->stats_probes_rcvd);
      if (probe->stats_probes_sent > 0)
        blobmsg_add_u32(&reply_buf, "loss_percent", (100 *(probe->stats_probes_sent - probe->stats_probes_rcvd)) / probe->stats_probes_sent);
      else
        blobmsg_add_u32(&reply_buf, "loss_percent", 0);
      blobmsg_close_table(&reply_buf, c);
    }
  }

  ubus_send_reply(ctx, req, reply_buf.head);

  return UBUS_STATUS_OK;
}

int nm_probe_init(struct uci_context *uci, struct ubus_context *ubus)
{
  /* Initialize the probe registry */
  avl_init(&probe_registry, avl_strcmp, false, NULL);

  /* Get probe configuration */
  struct uci_package *pkg = uci_lookup_package(uci, "netmeasured");
  if (!pkg)
    uci_load(uci, "netmeasured", &pkg);
  if (!pkg) {
    syslog(LOG_ERR, "Missing netmeasured UCI configuration.");
    return -1;
  }

  struct uci_element *e;
  uci_foreach_element(&pkg->sections, e) {
    struct uci_section *s = uci_to_section(e);
    if (strcmp(s->type, "probe") != 0)
      continue;

    if (s->anonymous) {
      syslog(LOG_WARNING, "Ignoring anonymous probe UCI section, please name the probe!");
      continue;
    }

    /* TODO: Extract interface so we can register a hook that will initialize
             the probe only after the interface is brought up by netifd. */

    /* Extract address and port */
    const char *address, *port;
    address = uci_lookup_option_string(uci, s, "address");
    if (!address)
      continue;

    port = uci_lookup_option_string(uci, s, "port");
    if (!port)
      continue;

    /* Extract the interval */
    const char *interval;
    interval = uci_lookup_option_string(uci, s, "interval");
    if (!interval)
      continue;

    int interval_value;
    char *interval_err;
    errno = 0;
    interval_value = strtol(interval, &interval_err, 10);
    if (errno != 0 || interval_err == interval)
      continue;

    nm_create_probe(s->e.name, address, port, interval_value);
    break;
  }

  /* Initialize ubus methods */
  static const struct ubus_method nmd_methods[] = {
    UBUS_METHOD("get_probe", nm_handle_get_probe, nm_probe_policy),
    UBUS_METHOD("reset_probe", nm_handle_reset_probe, nm_probe_policy),
  };

  static struct ubus_object_type agent_type =
    UBUS_OBJECT_TYPE("netmeasured", nmd_methods);

  static struct ubus_object obj = {
    .name = "netmeasured",
    .type = &agent_type,
    .methods = nmd_methods,
    .n_methods = ARRAY_SIZE(nmd_methods),
  };

  return ubus_add_object(ubus, &obj);

  return 0;
}
