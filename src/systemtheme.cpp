#include "systemtheme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QStyleHints>
#include <QVariant>

namespace {
QVariant unwrapVariant(QVariant value) {
    while (value.canConvert<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

bool colorSchemeIsDark(const QVariant &value, bool *known) {
    bool ok = false;
    const uint scheme = unwrapVariant(value).toUInt(&ok);
    if (!ok)
        return false;

    if (scheme == 1) {
        *known = true;
        return true;
    }
    if (scheme == 2) {
        *known = true;
        return false;
    }

    return false;
}

bool gsettingsSchemeIsDark(const QVariant &value, bool *known) {
    const QString scheme = unwrapVariant(value).toString();
    if (scheme.contains(QStringLiteral("prefer-dark"))) {
        *known = true;
        return true;
    }
    if (scheme.contains(QStringLiteral("prefer-light"))) {
        *known = true;
        return false;
    }

    return false;
}
} // namespace

qreal SystemTheme::normalizedTextScale(const QVariant &value, bool *known) {
    // GNOME's text-scaling-factor is the desktop-wide "apparent text size" knob;
    // omarchy drives it from `omarchy display text size`, anchored so the default
    // 12px maps to 1.0. Ignore nonsense values and cap the range GNOME allows.
    bool ok = false;
    const qreal scale = unwrapVariant(value).toDouble(&ok);
    if (!ok || scale <= 0)
        return 1.0;

    *known = true;
    return qBound(0.5, scale, 3.0);
}

SystemTheme::SystemTheme(QObject *parent) : QObject(parent) {
    // Dark mode can arrive after startup. The window size cannot: it is chosen
    // once from the text scale below, before the interface is shown.
    bool known = false;
    const bool qtDark = qtDarkMode(&known);
    if (known)
        m_darkMode = qtDark;

    if (QGuiApplication::styleHints()) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &SystemTheme::refresh);
    }

    QDBusConnection::sessionBus().connect(
        QString(),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"),
        QStringLiteral("SettingChanged"),
        this,
        SLOT(handlePortalSettingChanged(QString,QString,QDBusVariant)));

    requestPortalDarkMode();
    readInitialTextScale();
    // If the blocking read timed out, this can still update the font. It does
    // not resize the window: that size was taken before the event loop.
    requestPortalTextScale();
}

void SystemTheme::refresh() {
    bool known = false;
    const bool qtDark = qtDarkMode(&known);
    if (known)
        setDarkMode(qtDark);

    requestPortalDarkMode();
    requestPortalTextScale();
}

// Ask the desktop portal for a single setting without holding up the GUI
// thread; the handler only runs when a valid answer comes back, so a missing
// or stalled portal simply leaves the current state alone.
void SystemTheme::requestPortalSetting(const QString &nameSpace, const QString &key,
                                       std::function<void(const QVariant &)> handler) {
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    QDBusMessage request = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"),
        QStringLiteral("Read"));
    request << nameSpace << key;

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [handler = std::move(handler)](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<QDBusVariant> reply(*finished);
        finished->deleteLater();
        if (reply.isValid())
            handler(reply.value().variant());
    });
}

void SystemTheme::requestPortalDarkMode() {
    requestPortalSetting(QStringLiteral("org.freedesktop.appearance"),
                         QStringLiteral("color-scheme"),
                         [this](const QVariant &value) {
        bool known = false;
        const bool dark = colorSchemeIsDark(value, &known);
        if (known)
            setDarkMode(dark);
    });
}

void SystemTheme::requestPortalTextScale() {
    requestPortalSetting(QStringLiteral("org.gnome.desktop.interface"),
                         QStringLiteral("text-scaling-factor"),
                         [this](const QVariant &value) {
        bool known = false;
        const qreal scale = normalizedTextScale(value, &known);
        if (known)
            setTextScale(scale);
    });
}

// The async read above can miss the first frame. Block briefly so the launch
// size matches the desktop text scale. A missing or slow portal leaves 1.0.
void SystemTheme::readInitialTextScale() {
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    QDBusMessage request = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"),
        QStringLiteral("Read"));
    request << QStringLiteral("org.gnome.desktop.interface")
            << QStringLiteral("text-scaling-factor");

    const QDBusMessage reply = bus.call(request, QDBus::Block, 200);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return;

    bool known = false;
    const qreal scale = normalizedTextScale(reply.arguments().constFirst(), &known);
    if (known)
        m_textScale = scale;
}

void SystemTheme::handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                             const QDBusVariant &value) {
    if (key == QStringLiteral("text-scaling-factor")) {
        if (nameSpace != QStringLiteral("org.gnome.desktop.interface"))
            return;

        bool known = false;
        const qreal scale = normalizedTextScale(value.variant(), &known);
        if (known)
            setTextScale(scale);
        return;
    }

    if (key != QStringLiteral("color-scheme"))
        return;

    bool known = false;
    bool dark = false;
    if (nameSpace == QStringLiteral("org.freedesktop.appearance"))
        dark = colorSchemeIsDark(value.variant(), &known);
    else if (nameSpace == QStringLiteral("org.gnome.desktop.interface"))
        dark = gsettingsSchemeIsDark(value.variant(), &known);
    else
        return;

    if (known)
        setDarkMode(dark);
    else
        refresh();
}

bool SystemTheme::qtDarkMode(bool *known) const {
    *known = false;

    if (!QGuiApplication::styleHints())
        return false;

    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) {
        *known = true;
        return true;
    }
    if (scheme == Qt::ColorScheme::Light) {
        *known = true;
        return false;
    }

    return false;
}

void SystemTheme::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    emit darkModeChanged(m_darkMode);
}

void SystemTheme::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged(m_textScale);
}
