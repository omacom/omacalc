#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QKeyEvent>

#include "backend.h"

namespace {
void press(Backend &calculator, const QString &keys) {
    const QStringList sequence = keys.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &key : sequence)
        calculator.pressKey(key);
}
}

class OmacalcTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void keypadWithMissingNumLockState() {
        Backend calculator;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &calculator);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QCoreApplication::processEvents();
        QVERIFY(window->activeFocusItem());
        const auto send = [window](int key, Qt::KeyboardModifiers modifiers,
                                   const QString &text = QString()) {
            QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
            QCoreApplication::sendEvent(window->activeFocusItem(), &event);
        };
        // Deliver the empty-text navigation events seen when Num Lock is lost.
        const int keys[] = {Qt::Key_Insert, Qt::Key_End, Qt::Key_Down,
            Qt::Key_PageDown, Qt::Key_Left, Qt::Key_Clear, Qt::Key_Right,
            Qt::Key_Home, Qt::Key_Up, Qt::Key_PageUp};
        for (int digit = 0; digit < 10; ++digit) {
            calculator.pressKey("clear");
            send(keys[digit], Qt::KeypadModifier);
            QCOMPARE(calculator.display(), QString::number(digit));
        }
        send(Qt::Key_Delete, Qt::KeypadModifier);
        send(Qt::Key_End, Qt::KeypadModifier);
        QCOMPARE(calculator.display(), QStringLiteral("9.1"));
        // Normal navigation and modified shortcuts must not enter digits.
        send(Qt::Key_Left, Qt::NoModifier);
        send(Qt::Key_Home, Qt::KeypadModifier | Qt::ControlModifier);
        QCOMPARE(calculator.display(), QStringLiteral("9.1"));
        send(Qt::Key_Delete, Qt::NoModifier);
        QCOMPARE(calculator.display(), QStringLiteral("0"));
        send(Qt::Key_4, Qt::NoModifier, "4");
        send(Qt::Key_2, Qt::KeypadModifier, "2");
        QCOMPARE(calculator.display(), QStringLiteral("42"));
    }

    void startsAtZero() {
        Backend calculator;
        QCOMPARE(calculator.display(), QStringLiteral("0"));
        QCOMPARE(calculator.expression(), QString());
    }

    void calculatesWithPrecedence() {
        Backend calculator;
        press(calculator, "4 2 × 3 + 7 =");
        QCOMPARE(calculator.display(), QStringLiteral("133"));
        QCOMPARE(calculator.expression(), QStringLiteral("42 × 3 + 7"));

        press(calculator, "clear 2 + 3 × 4 =");
        QCOMPARE(calculator.display(), QStringLiteral("14"));

        press(calculator, "clear 1 0 - 4 ÷ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("8"));
    }

    void showsEntryWhileTyping() {
        Backend calculator;
        press(calculator, "4 2 ×");
        QCOMPARE(calculator.display(), QStringLiteral("42"));
        QCOMPARE(calculator.expression(), QStringLiteral("42 ×"));

        press(calculator, "3");
        QCOMPARE(calculator.display(), QStringLiteral("3"));
    }

    void handlesDecimals() {
        Backend calculator;
        press(calculator, ". 5 + . 2 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("0.75"));

        // Binary-float noise stays out of the display.
        press(calculator, "clear 0 . 1 + 0 . 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("0.3"));

        // A second decimal point in one number is ignored.
        press(calculator, "clear 1 . 5 . 5");
        QCOMPARE(calculator.display(), QStringLiteral("1.55"));
    }

    void divisionByZeroErrors() {
        Backend calculator;
        press(calculator, "1 ÷ 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));

        // Digits recover from an error without an explicit clear.
        press(calculator, "5");
        QCOMPARE(calculator.display(), QStringLiteral("5"));
    }

    void percentOfRunningTotal() {
        Backend calculator;

        // With a pending + or −, x% means x percent of the running total.
        press(calculator, "2 0 0 + 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("220"));

        press(calculator, "clear 2 0 0 - 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("180"));

        // With × or ÷, or standalone, x% is simply x ÷ 100.
        press(calculator, "clear 2 0 0 × 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("20"));

        press(calculator, "clear 5 0 %");
        QCOMPARE(calculator.display(), QStringLiteral("0.5"));

        // After equals, percent picks up from the result.
        press(calculator, "clear 4 0 + 1 0 = %");
        QCOMPARE(calculator.display(), QStringLiteral("0.5"));
    }

    void percentAndSign() {
        Backend calculator;
        press(calculator, "8 sign");
        QCOMPARE(calculator.display(), QStringLiteral("-8"));
        press(calculator, "sign");
        QCOMPARE(calculator.display(), QStringLiteral("8"));

        press(calculator, "clear 4 + 8 sign =");
        QCOMPARE(calculator.display(), QStringLiteral("-4"));
    }

    void signStartsNewOperand() {
        Backend calculator;

        // Sign with nothing typed starts a fresh negative operand rather than
        // negating the previous one: 4 + ± 2 = is 4 + (-2), not 4 + (-42).
        press(calculator, "4 + sign 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("2"));
        QCOMPARE(calculator.expression(), QStringLiteral("4 + -2"));

        press(calculator, "clear sign");
        QCOMPARE(calculator.display(), QStringLiteral("-0"));
        press(calculator, "5");
        QCOMPARE(calculator.display(), QStringLiteral("-5"));
    }

    void chainsWithFullPrecision() {
        Backend calculator;

        // Chaining continues from the exact value, not the rounded display.
        press(calculator, "1 ÷ 3 = × 3 =");
        QCOMPARE(calculator.display(), QStringLiteral("1"));

        // Integers within the 15-digit entry limit survive exactly.
        press(calculator, "clear 9 9 9 9 9 9 9 9 9 9 9 9 9 9 =");
        QCOMPARE(calculator.display(), QStringLiteral("99999999999999"));
    }

    void capsEntryAtFifteenDigits() {
        Backend calculator;
        press(calculator, "1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7 8");
        QCOMPARE(calculator.display(), QStringLiteral("123456789123456"));

        // The decimal point does not count against the digit cap.
        press(calculator, "clear . 1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7");
        QCOMPARE(calculator.display(), QStringLiteral("0.123456789123456"));
    }

    void backspaceEdits() {
        Backend calculator;
        press(calculator, "1 2 3 backspace");
        QCOMPARE(calculator.display(), QStringLiteral("12"));

        press(calculator, "backspace backspace backspace");
        QCOMPARE(calculator.display(), QStringLiteral("0"));
    }

    void chainsFromResult() {
        Backend calculator;
        press(calculator, "6 × 7 = × 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("84"));
        QCOMPARE(calculator.expression(), QStringLiteral("42 × 2"));

        // A digit after equals starts fresh instead of appending to the result.
        press(calculator, "9");
        QCOMPARE(calculator.display(), QStringLiteral("9"));
        QCOMPARE(calculator.expression(), QString());
    }

    void replacesDanglingOperator() {
        Backend calculator;
        press(calculator, "4 + × 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("8"));

        // Equals with a trailing operator drops it.
        press(calculator, "clear 9 + =");
        QCOMPARE(calculator.display(), QStringLiteral("9"));
    }

    void pastesNumbers() {
        Backend calculator;
        QClipboard *clipboard = QGuiApplication::clipboard();

        clipboard->setText(QStringLiteral(" 42.5 "));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("42.5"));

        // A pasted number is a normal entry that calculates like any other.
        press(calculator, "+ . 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("43"));

        // Decimal commas are welcome; garbage is ignored.
        clipboard->setText(QStringLiteral("1,5"));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("1.5"));

        clipboard->setText(QStringLiteral("not a number"));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("1.5"));
    }

    void evaluatesTokens() {
        bool ok = false;
        QCOMPARE(Backend::evaluateTokens({"2", "+", "3", "×", "4"}, &ok), 14.0);
        QVERIFY(ok);

        Backend::evaluateTokens({"2", "+"}, &ok);
        QVERIFY(!ok);

        Backend::evaluateTokens({"1", "÷", "0"}, &ok);
        QVERIFY(!ok);
    }

    void formatsNumbers() {
        QCOMPARE(Backend::formatNumber(133), QStringLiteral("133"));
        QCOMPARE(Backend::formatNumber(0.1 + 0.2), QStringLiteral("0.3"));
        QCOMPARE(Backend::formatNumber(-0.0), QStringLiteral("0"));
        QCOMPARE(Backend::formatNumber(1e15), QStringLiteral("1e+15"));
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend calculator;
        QCOMPARE(calculator.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(calculator.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(calculator.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(calculator.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!calculator.darkMode());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmacalcTest)
#include "tst_omacalc.moc"
