#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <optional>

#include "number.h"

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

    // One piece of an expression: a number, or an operator sign.
    struct Token {
        QString op;
        Number number;

        static Token value(const Number &number) { return {QString(), number}; }
        static Token operation(const QString &op) { return {op, Number()}; }
        bool isOperator() const { return !op.isEmpty(); }
    };

    // The value of number, operator, number, ... with ^ first (right to
    // left), then × and ÷, then + and −. Nothing when any step is an error.
    static std::optional<Number> evaluate(const QList<Token> &tokens);
    static QString groupDigits(const QString &number);

    // The exact number behind the display, which shows it rounded to fifteen
    // digits; nothing while the display shows Error.
    std::optional<Number> value() const;

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

private:
    void pressDigit(const QString &digit);
    void pressDecimal();
    void pressOperator(const QString &pretty);
    void pressEquals();
    void pressPercent();
    void pressToggleSign();
    void pressBackspace();
    void pressClear();

    QString plainDisplay() const;
    Number currentValue() const;
    void setEntryText(const QString &text);
    void setEntryValue(const Number &number);
    void clearEntry();
    void beginEditingAfterResult();
    void clearEvaluation();
    static QString prettyExpression(const QList<Token> &tokens);
    void loadOmarchyTheme();
    void watchOmarchyTheme();
    void discardSavedWindowGeometry();

    QList<Token> m_tokens;
    // The entry as typed, and its exact value. They differ only when the
    // entry came from a percent, a paste, or a result: then the text shows
    // fifteen digits and the value keeps them all, until a digit, point or
    // backspace makes the text itself the number again.
    QString m_entry;
    Number m_entryValue;
    QString m_result;
    Number m_resultValue;
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
