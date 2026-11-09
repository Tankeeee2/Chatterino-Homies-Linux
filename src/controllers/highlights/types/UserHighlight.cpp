// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/types/UserHighlight.hpp"

#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "controllers/highlights/types/Common.hpp"

namespace chatterino::highlights {

UserHighlight::UserHighlight(QStringView _id)
    : id(_id)
{
}

HighlightCheck UserHighlight::buildCheck() const
{
    using H = std::remove_pointer_t<decltype(this)>;
    using Params = HighlightCheckParams;

    return {
        [highlight = *this](const Params &p) -> std::optional<HighlightResult> {
            if (!highlight.matchesChannel(p.channel))
            {
                return std::nullopt;
            }

            if (highlight.username.compare(p.senderName, Qt::CaseInsensitive) !=
                0)
            {
                return std::nullopt;
            }

            return highlight.outcome.makeSimpleResult<H>();
        },
    };
}

bool UserHighlight::matchesChannel(const QString &channel) const
{
    return matchesChannelFilter(this->globalHighlight, this->channels,
                                this->excludedChannels, channel);
}

QDebug operator<<(QDebug dbg, const UserHighlight &v)
{
    dbg.nospace() << "UserHighlight("
                  << "name:" << v.name << ',' << "username:" << v.username
                  << ',' << "enabled:" << v.enabled << ','
                  << "outcome:" << v.outcome << ')';

    return dbg;
}

}  // namespace chatterino::highlights
