/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "screens_xrandr.h"
#include "xcbutils.h"


namespace KWin
{

XRandRScreens::XRandRScreens(QObject *parent)
    : Screens(parent)
    , X11EventFilter(Xcb::Extensions::self()->randrNotifyEvent())
{
}

XRandRScreens::~XRandRScreens() = default;

template <typename T>
void XRandRScreens::update()
{
    auto fallback = [this]() {
        m_geometries << QRect();
        m_refreshRates << -1.0f;
        setCount(1);
    };
    m_geometries.clear();
    m_names.clear();
    T resources(rootWindow());
    if (resources.isNull()) {
        fallback();
        return;
    }
    xcb_randr_crtc_t *crtcs = resources.crtcs();
    xcb_randr_mode_info_t *modes = resources.modes();

    QVector<Xcb::RandR::CrtcInfo> infos(resources->num_crtcs);
    for (int i = 0; i < resources->num_crtcs; ++i) {
        infos[i] = Xcb::RandR::CrtcInfo(crtcs[i], resources->config_timestamp);
    }

    for (int i = 0; i < resources->num_crtcs; ++i) {
        Xcb::RandR::CrtcInfo info(infos.at(i));

        xcb_randr_output_t *outputs = info.outputs();
        QVector<Xcb::RandR::OutputInfo> outputInfos(outputs ? resources->num_outputs : 0);
        if (outputs) {
            for (int i = 0; i < resources->num_outputs; ++i) {
                outputInfos[i] = Xcb::RandR::OutputInfo(outputs[i], resources->config_timestamp);
            }
        }

        float refreshRate = -1.0f;
        for (int j = 0; j < resources->num_modes; ++j) {
            if (info->mode == modes[j].id) {
                if (modes[j].htotal*modes[j].vtotal) { // BUG 313996
                    // refresh rate calculation - WTF was wikipedia 1998 when I needed it?
                    int dotclock = modes[j].dot_clock,
                          vtotal = modes[j].vtotal;
                    if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE)
                        dotclock *= 2;
                    if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN)
                        vtotal *= 2;
                    refreshRate = dotclock/float(modes[j].htotal*vtotal);
                }
                break; // found mode
            }
        }

        const QRect geo = info.rect();
        if (geo.isValid()) {
            m_geometries << geo;
            m_refreshRates << refreshRate;
            QString name;
            for (int j = 0; j < info->num_outputs; ++j) {
                Xcb::RandR::OutputInfo outputInfo(outputInfos.at(j));
                if (crtcs[i] == outputInfo->crtc) {
                    name = outputInfo.name();
                    break;
                }
            }
            m_names << name;
        }
    }
    if (m_geometries.isEmpty()) {
        fallback();
        return;
    }

    setCount(m_geometries.count());
}


void XRandRScreens::init()
{
    KWin::Screens::init();
    // we need to call ScreenResources at least once to be able to use current
    update<Xcb::RandR::ScreenResources>();
    emit changed();
}

QRect XRandRScreens::geometry(int screen) const
{
    if (screen >= m_geometries.size() || screen < 0) {
        return QRect();
    }
    return m_geometries.at(screen);
}

QString XRandRScreens::name(int screen) const
{
    if (screen >= m_names.size() || screen < 0) {
        return QString();
    }
    return m_names.at(screen);
}

int XRandRScreens::number(const QPoint &pos) const
{
    int bestScreen = 0;
    int minDistance = INT_MAX;
    for (int i = 0; i < m_geometries.size(); ++i) {
        const QRect &geo = m_geometries.at(i);
        if (geo.contains(pos)) {
            return i;
        }
        int distance = QPoint(geo.topLeft() - pos).manhattanLength();
        distance = qMin(distance, QPoint(geo.topRight() - pos).manhattanLength());
        distance = qMin(distance, QPoint(geo.bottomRight() - pos).manhattanLength());
        distance = qMin(distance, QPoint(geo.bottomLeft() - pos).manhattanLength());
        if (distance < minDistance) {
            minDistance = distance;
            bestScreen = i;
        }
    }
    return bestScreen;
}

float XRandRScreens::refreshRate(int screen) const
{
    if (screen >= m_refreshRates.size() || screen < 0) {
        return -1.0f;
    }
    return m_refreshRates.at(screen);
}

QSize XRandRScreens::size(int screen) const
{
    const QRect geo = geometry(screen);
    if (!geo.isValid()) {
        return QSize();
    }
    return geo.size();
}

void XRandRScreens::updateCount()
{
    update<Xcb::RandR::CurrentResources>();
}

bool XRandRScreens::event(xcb_generic_event_t *event)
{
    Q_ASSERT((event->response_type & ~0x80) == Xcb::Extensions::self()->randrNotifyEvent());
    // let's try to gather a few XRandR events, unlikely that there is just one
    startChangedTimer();
    return false;
}

} // namespace
