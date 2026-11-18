// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

// Microsoft 365 Accounts: the work accounts every app of the suite uses (one sign-in for all)
KCMUtils.ScrollViewKCM {
    id: root

    // Names from an organisation's directory are text, never markup
    function plain(text) {
        return String(text ?? "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    header: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: !kcm.serviceAvailable
            type: Kirigami.MessageType.Error
            text: i18n("The account service (graphaccountsd) does not answer. Reinstall plasma-graphcore-linux, or sign out of the desktop and in again.")
        }
        // Errors of a sign-in again or a sign-out started from the list (the dialog shows its own)
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: kcm.lastError.length > 0 && !addDialog.visible
            type: Kirigami.MessageType.Error
            text: kcm.lastError
            actions: [
                Kirigami.Action {
                    visible: kcm.adminConsentLink.length > 0
                    icon.name: "edit-copy"
                    text: i18n("Copy Approval Link")
                    onTriggered: kcm.copyText(kcm.adminConsentLink)
                }
            ]
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: kcm.busy && !addDialog.visible
            type: Kirigami.MessageType.Information
            text: kcm.deviceCode.length > 0
                ? i18n("Open %1 on any device, enter the code %2 and sign in with your work account.", kcm.deviceCodeAddress, kcm.deviceCode)
                : i18n("Sign in in the browser. This page continues by itself.")
            actions: [
                Kirigami.Action {
                    visible: kcm.deviceCode.length > 0
                    icon.name: "edit-copy"
                    text: i18n("Copy Code")
                    onTriggered: kcm.copyText(kcm.deviceCode)
                },
                Kirigami.Action {
                    icon.name: "dialog-cancel"
                    text: i18n("Cancel")
                    onTriggered: kcm.cancelSignIn()
                }
            ]
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: i18n("One sign-in for Graph Mail, Graph Sync and the other Microsoft 365 apps of the suite. Signing out here signs the account out of all of them.")
        }
    }

    view: ListView {
        id: list
        model: kcm.accounts
        reuseItems: true

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0 && kcm.serviceAvailable
            icon.name: "system-users"
            text: i18n("No accounts yet")
            explanation: i18n("Add your work account to use the apps of the suite.")
            helpfulAction: Kirigami.Action {
                icon.name: "list-add-user"
                text: i18n("Add Account…")
                onTriggered: addDialog.openClean()
            }
        }

        delegate: QQC2.ItemDelegate {
            id: item
            required property var modelData
            width: ListView.view.width
            down: false
            highlighted: false
            hoverEnabled: false

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    Layout.preferredWidth: Kirigami.Units.iconSizes.large
                    Layout.preferredHeight: Kirigami.Units.iconSizes.large
                    source: "im-user"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 4
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: item.modelData.displayName || item.modelData.userPrincipalName
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: item.modelData.organization.length > 0
                            ? i18nc("%1 address, %2 organisation", "%1 · %2", item.modelData.userPrincipalName, item.modelData.organization)
                            : item.modelData.userPrincipalName
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: item.modelData.stateText.length > 0
                        font: Kirigami.Theme.smallFont
                        color: item.modelData.state === "signedIn" ? Kirigami.Theme.positiveTextColor
                             : item.modelData.state === "signedOut" ? Kirigami.Theme.disabledTextColor
                             : Kirigami.Theme.negativeTextColor
                        text: item.modelData.stateText
                    }
                }
                QQC2.Button {
                    visible: item.modelData.state !== "signedIn"
                    enabled: !kcm.busy && kcm.serviceAvailable
                    icon.name: "im-user-online"
                    text: i18n("Sign In")
                    onClicked: kcm.signInAgain(item.modelData.id, false)
                }
                QQC2.ToolButton {
                    icon.name: "overflow-menu"
                    enabled: kcm.serviceAvailable
                    QQC2.ToolTip.text: i18n("More")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: menu.popup()
                    QQC2.Menu {
                        id: menu
                        QQC2.MenuItem {
                            icon.name: "im-user-online"
                            text: i18n("Sign In Again")
                            enabled: !kcm.busy
                            onTriggered: kcm.signInAgain(item.modelData.id, false)
                        }
                        QQC2.MenuItem {
                            icon.name: "dialog-password"
                            text: i18n("Sign In with a Code")
                            enabled: !kcm.busy && kcm.browserSignIn
                            visible: kcm.browserSignIn
                            onTriggered: kcm.signInAgain(item.modelData.id, true)
                        }
                        QQC2.MenuSeparator {}
                        QQC2.MenuItem {
                            icon.name: "system-log-out"
                            text: i18n("Sign Out…")
                            enabled: item.modelData.state !== "signedOut"
                            onTriggered: {
                                signOutDialog.accountId = item.modelData.id;
                                signOutDialog.address = item.modelData.userPrincipalName;
                                signOutDialog.open();
                            }
                        }
                    }
                }
            }
        }
    }

    footer: RowLayout {
        Item {
            Layout.fillWidth: true
        }
        QQC2.Button {
            icon.name: "list-add-user"
            text: i18n("Add Account…")
            enabled: kcm.serviceAvailable && !kcm.busy
            onClicked: addDialog.openClean()
        }
    }

    Kirigami.PromptDialog {
        id: signOutDialog
        objectName: "signOutDialog"
        property string accountId
        property string address
        title: i18n("Sign Out?")
        maximumWidth: Kirigami.Units.gridUnit * 24
        subtitle: i18n("%1 is signed out of every app of the suite: mail, files and the others stop until you sign in again. Nothing is deleted.", root.plain(address))
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                icon.name: "system-log-out"
                text: i18n("Sign Out")
                onTriggered: {
                    kcm.signOut(signOutDialog.accountId);
                    signOutDialog.close();
                }
            },
            Kirigami.Action {
                icon.name: "dialog-cancel"
                text: i18n("Cancel")
                onTriggered: signOutDialog.close()
            }
        ]
    }

    Kirigami.Dialog {
        id: addDialog
        objectName: "addDialog"
        title: i18n("Add Account")
        preferredWidth: Kirigami.Units.gridUnit * 26
        padding: Kirigami.Units.largeSpacing
        standardButtons: Kirigami.Dialog.NoButton

        // A clean form, not the result of the last attempt
        function openClean() {
            kcm.clearResult();
            emailField.text = "";
            ownRegistration.checked = false;
            clientField.text = "";
            open();
            emailField.forceActiveFocus();
        }
        // No registration on this computer yet (the first account): the Client ID field is shown by
        // itself and required; otherwise only behind the checkbox (another organisation's own)
        readonly property bool askRegistration: !kcm.registrationKnown || ownRegistration.checked
        readonly property bool canStart: emailField.text.trim().length > 0 && (kcm.registrationKnown || clientField.text.trim().length > 0)

        function start(withCode) {
            kcm.addAccount(emailField.text, addDialog.askRegistration ? clientField.text : "", withCode);
        }

        onRejected: kcm.cancelSignIn()

        Connections {
            target: kcm
            function onSignInDone(ok) {
                if (ok && addDialog.visible) {
                    addDialog.close();
                }
            }
        }

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: kcm.lastError.length > 0
                type: Kirigami.MessageType.Error
                text: kcm.lastError
                actions: [
                    Kirigami.Action {
                        visible: kcm.adminConsentLink.length > 0
                        icon.name: "internet-services"
                        text: i18n("Open Approval Page")
                        onTriggered: Qt.openUrlExternally(kcm.adminConsentLink)
                    },
                    Kirigami.Action {
                        visible: kcm.adminConsentLink.length > 0
                        icon.name: "edit-copy"
                        text: i18n("Copy Link")
                        onTriggered: kcm.copyText(kcm.adminConsentLink)
                    }
                ]
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                enabled: !kcm.busy

                QQC2.TextField {
                    id: emailField
                    Kirigami.FormData.label: i18n("Work e-mail address:")
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                    placeholderText: i18n("name@organization.com")
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                    onAccepted: addDialog.start(false)
                }
                QQC2.CheckBox {
                    id: ownRegistration
                    visible: kcm.registrationKnown
                    text: i18n("This organization has its own app registration")
                }
                QQC2.TextField {
                    id: clientField
                    Kirigami.FormData.label: i18n("Client ID:")
                    visible: addDialog.askRegistration
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                    placeholderText: "00000000-0000-0000-0000-000000000000"
                    font.family: "monospace"
                }
                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    text: !kcm.registrationKnown
                        ? i18n("The first account on this computer needs the Client ID of your organisation's app registration (or the setup text). Your IT administrator has it; the other apps of the suite use it too.")
                        : ownRegistration.checked
                        ? i18n("The client ID of the app registration in that organization (or the setup text), from its IT administrator.")
                        : i18n("Signs in with the same app registration as your other accounts. Its organization must have approved it.")
                }
            }

            // Waiting: the browser, or the code to enter on another device
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: kcm.busy
                type: Kirigami.MessageType.Information
                text: kcm.deviceCode.length > 0
                    ? i18n("Open %1 on any device, enter the code below and sign in with your work account. This window continues by itself.", kcm.deviceCodeAddress)
                    : i18n("Sign in in the browser. This window continues by itself.")
                actions: [
                    Kirigami.Action {
                        visible: kcm.deviceCode.length > 0
                        icon.name: "internet-services"
                        text: i18n("Open Page")
                        onTriggered: Qt.openUrlExternally(kcm.deviceCodeAddress)
                    }
                ]
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                visible: kcm.deviceCode.length > 0
                QQC2.TextField {
                    readOnly: true
                    text: kcm.deviceCode
                    font.family: "monospace"
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                    horizontalAlignment: Text.AlignHCenter
                    implicitWidth: Kirigami.Units.gridUnit * 10
                }
                QQC2.Button {
                    icon.name: "edit-copy"
                    text: i18n("Copy Code")
                    onClicked: kcm.copyText(kcm.deviceCode)
                }
            }

            // The buttons in the content: a footer with buttons that come and go lays out badly
            RowLayout {
                Layout.alignment: Qt.AlignRight
                QQC2.Button {
                    visible: !kcm.busy
                    enabled: addDialog.canStart
                    icon.name: "im-user-online"
                    text: i18n("Sign In")
                    onClicked: addDialog.start(false)
                }
                QQC2.Button {
                    visible: !kcm.busy && kcm.browserSignIn
                    enabled: addDialog.canStart
                    icon.name: "dialog-password"
                    text: i18n("Sign In with a Code")
                    onClicked: addDialog.start(true)
                }
                QQC2.Button {
                    icon.name: "dialog-cancel"
                    text: i18n("Cancel")
                    onClicked: {
                        kcm.cancelSignIn();
                        addDialog.close();
                    }
                }
            }
        }
    }
}
