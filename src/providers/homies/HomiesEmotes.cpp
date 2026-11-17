#include "HomiesEmotes.hpp"

#include "common/Common.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/Outcome.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"

#include "util/PostToThread.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace chatterino {
namespace {
    const QString CHANNEL_HAS_NO_EMOTES(
        "This channel has no Homies channel emotes.");

    const QString emoteLinkFormat("https://7tv.app/emotes/%1");

    Url getEmoteLink(const EmoteId &id, const QString &emoteScale)
    {
        const QString urlTemplate("https://itzalex.github.io/emote/%1/%2");

        return {urlTemplate.arg(id.string, emoteScale)};
    }

    EmotePtr cachedOrMake(Emote &&emote, const EmoteId &id)
    {
        static std::unordered_map<EmoteId, std::weak_ptr<const Emote>> cache;
        static std::mutex mutex;

        return cachedOrMakeEmotePtr(std::move(emote), cache, mutex, id);
    }

    struct CreateEmoteResult {
        EmoteId id;
        EmoteName name;
        Emote emote;
    };

    CreateEmoteResult createEmote(QJsonValue jsonEmote, bool isGlobal)
    {
        auto id = EmoteId{jsonEmote.toObject().value("id").toString()};
        auto name = EmoteName{jsonEmote.toObject().value("name").toString()};
        auto author =
            EmoteAuthor{jsonEmote.toObject().value("author").toString()};
        bool zeroWidth = jsonEmote.toObject().value("zerowidth").toBool();

        auto emote = Emote(
            {name,
             ImageSet{Image::fromUrl(getEmoteLink(id, "1x"), 1),
                      Image::fromUrl(getEmoteLink(id, "2x"), 0.66),
                      Image::fromUrl(getEmoteLink(id, "3x"), 0.33)},
             Tooltip{QString("%1<br>%2 Homies Emote<br>By: %3")
                         .arg(name.string, (isGlobal ? "Global" : "Channel"),
                              author.string)},
             Url{emoteLinkFormat.arg(id.string)}, zeroWidth});

        auto result = CreateEmoteResult({id, name, emote});
        return result;
    }

    std::pair<Outcome, EmoteMap> parseGlobalEmotes(
        const QJsonArray &jsonEmotes, const EmoteMap &currentEmotes)
    {
        auto emotes = EmoteMap();

        for (const auto jsonEmote : jsonEmotes)
        {
            auto emote = createEmote(jsonEmote, true);
            emotes[emote.name] =
                cachedOrMakeEmotePtr(std::move(emote.emote), currentEmotes);
        }

        return {Success, std::move(emotes)};
    }

    EmoteMap parseChannelEmotes(const QJsonObject &root)
    {
        auto emotes = EmoteMap();

        auto jsonEmotes = root.value("emotes").toArray();
        for (auto jsonEmote_ : jsonEmotes)
        {
            auto jsonEmote = jsonEmote_.toObject();

            auto emote = createEmote(jsonEmote, false);

            emotes[emote.name] = cachedOrMake(std::move(emote.emote), emote.id);
        }

        return emotes;
    }

    struct PendingChannelRequest {
        std::weak_ptr<Channel> channel;
        QString channelId;
        std::function<void(EmoteMap &&)> callback;
        bool manualRefresh;
    };

    struct HomiesChannelCache {
        static constexpr auto CACHE_TTL = std::chrono::minutes(5);

        std::mutex mutex;
        bool inFlight = false;
        std::chrono::steady_clock::time_point lastFetched{};
        std::unordered_map<QString, QJsonObject> channelEmotes;
        std::vector<PendingChannelRequest> pending;
    };

    HomiesChannelCache &getChannelCache()
    {
        static HomiesChannelCache cache;
        return cache;
    }

    void finishChannelRequest(PendingChannelRequest &&req, EmoteMap &&emoteMap)
    {
        bool hasEmotes = !emoteMap.empty();
        postToGuiThread([req = std::move(req), emoteMap = std::move(emoteMap),
                         hasEmotes]() mutable {
            auto shared = req.channel.lock();
            if (!shared)
            {
                return;
            }
            if (hasEmotes)
            {
                req.callback(std::move(emoteMap));
            }
            if (req.manualRefresh)
            {
                if (hasEmotes)
                {
                    shared->addSystemMessage("Homies channel emotes reloaded.");
                }
                else
                {
                    shared->addSystemMessage(CHANNEL_HAS_NO_EMOTES);
                }
            }
        });
    }
}  // namespace

HomiesEmotes::HomiesEmotes()
    : global_(std::make_shared<EmoteMap>())
{
}

std::shared_ptr<const EmoteMap> HomiesEmotes::emotes() const
{
    return this->global_.get();
}

std::optional<EmotePtr> HomiesEmotes::emote(const EmoteName &name) const
{
    auto emotes = this->global_.get();
    auto it = emotes->find(name);

    if (it != emotes->end())
    {
        return it->second;
    }
    return std::nullopt;
}

void HomiesEmotes::loadEmotes()
{
    if (!Settings::instance().enableHomiesGlobalEmotes)
    {
        this->global_.set(EMPTY_EMOTE_MAP);
        return;
    }

    NetworkRequest(apiUrl)
        .concurrent()
        .onSuccess([this](NetworkResult result) -> Outcome {
            QJsonArray parsedEmotes = result.parseJson()
                                          .value("data")
                                          .toObject()
                                          .value("global_emotes")
                                          .toArray();

            auto pair = parseGlobalEmotes(parsedEmotes, *this->global_.get());
            if (pair.first)
                this->global_.set(
                    std::make_shared<EmoteMap>(std::move(pair.second)));
            return pair.first;
        })
        .execute();
}

void HomiesEmotes::loadChannel(std::weak_ptr<Channel> channel,
                               const QString &channelId,
                               std::function<void(EmoteMap &&)> callback,
                               bool manualRefresh)
{
    auto &cache = getChannelCache();
    PendingChannelRequest req{
        .channel = std::move(channel),
        .channelId = channelId,
        .callback = std::move(callback),
        .manualRefresh = manualRefresh,
    };

    bool shouldFetch = false;
    std::optional<QJsonObject> cachedChannelObj;
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        auto now = std::chrono::steady_clock::now();
        if (!manualRefresh &&
            cache.lastFetched.time_since_epoch().count() > 0 &&
            (now - cache.lastFetched) < HomiesChannelCache::CACHE_TTL)
        {
            auto it = cache.channelEmotes.find(channelId);
            if (it != cache.channelEmotes.end())
            {
                cachedChannelObj = it->second;
            }
            else
            {
                cachedChannelObj = QJsonObject{};
            }
        }
        else
        {
            cache.pending.push_back(std::move(req));
            if (!cache.inFlight)
            {
                cache.inFlight = true;
                shouldFetch = true;
            }
        }
    }

    if (cachedChannelObj.has_value())
    {
        auto emoteMap = parseChannelEmotes(*cachedChannelObj);
        finishChannelRequest(std::move(req), std::move(emoteMap));
        return;
    }

    if (!shouldFetch)
    {
        return;
    }

    NetworkRequest(apiUrl)
        .concurrent()
        .onSuccess([](NetworkResult result) -> Outcome {
            QJsonObject allChannels = result.parseJson()
                                          .value("data")
                                          .toObject()
                                          .value("channel_emotes")
                                          .toObject();

            std::unordered_map<QString, QJsonObject> parsedMap;
            parsedMap.reserve(allChannels.size());
            for (auto it = allChannels.begin(); it != allChannels.end(); ++it)
            {
                parsedMap.emplace(it.key(), it.value().toObject());
            }

            std::vector<PendingChannelRequest> requests;
            std::vector<std::pair<PendingChannelRequest, QJsonObject>> resolved;
            auto &cache = getChannelCache();
            {
                std::lock_guard<std::mutex> lock(cache.mutex);
                cache.channelEmotes = std::move(parsedMap);
                cache.lastFetched = std::chrono::steady_clock::now();
                cache.inFlight = false;
                requests.swap(cache.pending);
                resolved.reserve(requests.size());
                for (auto &pendingReq : requests)
                {
                    auto it = cache.channelEmotes.find(pendingReq.channelId);
                    QJsonObject obj =
                        it != cache.channelEmotes.end() ? it->second
                                                        : QJsonObject{};
                    resolved.emplace_back(std::move(pendingReq),
                                          std::move(obj));
                }
            }

            for (auto &[pendingReq, obj] : resolved)
            {
                auto emoteMap = parseChannelEmotes(obj);
                finishChannelRequest(std::move(pendingReq),
                                     std::move(emoteMap));
            }
            return Success;
        })
        .onError([](NetworkResult result) {
            std::vector<PendingChannelRequest> requests;
            auto &cache = getChannelCache();
            {
                std::lock_guard<std::mutex> lock(cache.mutex);
                cache.inFlight = false;
                requests.swap(cache.pending);
            }

            auto status = result.status();
            postToGuiThread([requests = std::move(requests), status]() {
                for (const auto &pendingReq : requests)
                {
                    auto shared = pendingReq.channel.lock();
                    if (!shared)
                    {
                        continue;
                    }
                    if (status == 400)
                    {
                        if (pendingReq.manualRefresh)
                        {
                            shared->addSystemMessage(CHANNEL_HAS_NO_EMOTES);
                        }
                    }
                    else
                    {
                        shared->addSystemMessage(
                            "Failed to fetch Homies channel "
                            "emotes. (unknown error)");
                    }
                }
            });
        })
        .execute();
}

}  // namespace chatterino