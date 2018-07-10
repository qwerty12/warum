# Warum

GoodbyeDPI's ability to "set HTTPS\[' packets\] fragmentation" works great on Windows to bypass blocking attempts on HTTPS sites. An AutoHotkey script allowed for its quick en/disabling without needing to accept UAC prompts/entering passwords. Here's an attempt to bring a similar experience for the Linux desktop.

## Features

* Sets window size of packets sent to it via NFQUEUE in userspace
* Can be disabled/enabled over DBus
     * A simple pure-Qt5-based tray icon can be used (well, in KDE and XFCE at least) to quickly change warum's state with no fuss

## Downsides

* Only provides an equivalent to GDPI's `-e` option (GDPI wouldn't work for HTTP sites here, so that was the only functionality copied)
* No IPv6 support
* This is made with desktop Linux in mind, not routers etc. so it happily uses GLib and prefers to be made available over DBus (though without auto-activation)
    * There is no API to add iptables rules so `warum` proper doesn't try; it blindly assumes the appropriate rules are present and nor does it make any attempt to clear the rules on exit.  
         The use of `--queue-bypass` in the iptables rule allows matching packets to be blindly accepted even if a program isn't attached to the pertaining queue. `--queue-bypass` is "available since Linux kernel 2.6.39" and "broken from kernel 3.10 to 3.12" - on desktop Linux, this shouldn't be a problem. (The provided systemd .service attempts to have the rules added and cleared before starting and exiting, respectively.)
* Code is probably of questionable quality ¯\\_()_/¯

(I should point out that zapret's nfqws can already do what warum does, but with a far smaller footprint. You can use that instead with the iptables rules here.)

## Requirements

* cmake
* libnetfilter_queue (both the userspace library and the corresponding kernel feature enabled/available as a module)
* GLib
* Optionally for the tray icon: Qt 5
* `iptables` and `ipset` programs installed

## Installation

```
git clone https://github.com/qwerty12/warum.git
cd warum
mkdir build
cd build
# Remove -DWARUMTRAY=ON to ignore building of the tray frontend
# Specify -DSYSTEMD=OFF below to exclude installing the systemd unit
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DWARUMTRAY=ON
make
sudo make install
```

## Configuration

Not much to talk about here. `warum` has two main command-line arguments:

| Argument    | Description    |
| --- | --- |
| --qnum=`N`    | Number of the NFQUEUE rule to attach to. Must match the number of the corresponding iptables rule    |
| --wsize=`N`    | Set fragmentation of HTTPS packets to this number    |

With DBus support enabled, the following arguments can be additionally passed to warum:

| Argument    | Description    |
| --- | --- |
| --dbus    | Enable remote control via DBus    |
| --disabled    | Start without attempting to attach to the NF queue (it makes no sense to have this set without enabling the DBus functionality for obvious reasons)   |

When using the systemd service file, you can set the options by running
`sudo systemctl edit warum.service`
and shoving the following in and modifying as necessary:

```
[Service]
Environment="NFQUEUE_QNUM=200" "WINDOW_SIZE=40" "DBUS_ARGS=--dbus -d"
```
(`"DBUS_ARGS=--dbus -d"` can be removed if your build doesn't support DBus)

## Running

### With systemd+DBus (recommended)

`systemctl enable warum --now`

and

`warumtray`

By default, warum will be started disabled. You can enable it by middle-clicking warumtray's tray icon or by bringing up its context menu. warumtray does not have a desktop file - run it from the terminal and configure it to autostart how you wish.

or

`dbus-send --system --dest=pk.qwerty12.warum --print-reply / org.freedesktop.DBus.Properties.Get string:pk.qwerty12.warum string:Enabled` to get warum's current state
`dbus-send --system --dest=pk.qwerty12.warum --print-reply / org.freedesktop.DBus.Properties.Set string:pk.qwerty12.warum string:Enabled variant:boolean:true` to enable it
`dbus-send --system --dest=pk.qwerty12.warum --print-reply / org.freedesktop.DBus.Properties.Set string:pk.qwerty12.warum string:Enabled variant:boolean:false` to disable it

### With systemd without DBus

I haven't tested this scenario out, in all honesty. Starting the systemd service should automatically start warum; stop it to shutdown warum.

### Starting warum manually

```
# /usr/libexec/warum/iptables_rules.sh add 200
# /usr/sbin/warum --qnum=200 --wsize=40
... when done with warum
# /usr/libexec/warum/iptables_rules.sh remove 200
```

## Acknowledgments

* Much of the netfilter packet handling code comes from zapret's nfqws
* boltd's hack for having more control over DBus properties from `gdbus-codegen`'s generated code was nicer than my original one
* The iptables rule is, as best as I could manage, a straight conversion of the pertinent GoodbyeDPI WinDivert filter. I tried to make it as efficient as possible, striving to ensure only the most relevant packets are sent into userspace
