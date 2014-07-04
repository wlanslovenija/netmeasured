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
  /* Probe interval (in miliseconds) */
  int interval;
  /* Probe next scheduled run */
  struct uloop_timeout sched_timeout;
  /* Probe UDP socket */
  struct uloop_fd sock;
};

/* AVL tree containing all registered probes with probe name as their key */
static struct avl_tree probe_registry;

static void nm_probe_handler(struct uloop_fd *fd, unsigned int events)
{
  struct nm_probe *probe;

  /* Extract the probe where the event occurred */
  probe = container_of(fd, struct nm_probe, sock);
  /* Read the probe */
  char probe_data[128] = {0, };
  if (recv(probe->sock.fd, probe_data, sizeof(probe_data), 0) > 0) {
    probe->stats_probes_rcvd++;
  }
}

static void nm_probe_run(struct uloop_timeout *timeout)
{
  struct nm_probe *probe;

  /* Extract the probe where the timeout ocurred */
  probe = container_of(timeout, struct nm_probe, sched_timeout);
  /* Initiate probe */
  char probe_data[128] = {0, };
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

  uloop_fd_add(&probe->sock, ULOOP_READ);

  /* Schedule the probe */
  probe->sched_timeout.cb = nm_probe_run;
  uloop_timeout_set(&probe->sched_timeout, interval);

  syslog(LOG_INFO, "Created probe '%s' (%s:%s, interval %d msec).", name, address, port, interval);
}

int nm_probe_init(struct uci_context *uci, struct ubus_context *ubus)
{
  /* Initialize the probe registry */
  avl_init(&probe_registry, avl_strcmp, false, NULL);

  /* Get probe configuration */
  struct uci_package *pkg = NULL;
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

  return 0;
}
