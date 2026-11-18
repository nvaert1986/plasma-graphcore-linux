# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Build: rpmbuild -ba --define "_sourcedir $PWD" plasma-graphcore-linux.spec
# (with plasma-graphcore-linux-<version>.tar.gz next to it)

Name:           plasma-graphcore-linux
Version:        0.14.1
Release:        1%{?dist}
Summary:        Account service and shared core of the Microsoft 365 apps for KDE Plasma
License:        GPL-3.0-or-later
URL:            https://github.com/nvaert1986/plasma-graphcore-linux
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  gettext
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Concurrent)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6NetworkAuth)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6Test)
BuildRequires:  cmake(Qt6Keychain)
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(KF6KCMUtils)
BuildRequires:  cmake(KF6Notifications)

# Loaded at run time, so rpm cannot find them by itself: the QML modules of the System Settings page
Requires:       kf6-kirigami
Requires:       kf6-kcmutils
Requires:       qt6-qtdeclarative
# The keychain (Secret Service) that holds the sign-in, and System Settings for the accounts page
Recommends:     kf6-kwallet
Recommends:     plasma-systemsettings


# Built against the versions installed now, so they become the minimum: a system that was not
# updated first (dnf install ./file.rpm keeps what is "good enough") otherwise fails at run time with
# "undefined symbol" (0.14.0-1)
%{lua:
for name in ("qt6-qtbase qt6-qtdeclarative qt6-qtnetworkauth qtkeychain-qt6 kf6-kconfig kf6-kcoreaddons kf6-ki18n kf6-kcmutils kf6-knotifications"):gmatch("%S+") do
  local v = rpm.expand("%(rpm -q --qf '%%{version}' " .. name .. " 2>/dev/null)")
  if v ~= "" and not v:find("not installed") then
    print("Requires: " .. name .. rpm.expand("%{?_isa}") .. " >= " .. v .. "\n")
  end
end
}

%description
The sign-in shared by Graph Sync, Graph Mail and the other apps of the suite:
the account service graphaccountsd (D-Bus activated) keeps one sign-in per
account in the system keychain and hands out access tokens to the suite's
programs; System Settings > Microsoft 365 Accounts lists the accounts, adds
one, signs in again and signs out. Also the library the apps share
(libgraphcore, named after its release series). Not affiliated with Microsoft.

%package        devel
Summary:        Development files of the Microsoft Graph library of the Microsoft 365 apps
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       cmake(Qt6Core)
Requires:       cmake(Qt6Network)
Requires:       cmake(KF6Config)

%description    devel
Headers and the CMake package GraphCore, for building the apps of the suite.

%prep
%autosetup -p1

%build
%cmake -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install
%find_lang %{name}
%find_lang kcm_graphaccounts

%files -f %{name}.lang -f kcm_graphaccounts.lang
%license LICENSES/GPL-3.0-or-later.txt
%doc README.md CHANGELOG.md
%{_bindir}/graphaccountsctl
%{_bindir}/graphaccountsd
%{_libdir}/libgraphcore.so.0.14*
%{_qt6_plugindir}/plasma/kcms/systemsettings/kcm_graphaccounts.so
%{_datadir}/applications/kcm_graphaccounts.desktop
%{_datadir}/dbus-1/services/io.github.nvaert1986.GraphAccounts.service
%{_datadir}/knotifications6/plasma-graphcore-linux.notifyrc

%files devel
%{_includedir}/graphcore/
%{_libdir}/libgraphcore.so
%{_libdir}/cmake/GraphCore/

%changelog
* Wed Nov 18 2026 nvaert1986 <25772781+nvaert1986@users.noreply.github.com> - 0.14.1-1
- Add Account asks for the Client ID when none is configured; clear messages when no keychain
  answers or the account service cannot be started

* Wed Nov 18 2026 nvaert1986 <25772781+nvaert1986@users.noreply.github.com> - 0.14.0-2
- Require at least the Qt and KDE library versions the package was built against: on a
  system that was not updated first it failed with "undefined symbol"

* Tue Nov 17 2026 nvaert1986 <25772781+nvaert1986@users.noreply.github.com> - 0.14.0-1
- First packaged release: the account service, System Settings > Microsoft 365 Accounts, the
  system's proxy, clear error messages, one "Sign in again" notification for the suite
