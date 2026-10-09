#pragma once

#include <QObject>
#include <QVariant>

#include <functional>

class QDBusVariant;

class SystemTheme : public QObject {
    Q_OBJECT

public:
    explicit SystemTheme(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }
    qreal textScale() const { return m_textScale; }

    // Portal text-scaling-factor, clamped to the range GNOME allows.
    // *known is set only when value is a positive number.
    static qreal normalizedTextScale(const QVariant &value, bool *known);

signals:
    void darkModeChanged(bool darkMode);
    void textScaleChanged(qreal textScale);

public slots:
    void refresh();

private slots:
    void handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                    const QDBusVariant &value);

private:
    void requestPortalSetting(const QString &nameSpace, const QString &key,
                              std::function<void(const QVariant &)> handler);
    void requestPortalDarkMode();
    void requestPortalTextScale();
    void readInitialTextScale();
    bool qtDarkMode(bool *known) const;
    void setDarkMode(bool darkMode);
    void setTextScale(qreal textScale);

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
};
