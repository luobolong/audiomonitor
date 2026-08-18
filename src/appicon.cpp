#include "appicon.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

namespace {

// Draw the white speaker and sound waves using coordinates relative to rect.
void paintSpeaker(QPainter& p, const QRectF& r)
{
    const qreal s = r.width();

    QPolygonF body;
    body << QPointF(r.left() + 0.20 * s, r.top() + 0.38 * s)
         << QPointF(r.left() + 0.34 * s, r.top() + 0.38 * s)
         << QPointF(r.left() + 0.44 * s, r.top() + 0.28 * s)
         << QPointF(r.left() + 0.44 * s, r.top() + 0.72 * s)
         << QPointF(r.left() + 0.34 * s, r.top() + 0.62 * s)
         << QPointF(r.left() + 0.20 * s, r.top() + 0.62 * s);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawPolygon(body);

    QPen pen(Qt::white);
    pen.setWidthF(0.055 * s);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QPointF c(r.left() + 0.52 * s, r.top() + 0.50 * s);
    p.drawArc(QRectF(c.x() - 0.10 * s, c.y() - 0.10 * s, 0.20 * s, 0.20 * s), -55 * 16, 110 * 16);
    p.drawArc(QRectF(c.x() - 0.17 * s, c.y() - 0.17 * s, 0.34 * s, 0.34 * s), -50 * 16, 100 * 16);
}

QPixmap paintIcon(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient g(0, 0, 0, size);
    g.setColorAt(0, QColor(0x5A, 0x8A, 0xF5));
    g.setColorAt(1, QColor(0x2F, 0x5B, 0xD7));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    const qreal radius = size * 0.22;
    p.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

    paintSpeaker(p, QRectF(0, 0, size, size));
    return pm;
}

} // namespace

QIcon makeAppIcon()
{
    QIcon icon;
    for (const int s : { 16, 24, 32, 48, 64, 128, 256 })
        icon.addPixmap(paintIcon(s));
    return icon;
}
