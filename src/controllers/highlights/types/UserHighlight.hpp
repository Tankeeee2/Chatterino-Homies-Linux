// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/types/Common.hpp"
#include "controllers/highlights/types/Outcome.hpp"
#include "util/RapidjsonHelpers.hpp"

#include <pajlada/serialize/common.hpp>
#include <QColor>
#include <QDebug>
#include <QStringView>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <cassert>
#include <optional>
#include <vector>

class QIcon;

namespace chatterino::highlights {

/// User-created highlight matching messages based on the sender's username
struct UserHighlight {
    static constexpr QStringView TYPE = u"user";
    static constexpr QStringView TYPE_NAME = u"User";
    static constexpr QStringView ICON_RESOURCE = u":/settings/accounts.svg";

    static constexpr bool ENABLED_BY_DEFAULT = true;
    static constexpr bool SHOW_IN_MENTIONS_DEFAULT = true;
    static constexpr bool ALERT_DEFAULT = true;
    static constexpr QColor BACKGROUND_COLOR_DEFAULT = QColor(127, 63, 73, 127);

    UserHighlight(QStringView _id);

    QString getDefaultName() const
    {
        return this->username;
    }

    QStringView getID() const
    {
        return this->id;
    }

    bool operator==(const UserHighlight &other) const = default;

    QString getUsername() const
    {
        return this->username;
    }
    void setUsername(const QString &newValue)
    {
        this->username = newValue;
    }

    /// The display name/pretty name of this highlight.
    /// If empty, we will try to auto-generate something that makes sense (e.g. "Text contains 'foo'")
    QString name;

    std::optional<bool> enabled;

    /// If true (default), this highlight applies to every channel.
    bool globalHighlight = true;

    /// Channels this highlight applies to when globalHighlight is false.
    std::vector<QString> channels;

    /// Channels this highlight never applies to.
    std::vector<QString> excludedChannels;

    /// Returns true if this highlight applies to the given channel.
    /// An empty channel name means "unknown", in which case the filter passes.
    bool matchesChannel(const QString &channel) const;

    Outcome outcome = Outcome::create<UserHighlight>();

protected:
    /// Contains the username to match on (e.g. "forsen" or "spaenny")
    QString username;

public:
    HighlightCheck buildCheck() const;

protected:
    template <typename Type, typename RJValue>
    friend struct pajlada::Serialize;

    template <typename Type, typename RJValue, typename Enable>
    friend struct pajlada::Deserialize;

    friend class ConfigureDialog;

    /// Unique identifier for this highlight.
    /// This should be a random UUID
    QString id;

    friend QDebug operator<<(QDebug dbg, const UserHighlight &v);

    bool isValid() const
    {
        return !this->getUsername().isEmpty();
    }
};

}  // namespace chatterino::highlights

namespace pajlada {

template <>
struct Serialize<chatterino::highlights::UserHighlight> {
    using H = chatterino::highlights::UserHighlight;

    static rapidjson::Value get(const H &value,
                                rapidjson::Document::AllocatorType &a)
    {
        using namespace chatterino;

        rapidjson::Value ret(rapidjson::kObjectType);

        rj::set(ret, "id", value.id, a);
        rj::set(ret, "type", H::TYPE, a);
        rj::setOptionally(ret, "name", value.name, a);
        rj::setOptionally(ret, "enabled", value.enabled, a);

        rj::setOptionally(ret, "username", value.username, a);

        // Only persist the per-channel filter when it is actually in use, so
        // highlights that apply to every channel keep a minimal representation.
        if (!value.globalHighlight || !value.channels.empty() ||
            !value.excludedChannels.empty())
        {
            rj::set(ret, "global", value.globalHighlight, a);

            rapidjson::Value channels(rapidjson::kArrayType);
            for (const auto &channel : value.channels)
            {
                rj::add(channels, channel, a);
            }
            rj::set(ret, "channels", channels, a);

            rapidjson::Value excludedChannels(rapidjson::kArrayType);
            for (const auto &channel : value.excludedChannels)
            {
                rj::add(excludedChannels, channel, a);
            }
            rj::set(ret, "excludedChannels", excludedChannels, a);
        }

        value.outcome.serialize(ret, a);

        return ret;
    }
};

template <>
struct Deserialize<chatterino::highlights::UserHighlight> {
    using H = chatterino::highlights::UserHighlight;

    static H get(const rapidjson::Value &value, bool *error = nullptr)
    {
        if (!value.IsObject())
        {
            PAJLADA_REPORT_ERROR(error)
            return {u"invalid"};
        }

        if (!chatterino::highlights::matchesType(value, H::TYPE))
        {
            PAJLADA_REPORT_ERROR(error)
            return {u"invalid"};
        }

        QString id;
        if (!chatterino::rj::getSafe(value, "id", id))
        {
            PAJLADA_REPORT_ERROR(error)
            return {u"invalid"};
        }

        H h(id);

        chatterino::rj::getSafe(value, "name", h.name);
        chatterino::rj::getSafe(value, "enabled", h.enabled);

        chatterino::rj::getSafe(value, "username", h.username);
        chatterino::rj::getSafe(value, "global", h.globalHighlight);

        auto readChannels = [&value](const char *key,
                                     std::vector<QString> &out) {
            if (!value.HasMember(key) || !value[key].IsArray())
            {
                return;
            }

            for (const auto &channel : value[key].GetArray())
            {
                if (channel.IsString())
                {
                    out.emplace_back(QString::fromUtf8(
                        channel.GetString(),
                        static_cast<int>(channel.GetStringLength())));
                }
            }
        };

        readChannels("channels", h.channels);
        if (value.HasMember("excludedChannels"))
        {
            readChannels("excludedChannels", h.excludedChannels);
        }
        else
        {
            readChannels("ExcludedChannels", h.excludedChannels);
        }

        h.outcome.deserialize(value);

        return h;
    }
};

}  // namespace pajlada

// QDebug operator<<(QDebug dbg, const chatterino::UserHighlight &v);
