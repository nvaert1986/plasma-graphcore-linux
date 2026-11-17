# plasma-graphcore-linux

The shared Microsoft Graph core of a small suite of KDE Plasma apps for Microsoft 365:
[Graph Sync](https://github.com/nvaert1986/plasma-graphsync-linux) (OneDrive and SharePoint files)
and Graph Mail (mail, calendar, contacts and tasks for Exchange Online). Every app of the suite
uses this core, so one sign-in per organisation is enough for all of them.

**Status:** version 0.14.0, in development (0.13.0 is the current release). Fixes for a series go on its branch (`0.13`, `0.12`, `0.11`). The suite's apps
share major.minor. Within a series the library's ABI and API stay compatible (CONTRIBUTING.md); the
library is named after its series (`libgraphcore.so.0.12`).

> **Experimental: use at your own risk.** This is young software that handles your Microsoft 365
> sign-in. Not affiliated with or endorsed by Microsoft.

## What is in it

- **graphaccountsd**, the account service: a small user daemon that keeps the one sign-in of every
  app of the suite in the system keychain (KWallet or another Secret Service provider) and hands out
  short-lived access tokens over D-Bus (`io.github.nvaert1986.GraphAccounts`). It signs in through
  the browser or with a code entered on any device, refreshes tokens, tells the apps when a sign-in
  has expired, and signs out for every app at once. D-Bus starts it when an app first needs it.
- **libgraphcore**, the library the apps link: accounts (`graphaccountsrc`), the client of the
  account service, a Microsoft Graph client with throttling, retries and connectivity checks, the
  helpers the apps' sign-in screens need, and the apps' daily check for a new release on GitHub.
  CMake package `GraphCore`.
- **graphaccountsctl**: the account service from the command line (`list`, `login`, `logout`,
  `check`). It never prints a token.

Only the suite's own programs get tokens: the service checks which executable sent each D-Bus call.
This keeps other programs on the session bus from simply asking for one; it is not a security
boundary against code that already runs as the same user, which could read the open keychain too.
For development builds, `[Development] AllowedExecutables` in `~/.config/graphaccountsrc` lists extra
executables (absolute paths) or build directories (paths ending in `/`).

## Building

Requirements: CMake 3.25, Extra CMake Modules and KConfig 6.13 or newer, Qt 6.8 or newer (Core,
Network, NetworkAuth, Concurrent, DBus, Test), QtKeychain for Qt 6, and `dbus-run-session` for the
account service test.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
cmake --install build          # as root, or with -DCMAKE_INSTALL_PREFIX=~/.local for one user
```

Apps use it with `find_package(GraphCore)` and link `GraphCore::GraphCore`. The account service test
runs the service on a private D-Bus session without any keychain, so it never touches yours.

## Licence

GPL-3.0-or-later (LICENSES/).
