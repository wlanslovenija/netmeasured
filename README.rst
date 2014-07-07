netmeasured
===========

Netmeasured is a simple network measurement daemon that can perform periodic
measurements (latency, packet loss) and provide reports via ubus.

Configuration
-------------

The listener and probes can be configured via UCI in the following manner::

  config listener
    option interface 'wan'
    option address '172.16.0.1'
    option port 9000

  config probe ping0
    option interface 'wan'
    option address '172.16.0.2'
    option port 9000
    option interval 1000


ubus API
--------

The daemon provides two method calls via the ubus API. The first method is ``get_probe``
which can be used to obtain the current status of a probe. In case no arguments are
specified, the status of all probes are returned. Usage is as follows::

  $ ubus call netmeasured get_probe "{ 'probe': 'ping0' }"
  {
    "ping0": {
      "name": "ping0",
      "interval": 1000,
      "sent": 318,
      "rcvd": 318
    }
  }

Parameters ``sent`` and ``rcvd`` specify the number of probes sent and the number of
probes successfully received.

The next method, ``reset_probe`` can be used to reset probe statistics and start a new
measurement. In this case, the ``probe`` argument is mandatory:::

  $ ubus call netmeasured reset_probe "{ 'probe': 'ping0' }"
  {
  }
