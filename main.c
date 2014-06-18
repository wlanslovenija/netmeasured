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
#include <unistd.h>
#include <libubox/uloop.h>
#include <libubus.h>
#include <uci.h>
#include <signal.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

/* Global ubus connection context */
static struct ubus_context *ubus;
/* Global UCI context */
static struct uci_context *uci;

/**
 * Nodewatcher agent entry point.
 */
int main(int argc, char **argv)
{
  struct stat s;
  const char *ubus_socket = NULL;
  int log_option = 0;
  int c;

  while ((c = getopt(argc, argv, "s:")) != -1) {
    switch (c) {
      case 's': ubus_socket = optarg; break;
      case 'f': log_option |= LOG_PERROR; break;
      default: break;
    }
  }

  /* Open the syslog facility */
  openlog("netmeasured", log_option, LOG_DAEMON);

  /* Create directory for temporary run files */
  if (stat("/var/run/netmeasured", &s))
    mkdir("/var/run/netmeasured", 0700);

  umask(0077);

  /* Setup signal handlers */
  signal(SIGPIPE, SIG_IGN);
  /* TODO: Handle SIGHUP to reload? */

  /* Initialize event loop */
  uloop_init();

  /* Attempt to establish connection to ubus daemon */
  ubus = ubus_connect(ubus_socket);
  if (!ubus) {
    fprintf(stderr, "ERROR: Failed to connect to ubus!");
    return -1;
  }

  ubus_add_uloop(ubus);

  /* Initialize UCI context */
  uci = uci_alloc_context();
  if (!uci) {
    fprintf(stderr, "ERROR: Failed to initialize UCI!\n");
    return -1;
  }

  /* TODO */

  /* Enter the event loop */
  uloop_run();
  ubus_free(ubus);
  uci_free_context(uci);
  uloop_done();

  return 0;
}
