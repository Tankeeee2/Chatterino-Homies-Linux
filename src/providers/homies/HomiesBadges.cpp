#include "HomiesBadges.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Emote.hpp"
#include "common/Outcome.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QThread>
#include <QUrl>

namespace chatterino {

HomiesBadges::HomiesBadges(bool loadOnConstruct)
{
    if (loadOnConstruct)
    {
        this->loadHomiesBadges();
    }
}

boost::optional<EmotePtr> HomiesBadges::getBadge(const UserId &id)
{
    std::shared_lock lock(this->mutex_);

    auto it = this->badgeMap.find(id.string);
    if (it != this->badgeMap.end() && it->second >= 0 &&
        static_cast<size_t>(it->second) < this->emotes.size())
    {
        return this->emotes[it->second];
    }
    return boost::none;
}

boost::optional<EmotePtr> HomiesBadges::getBadge2(const UserId &id)
{
    std::shared_lock lock(this->mutex_);

    auto it = this->badgeMap2.find(id.string);
    if (it != this->badgeMap2.end() && it->second >= 0 &&
        static_cast<size_t>(it->second) < this->emotes2.size())
    {
        return this->emotes2[it->second];
    }
    return boost::none;
}

boost::optional<EmotePtr> HomiesBadges::getBadge3(const UserId &id)
{
    std::shared_lock lock(this->mutex_);

    auto it = this->badgeMap3.find(id.string);
    if (it != this->badgeMap3.end() && it->second >= 0 &&
        static_cast<size_t>(it->second) < this->emotes3.size())
    {
        return this->emotes3[it->second];
    }
    return boost::none;
}

void HomiesBadges::loadHomiesBadges()
{
    static QUrl url("https://chatterinohomies.com/api/badges/list");

    NetworkRequest(url)
        .concurrent()
        .onSuccess([this](auto result) -> Outcome {
            auto jsonRoot = result.parseJson();

            std::vector<EmotePtr> newEmotes;
            std::unordered_map<QString, int> newBadgeMap;

            int index = 0;
            for (const auto &jsonBadge_ : jsonRoot.value("badges").toArray())
            {
                auto jsonBadge = jsonBadge_.toObject();
                auto emote = Emote{
                    EmoteName{},
                    ImageSet{Url{jsonBadge.value("image1").toString()},
                             Url{jsonBadge.value("image2").toString()},
                             Url{jsonBadge.value("image3").toString()}},
                    Tooltip{jsonBadge.value("tooltip").toString()}, Url{}};

                newEmotes.push_back(
                    std::make_shared<const Emote>(std::move(emote)));

                newBadgeMap[jsonBadge.value("userId").toString()] = index;
                ++index;
            }

            {
                std::unique_lock lock(this->mutex_);
                this->emotes = std::move(newEmotes);
                this->badgeMap = std::move(newBadgeMap);
            }

            return Success;
        })
        .execute();

    static QUrl url2("https://itzalex.github.io/badges");

    NetworkRequest(url2)
        .concurrent()
        .onSuccess([this](auto result) -> Outcome {
            auto jsonRoot = result.parseJson();

            std::vector<EmotePtr> newEmotes;
            std::unordered_map<QString, int> newBadgeMap;

            int index = 0;
            for (const auto &jsonBadge_ : jsonRoot.value("badges").toArray())
            {
                auto jsonBadge = jsonBadge_.toObject();
                auto emote = Emote{
                    EmoteName{},
                    ImageSet{Url{jsonBadge.value("image1").toString()},
                             Url{jsonBadge.value("image2").toString()},
                             Url{jsonBadge.value("image3").toString()}},
                    Tooltip{jsonBadge.value("tooltip").toString()}, Url{}};

                newEmotes.push_back(
                    std::make_shared<const Emote>(std::move(emote)));

                for (const auto &user : jsonBadge.value("users").toArray())
                {
                    newBadgeMap[user.toString()] = index;
                }
                ++index;
            }

            {
                std::unique_lock lock(this->mutex_);
                this->emotes2 = std::move(newEmotes);
                this->badgeMap2 = std::move(newBadgeMap);
            }

            return Success;
        })
        .execute();

    static QUrl url3("https://itzalex.github.io/badges2");

    NetworkRequest(url3)
        .concurrent()
        .onSuccess([this](auto result) -> Outcome {
            auto jsonRoot = result.parseJson();

            std::vector<EmotePtr> newEmotes;
            std::unordered_map<QString, int> newBadgeMap;

            int index = 0;
            for (const auto &jsonBadge_ : jsonRoot.value("badges").toArray())
            {
                auto jsonBadge = jsonBadge_.toObject();
                auto emote = Emote{
                    EmoteName{},
                    ImageSet{Url{jsonBadge.value("image1").toString()},
                             Url{jsonBadge.value("image2").toString()},
                             Url{jsonBadge.value("image3").toString()}},
                    Tooltip{jsonBadge.value("tooltip").toString()}, Url{}};

                newEmotes.push_back(
                    std::make_shared<const Emote>(std::move(emote)));

                for (const auto &user : jsonBadge.value("users").toArray())
                {
                    newBadgeMap[user.toString()] = index;
                }
                ++index;
            }

            {
                std::unique_lock lock(this->mutex_);
                this->emotes3 = std::move(newEmotes);
                this->badgeMap3 = std::move(newBadgeMap);
            }

            return Success;
        })
        .execute();
}
}  // namespace chatterino