#include "providers/seventv/paints/Paint.hpp"

#include "Application.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"

#include <private/qpixmapfilter_p.h>
#include <QCoreApplication>
#include <QLabel>
#include <QPainter>

#include <array>
#include <optional>

namespace {

struct PaintCacheEntry {
    size_t hash = 0;
    size_t frameIndex = 0;
    QPixmap pixmap;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
constinit std::array<std::optional<PaintCacheEntry>, 64> ANIMATED_PAINT_CACHE;

void ensureCacheCleanupRegistered()
{
    static const QCoreApplication *registeredApp = nullptr;
    auto *app = QCoreApplication::instance();
    if (app != nullptr && app != registeredApp)
    {
        registeredApp = app;
        QObject::connect(app, &QCoreApplication::aboutToQuit, [] {
            ANIMATED_PAINT_CACHE.fill(std::nullopt);
        });
    }
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
bool getFromCache(size_t hash, size_t frameIndex, QPixmap *pixmap)
{
    if (QCoreApplication::closingDown())
    {
        return false;
    }
    const auto &entry =
        ANIMATED_PAINT_CACHE[hash % ANIMATED_PAINT_CACHE.size()];
    if (!entry || entry->hash != hash || entry->frameIndex != frameIndex)
    {
        return false;
    }
    *pixmap = entry->pixmap;
    return true;
}

void putToCache(size_t hash, size_t frameIndex, const QPixmap &pixmap)
{
    if (QCoreApplication::closingDown())
    {
        return;
    }
    ensureCacheCleanupRegistered();
    ANIMATED_PAINT_CACHE[hash % ANIMATED_PAINT_CACHE.size()] = PaintCacheEntry{
        .hash = hash,
        .frameIndex = frameIndex,
        .pixmap = pixmap,
    };
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

}  // namespace

namespace chatterino {

using namespace Qt::Literals;

QPixmap Paint::getPixmap(const QString &text, const QFont &font,
                         QColor userColor, QSizeF size, float scale,
                         float dpr) const
{
    const bool isAnimated = this->animated();
    const bool showShadows = getSettings()->displaySevenTVPaintShadows;
    const QSize targetSize = (size * dpr).toSize();

    size_t hash = 0;
    size_t frameIdx = 0;
    if (isAnimated)
    {
        frameIdx = this->frameIndex();
        hash = qHashMulti(0, this->id, text, font, userColor.rgba(),
                          targetSize.width(), targetSize.height(), scale, dpr,
                          showShadows);
        QPixmap cached;
        if (getFromCache(hash, frameIdx, &cached))
        {
            return cached;
        }
    }

    QPixmap pixmap(targetSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter pixmapPainter(&pixmap);
    pixmapPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    pixmapPainter.setFont(font);

    // NOTE: draw colon separately from the nametag
    // otherwise the paint would extend onto the colon
    bool drawColon = false;
    QRectF nametagBoundingRect{QPointF{}, size};
    QString nametagText = text;
    if (nametagText.endsWith(':'))
    {
        drawColon = true;
        nametagText = nametagText.chopped(1);
        nametagBoundingRect = pixmapPainter.boundingRect(
            QRectF(0, 0, 10000, 10000), nametagText,
            QTextOption(Qt::AlignLeft | Qt::AlignTop));
    }

    QPen pen;
    const QBrush brush = this->asBrush(userColor, nametagBoundingRect);
    pen.setBrush(brush);
    pixmapPainter.setPen(pen);

    pixmapPainter.drawText(nametagBoundingRect, nametagText,
                           QTextOption(Qt::AlignLeft | Qt::AlignTop));
    pixmapPainter.end();

    if (!this->getDropShadows().empty() && showShadows)
    {
        QPixmap outMap(targetSize);
        outMap.setDevicePixelRatio(dpr);
        for (const auto &shadow : this->getDropShadows())
        {
            if (!shadow.isValid())
            {
                continue;
            }
            outMap.fill(Qt::transparent);

            {
                QPainter outPainter(&outMap);
                auto scaled = shadow.scaled(
                    scale / static_cast<float>(outMap.devicePixelRatio()));

                QPixmapDropShadowFilter filter;
                scaled.apply(filter);
                filter.draw(&outPainter, {0, 0}, pixmap);
            }
            outMap.swap(pixmap);
        }
    }

    if (drawColon)
    {
        auto colonColor = getApp()->getThemes()->messages.textColors.regular;

        pixmapPainter.begin(&pixmap);

        pixmapPainter.setPen(QPen(colonColor));
        pixmapPainter.setFont(font);

        QRectF colonBoundingRect(nametagBoundingRect.right(), 0, 10000, 10000);
        pixmapPainter.drawText(colonBoundingRect, u":"_s,
                               QTextOption(Qt::AlignLeft | Qt::AlignTop));
        pixmapPainter.end();
    }

    if (isAnimated)
    {
        putToCache(hash, frameIdx, pixmap);
    }

    return pixmap;
}

QColor Paint::overlayColors(QColor background, QColor foreground)
{
    auto alpha = foreground.alphaF();

    auto r = ((1 - alpha) * static_cast<float>(background.red())) +
             (alpha * static_cast<float>(foreground.red()));
    auto g = ((1 - alpha) * static_cast<float>(background.green())) +
             (alpha * static_cast<float>(foreground.green()));
    auto b = ((1 - alpha) * static_cast<float>(background.blue())) +
             (alpha * static_cast<float>(foreground.blue()));

    return {static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)};
}

qreal Paint::offsetRepeatingStopPosition(const qreal position,
                                         const QGradientStops &stops)
{
    const qreal gradientStart = stops.first().first;
    const qreal gradientEnd = stops.last().first;
    const qreal gradientLength = gradientEnd - gradientStart;
    const qreal offsetPosition = (position - gradientStart) / gradientLength;

    return offsetPosition;
}

}  // namespace chatterino
