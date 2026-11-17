# Changelog

## 0.11.0 (2026-11-17)

First version, numbered like the suite's apps that use it.

- libgraphcore: accounts (graphaccountsrc), a Microsoft Graph client with throttling, retries and
  connectivity, the sign-in helpers for the apps' setup screens, and the client of the account
  service (ServiceTokenProvider, AccountsClient). CMake package `GraphCore`.
- graphaccountsd: the account service. It keeps the one sign-in of every app of the suite in the
  system keychain, hands out access tokens over D-Bus only to the suite's own programs, signs in
  through the browser or with a code, signs out for every app, and takes over the sign-ins the apps
  stored before it existed.
- graphaccountsctl: the account service from the command line (it never prints a token).
