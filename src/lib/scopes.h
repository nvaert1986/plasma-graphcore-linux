// SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

// Delegated Microsoft Graph permissions. Every app of the suite signs in with suite(), so one
// admin consent per tenant covers the mail client and the sync client together; Graph Chat also
// asks for chat() (its own token from the same sign-in).
namespace GraphCore::Scopes
{

// Asked for on every sign-in: the refresh token, the ID token and the user's own profile
inline QStringList signIn()
{
    return {QStringLiteral("offline_access"), QStringLiteral("openid"), QStringLiteral("profile"), QStringLiteral("User.Read")};
}

inline QStringList files()
{
    return {QStringLiteral("Files.ReadWrite.All"), QStringLiteral("Sites.ReadWrite.All")};
}

// Own and shared mailboxes; MailboxSettings also covers master categories and inbox rules
inline QStringList mail()
{
    return {QStringLiteral("Mail.ReadWrite"),
            QStringLiteral("Mail.ReadWrite.Shared"),
            QStringLiteral("Mail.Send"),
            QStringLiteral("Mail.Send.Shared"),
            QStringLiteral("MailboxSettings.ReadWrite")};
}

// Place.Read.All (room finder) needs admin consent
inline QStringList calendars()
{
    return {QStringLiteral("Calendars.ReadWrite"), QStringLiteral("Calendars.ReadWrite.Shared"), QStringLiteral("Place.Read.All")};
}

inline QStringList contacts()
{
    return {QStringLiteral("Contacts.ReadWrite"),
            QStringLiteral("Contacts.ReadWrite.Shared"),
            QStringLiteral("People.Read"),
            QStringLiteral("User.ReadBasic.All")};
}

// Shared covers To Do lists other people shared with the user
inline QStringList tasks()
{
    return {QStringLiteral("Tasks.ReadWrite"), QStringLiteral("Tasks.ReadWrite.Shared")};
}

// Asked for on its own, never at sign-in: a tenant without admin consent for it must keep working
// (AADSTS65001 would fail the whole token). The contact card's manager and direct reports.
inline QStringList directory()
{
    return signIn() + QStringList{QStringLiteral("User.Read.All")};
}

// Teams through Graph (Graph Chat): chats, channels, presence, meetings and their artifacts,
// approvals. Several need an administrator's consent
inline QStringList teams()
{
    return {QStringLiteral("Chat.ReadWrite"),
            QStringLiteral("ChatMessage.Send"),
            QStringLiteral("ChannelMessage.Send"),
            QStringLiteral("ChannelMessage.Read.All"),
            QStringLiteral("ChannelMessage.ReadWrite"),
            QStringLiteral("Team.ReadBasic.All"),
            QStringLiteral("Channel.ReadBasic.All"),
            QStringLiteral("TeamMember.Read.All"),
            QStringLiteral("ChannelMember.Read.All"),
            QStringLiteral("Presence.Read.All"),
            QStringLiteral("Presence.ReadWrite"),
            QStringLiteral("OnlineMeetings.ReadWrite"),
            QStringLiteral("OnlineMeetingArtifact.Read.All"),
            QStringLiteral("OnlineMeetingTranscript.Read.All"),
            QStringLiteral("OnlineMeetingRecording.Read.All"),
            QStringLiteral("ApprovalSolution.ReadWrite")};
}

// Asked for on its own, never at sign-in, like directory(): a tenant that has not approved the
// Teams permissions keeps its suite token. What Graph Chat uses: Teams, files for attachments,
// the calendar for meetings, people search
inline QStringList chat()
{
    return signIn() + teams() + files() + QStringList{QStringLiteral("Calendars.ReadWrite"), QStringLiteral("People.Read"), QStringLiteral("User.ReadBasic.All")};
}

inline QStringList suite()
{
    return signIn() + files() + mail() + calendars() + contacts() + tasks();
}

}
