#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString expression READ expression NOTIFY calculationChanged)
    Q_PROPERTY(QString display READ display NOTIFY calculationChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    QString expression() const;
    QString display() const;

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }

    static double evaluateTokens(const QStringList &tokens, bool *ok);
    static QString formatNumber(double value);

    Q_INVOKABLE void pressKey(const QString &key);
    Q_INVOKABLE void copyResult() const;
    Q_INVOKABLE void pasteNumber();
    // Walk back through expressions that were equals'd this session.
    Q_INVOKABLE void recallPrevious();
    // Launch size for the current text scale. Not a live binding: the window
    // reads it once, and a later scale change waits until the next launch.
    Q_INVOKABLE QVariantMap defaultWindowSize() const;

signals:
    void calculationChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();

private:
    void pressDigit(const QString &digit);
    void pressDecimal();
    void pressOperator(const QString &pretty);
    void pressEquals();
    void pressPercent();
    void pressToggleSign();
    void pressBackspace();
    void pressClear();

    QString currentValue() const;
    void beginEditingAfterResult();
    void clearEvaluation();
    static QString prettyExpression(const QStringList &tokens);
    void loadOmarchyTheme();
    void watchOmarchyTheme();
    void discardSavedWindowGeometry();

    QStringList m_tokens;
    QList<QStringList> m_history;
    int m_recallIndex = -1;
    QString m_entry;
    QString m_result;
    double m_resultValue = 0;
    QString m_evaluatedExpression;
    bool m_justEvaluated = false;
    bool m_errored = false;

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QFileSystemWatcher m_themeWatcher;
};
