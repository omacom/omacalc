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
    Q_PROPERTY(bool scientific READ scientific WRITE setScientific NOTIFY scientificChanged)
    Q_PROPERTY(bool degrees READ degrees WRITE setDegrees NOTIFY degreesChanged)

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
    bool scientific() const { return m_scientific; }
    void setScientific(bool scientific);
    bool degrees() const { return m_degrees; }
    void setDegrees(bool degrees);

    static double evaluateTokens(const QStringList &tokens, bool *ok);
    static QString formatNumber(double value);
    static QString groupDigits(const QString &number);
    static double applyFunction(const QString &function, double value, bool degrees, bool *ok);

    Q_INVOKABLE void pressKey(const QString &key);
    Q_INVOKABLE void copyResult() const;
    Q_INVOKABLE void pasteNumber();
    // Launch size for the current text scale. Not a live binding: the window
    // reads it once, and a later scale change waits until the next launch.
    Q_INVOKABLE QVariantMap defaultWindowSize() const;

signals:
    void calculationChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void scientificChanged();
    void degreesChanged();

private:
    void pressDigit(const QString &digit);
    void pressDecimal();
    void pressOperator(const QString &pretty);
    void pressEquals();
    void pressPercent();
    void pressToggleSign();
    void pressBackspace();
    void pressClear();
    void pressOpenParenthesis();
    void pressCloseParenthesis();
    void pressFunction(const QString &function);
    void pressConstant(double value);
    void pushEntry();
    int openParentheses() const;
    int currentGroupStart() const;

    QString plainDisplay() const;
    QString currentValue() const;
    void beginEditingAfterResult();
    void clearEvaluation();
    static QString prettyExpression(const QStringList &tokens);
    void loadOmarchyTheme();
    void watchOmarchyTheme();
    void discardSavedWindowGeometry();

    QStringList m_tokens;
    QString m_entry;
    QString m_result;
    double m_resultValue = 0;
    QString m_evaluatedExpression;
    bool m_justEvaluated = false;
    bool m_errored = false;
    // Set when a function or constant wrote the entry: it is a whole value,
    // so typing starts a new number instead of appending to its digits.
    bool m_entryComputed = false;
    bool m_scientific = false;
    bool m_degrees = true;

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QFileSystemWatcher m_themeWatcher;
};
