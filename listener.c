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
#include <netmeasured/listener.h>

#include <libubox/usock.h>
#include <sys/socket.h>
#include <syslog.h>

/* Listener socket (currently only one is supported) */
static struct uloop_fd listener_sock;

static void nm_listener_handler(struct uloop_fd *fd, unsigned int events)
{
  struct sockaddr_storage peer_addr;
  socklen_t peer_addr_len;

  /* Read the probe */
  char probe_data[1024] = {0, };
  size_t bytes = recvfrom(fd->fd, probe_data, sizeof(probe_data), 0,
    (struct sockaddr*) &peer_addr, &peer_addr_len);
  if (bytes <= 0)
    return;

  /* Transmit the same probe back */
  sendto(fd->fd, probe_data, bytes, 0, (struct sockaddr*) &peer_addr, peer_addr_len);
}

static void nm_start_listener(const char *address, const char *port)
{
  listener_sock.cb = &nm_listener_handler;
  listener_sock.fd = usock(USOCK_UDP | USOCK_SERVER, address, port);
  if (listener_sock.fd < 0) {
    syslog(LOG_ERR, "Failed to initialize listener '%s:%s'.", address, port);
    return;
  }

  uloop_fd_add(&listener_sock, ULOOP_READ);
  syslog(LOG_INFO, "Started listener on '%s:%s'.", address, port);
}

int nm_listener_init(struct uci_context *uci, struct ubus_context *ubus)
{
  /* Get listener configuration */
  struct uci_package *pkg = NULL;
  uci_load(uci, "netmeasured", &pkg);
  if (!pkg) {
    syslog(LOG_ERR, "Missing netmeasured UCI configuration.");
    return -1;
  }

  struct uci_element *e;
  uci_foreach_element(&pkg->sections, e) {
    struct uci_section *s = uci_to_section(e);
    if (strcmp(s->type, "listener") != 0)
      continue;

    /* TODO: Extract interface so we can register a hook that will initialize
             the listener only after the interface is brought up by netifd. */

    /* Extract address and port */
    const char *address, *port;
    address = uci_lookup_option_string(uci, s, "address");
    if (!address)
      continue;

    port = uci_lookup_option_string(uci, s, "port");
    if (!port)
      continue;

    nm_start_listener(address, port);
    break;
  }

  return 0;
}
