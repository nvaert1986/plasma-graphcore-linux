# plasma-graphcore-linux

The shared Microsoft Graph core of a small suite of KDE Plasma apps for Microsoft 365 (mail and
calendar, file sync): accounts, a Microsoft Graph client with throttling and retries, and (in
progress) one account service that owns the sign-in, so every app of the suite shares one sign-in
per organisation.

Status: in development, no release yet. There is no stable API or ABI (SOVERSION 0).

## Building

Requirements: CMake 3.25, Extra CMake Modules and KConfig 6.13 or newer, Qt 6.8 or newer (Core,
Network, NetworkAuth, Concurrent, DBus, Test), QtKeychain for Qt 6.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
cmake --install build
```

Apps use it with `find_package(GraphCore)` and link `GraphCore::GraphCore`.

## Licence

GPL-3.0-or-later (LICENSES/).
