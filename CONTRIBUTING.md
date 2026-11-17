# Contributing

## Versions and branches

The suite's apps (Graph Sync, Graph Mail) and this core keep one version series.

- The suite shares **major.minor**; the patch number may differ per app.
- Within a release series (0.11.x) the library's ABI and API, the account service's D-Bus
  interface, and the configuration formats stay compatible. A patch release holds fixes, and small
  features only when they change none of these. New or changed library API (also only added
  functions: an app built against 0.11.1 must still run on 0.11.0), D-Bus methods and file formats
  wait for the next minor (0.12.0) or major (1.0.0).
- Each series has a maintenance branch named after it (`0.11`). Fixes go there and are merged into
  `main`; `main` holds the next series.
- libgraphcore's SOVERSION is major.minor (`libgraphcore.so.0.11`), and `find_package(GraphCore 0.11)`
  only accepts 0.11.x, so an app is never built against or loads another series by accident.
- The D-Bus interface carries its own number (`io.github.nvaert1986.GraphAccounts1`). An incompatible
  change gets a new interface (`GraphAccounts2`) next to the old one, never a silent change.

## Building and tests

See README.md. `ctest --test-dir build` must pass; the account service test needs `dbus-run-session`.
