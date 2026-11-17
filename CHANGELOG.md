# Changelog

## 0.14.0 (in development)

**New**
- The system's proxy (System Settings > Network > Proxy: manual, environment variables, PAC or
  automatic) for every sign-in and Graph request of the service and the suite's apps
  (`GraphCore::Proxy::useSystemSettings`).
- Graph errors and the service's problems as clear messages that say what to do, the same in every
  app (`GraphResponse::userMessage`, `GraphCore::Errors`), with Dutch translations: the core has its
  own translation catalog now (`plasma-graphcore-linux`).
- `Connectivity::Watcher`: signals when the network comes back or a connection becomes metered.
- One "Sign in again" notification per account for the whole suite, from the account service, with
  a Sign In button (the account's address filled in); "Approval needed" when an administrator has to
  approve the apps. It closes by itself once the account works again. The apps no longer show their own.

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
