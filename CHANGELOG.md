# Changelog

## 0.13.0 (2026-11-17)

**Changed**
- Once the account service has used its own sign-in of an account, it removes the copies the apps
  stored before it existed (Graph Mail's and Graph Sync's own keychain entries). Versions of the apps
  from before 0.11 then have to sign in again.

## 0.12.0 (2026-11-17)

**New**
- UpdateChecker: the update check of the suite's apps (once a day GitHub's newest release of the
  app's repository, the announced and the skipped version kept in the app's state folder). Graph
  Sync's own check moved here; Graph Mail uses it too.

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
