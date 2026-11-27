#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <optional>
#include <random>

#include "backend.h"
#include "reference.h"

// Types random calculations into the real Backend, one key at a time, and
// after every key holds the display, the expression line and the exact value
// behind the display against an independent model of the calculator
// (reference.h). A failure is shrunk to the fewest keys that still fail and
// reported with its seed, ready to paste into a regular test.
//
// OMACALC_FUZZ_SEQUENCES sets how many calculations to try (default 6000)
// and OMACALC_FUZZ_SEED picks a different set. For a long run:
//   OMACALC_FUZZ_SEQUENCES=1000000 OMACALC_FUZZ_SEED=$RANDOM build/omacalc_fuzz

namespace {

// Slower than this is a hang as far as anyone at the keyboard is concerned.
constexpr qint64 kSlowKeyMilliseconds = 1000;

struct Action {
    enum Kind { Key, Paste, CopyPaste, CopyPasteGrouped };
    Kind kind = Key;
    QString text;
};

QString describe(const QList<Action> &actions) {
    QStringList words;
    for (const Action &action : actions) {
        switch (action.kind) {
        case Action::Key:
            words << (action.text.isEmpty() ? QStringLiteral("\"\"") : action.text);
            break;
        case Action::Paste:
            words << QStringLiteral("paste(\"") + action.text + QStringLiteral("\")");
            break;
        case Action::CopyPaste:
            words << QStringLiteral("copy-paste");
            break;
        case Action::CopyPasteGrouped:
            words << QStringLiteral("copy-paste-grouped");
            break;
        }
    }
    return words.join(QLatin1Char(' '));
}

quint64 environmentNumber(const char *name, quint64 fallback) {
    bool ok = false;
    const quint64 value = qEnvironmentVariable(name).toULongLong(&ok);
    return ok ? value : fallback;
}

class Generator {
public:
    explicit Generator(std::seed_seq &seed) : m_random(seed) {}

    QList<Action> sequence() {
        QList<Action> actions;
        const int target = 1 + below(40);
        bool wantOperand = true;
        while (actions.size() < target) {
            const int roll = below(100);
            if (wantOperand && roll < 60) {
                type(actions, number());
                wantOperand = false;
            } else if (roll < 72) {
                static const QStringList operators = {
                    "+", "+", "-", "-", "−", "*", "*", "×", "/", "/", "÷", "^", "^"};
                const QString op = operators.at(below(operators.size()));
                actions << key(op);
                if (op == QStringLiteral("^") && chance(0.75)) {
                    type(actions, exponent());
                    wantOperand = false;
                } else {
                    wantOperand = true;
                }
            } else if (roll < 82) {
                actions << key(QStringLiteral("="));
                wantOperand = chance(0.4);
            } else if (roll < 86) {
                actions << key(QStringLiteral("%"));
            } else if (roll < 89) {
                actions << key(QStringLiteral("sign"));
            } else if (roll < 92) {
                for (int i = below(3); i >= 0; --i)
                    actions << key(QStringLiteral("backspace"));
            } else if (roll < 93) {
                actions << key(QStringLiteral("."));
            } else if (roll < 95) {
                actions << Action{Action::Paste, pasteText()};
                wantOperand = false;
            } else if (roll < 97) {
                actions << Action{chance(0.5) ? Action::CopyPaste : Action::CopyPasteGrouped, {}};
            } else if (roll < 98) {
                actions << key(QStringLiteral("clear"));
                wantOperand = true;
            } else {
                static const QStringList junk = {"x", "", "Enter", "?", "e", "12", "+-", "٣", "C"};
                actions << key(junk.at(below(junk.size())));
            }
        }
        if (chance(0.7))
            actions << key(QStringLiteral("="));
        return actions;
    }

    // A number to type, sign included: "13373.54", "-0.000123", "1.21".
    QString number() {
        QString text;
        switch (below(12)) {
        case 0:
            text = QString::number(below(100));
            break;
        case 1:
            text = digits(1 + below(15));
            break;
        case 2:
            text = digits(1 + below(6)) + QLatin1Char('.') + digits(2, false);
            break;
        case 3: {
            const int total = 2 + below(14);
            const int whole = 1 + below(total - 1);
            text = digits(whole) + QLatin1Char('.') + digits(total - whole, false);
            break;
        }
        case 4: {
            const int zeros = 1 + below(12);
            text = QStringLiteral("0.") + QString(zeros, QLatin1Char('0'))
                + digits(1 + below(15 - zeros));
            break;
        }
        case 5:
        case 6:
            // Close to, or equal to, the last number: cancellation is where
            // binary floats showed their noise.
            text = m_last.isEmpty() ? digits(1 + below(8)) : nudged(m_last);
            break;
        case 7:
        case 8: {
            static const QStringList memorable = {
                "0.1", "0.2", "0.3", "0.7", "1.1", "1.15", "3", "7", "9", "10", "100", "1000",
                "0.5", "0.25", "0.125", "2", "4", "8", "16", "27", "32", "64", "81", "1.21",
                "0.04", "1.44", "0.01", "0.001", "999999999999999", "0.999999999999999",
                "123456789012345", "0.333333333333333", "0.666666666666667", "1.00000000000001",
                "99999999999999.9", "13373.54", "13438.77", "1000000.1", "0.000000000000001",
                "12", "60", "365", "1.05", "0.05", "19.99", "4.35", "1.0001", "1.001", "0.999",
                "1.01", "0.9999", "1.15", "1.5", "2.5", "0.75"};
            text = memorable.at(below(memorable.size()));
            break;
        }
        case 9:
            text = QStringLiteral("0");
            break;
        case 10:
            text = QStringLiteral("1");
            break;
        default: {
            const int point = below(15);
            text = digits(15);
            if (point > 0)
                text.insert(point, QLatin1Char('.'));
            break;
        }
        }
        m_last = text;
        return chance(0.15) ? QStringLiteral("-") + text : text;
    }

    QString exponent() {
        static const QStringList exponents = {
            "0", "1", "2", "3", "4", "0.5", "0.5", "0.25", "0.75", "1.5", "0.2", "0.1", "0.125",
            "2.5", "10", "20", "100", "-1", "-2", "-0.5", "0.333333333333333", "0.05", "1000",
            "12345", "99999", "123456789012345", "0.000001", "3.7", "365", "-0.25", "100001",
            "54321", "-12345", "77777"};
        if (chance(0.15))
            return number();
        if (chance(0.1))
            return exactnessBoundary();
        return exponents.at(below(exponents.size()));
    }

    // An exponent near where the number typed last, raised to it, stops
    // fitting the backend's exact fractions: just inside, just outside, and
    // either side of where a quick bit-count estimate would put that line.
    QString exactnessBoundary() {
        const std::optional<mpq_class> base = reference::parse(m_last);
        if (!base || *base == 0 || abs(*base) == 1)
            return QStringLiteral("32769");
        const mpz_class numerator = abs(base->get_num());
        const mpz_class &wider = numerator > base->get_den() ? numerator : base->get_den();
        long exponent = 0;
        const double mantissa = mpz_get_d_2exp(&exponent, wider.get_mpz_t());
        const double log2 = double(exponent) + std::log2(mantissa);
        const double widest = double(mpz_sizeinbase(wider.get_mpz_t(), 2));
        const double limit = double(Number::kMaxExactBits);
        const double lines[] = {limit / log2, limit / widest, 2 * limit / widest};
        const long n = long(lines[below(3)]) + below(7) - 3;
        return QString::number(std::max(n, 2L));
    }

    QString pasteText() {
        static const QStringList pastes = {
            "42", " 42.5 ", "1,234,567.89", "−12,000", "1,5", "abc", "", "1e-7", "-1e+300",
            "1e10000", "1e-10000", "9.999999999999995e9999", "1e9999", "1e-9999",
            "0.1000000000000000001", "123456789012345678901234567890",
            "3.14159265358979323846264338327950288", "-0", "+5", ".5", "5.", "1e", "inf", "NaN",
            "0x1F", "1 000", "12,34", "1,2345", "٣", "1e999999999", "0e5", "00012", "-.5",
            "1.7976931348623157e308", "4.9e-324", "\t7\n", "1,234,5", "--5", "2.5e-3",
            "0.333333333333333333333333333333333333333", "9.99999999999999e9999",
            "-9.99999999999999e+9999", "9.999999999999995e-10000", "-1e-9999", "1.0e-10000",
            "99999999999999.95", "0.0000099999999999999995"};
        if (chance(0.3))
            return number();
        return pastes.at(below(pastes.size()));
    }

    int below(int count) { return std::uniform_int_distribution<int>(0, count - 1)(m_random); }
    bool chance(double probability) {
        return std::uniform_real_distribution<double>(0, 1)(m_random) < probability;
    }

    static Action key(const QString &text) { return Action{Action::Key, text}; }

    // Keys for a number, with its minus typed as the sign key before or
    // after the digits, as people do both.
    void type(QList<Action> &actions, const QString &text) {
        const bool negative = text.startsWith(QLatin1Char('-'));
        const bool signFirst = chance(0.5);
        if (negative && signFirst)
            actions << key(QStringLiteral("sign"));
        for (const QChar character : text.mid(negative ? 1 : 0))
            actions << key(QString(character));
        if (negative && !signFirst)
            actions << key(QStringLiteral("sign"));
    }

private:
    QString digits(int count, bool leadingNonZero = true) {
        QString text;
        for (int i = 0; i < count; ++i)
            text += QChar(QLatin1Char('0' + (i == 0 && leadingNonZero ? 1 + below(9) : below(10))));
        return text;
    }

    QString nudged(QString text) {
        if (chance(0.3))
            return text;
        for (int i = text.size() - 1; i >= 0; --i) {
            if (!text.at(i).isDigit())
                continue;
            const int digit = text.at(i).digitValue();
            text[i] = QChar(QLatin1Char('0' + (digit + 1 + below(9)) % 10));
            return text;
        }
        return text;
    }

    std::mt19937_64 m_random;
    QString m_last;
};

struct Stats {
    int sequences = 0;
    int keys = 0;
    int approximated = 0;
    int lost = 0;
    qint64 slowestMilliseconds = 0;
    QString slowest;
};

struct Failure {
    int step;
    QString message;
};

std::optional<mpq_class> exactValue(const Backend &calculator) {
    const std::optional<Number> value = calculator.value();
    if (!value)
        return std::nullopt;
    return value->fraction();
}

std::optional<Failure> run(const QList<Action> &actions, Stats *stats = nullptr) {
    Backend calculator;
    reference::Calculator model;
    QClipboard *clipboard = QGuiApplication::clipboard();

    for (int step = 0; step < actions.size(); ++step) {
        const Action &action = actions.at(step);
        QElapsedTimer timer;
        timer.start();
        switch (action.kind) {
        case Action::Key:
            calculator.pressKey(action.text);
            model.press(action.text);
            break;
        case Action::Paste:
            clipboard->setText(action.text);
            calculator.pasteNumber();
            model.paste(action.text);
            break;
        case Action::CopyPaste:
        case Action::CopyPasteGrouped: {
            QString plain = calculator.display();
            plain.remove(QLatin1Char(','));
            calculator.copyResult();
            if (clipboard->text() != plain)
                return Failure{step, QStringLiteral("copied \"") + clipboard->text()
                                         + QStringLiteral("\" from a display of \"")
                                         + calculator.display() + QStringLiteral("\"")};
            const QString text = action.kind == Action::CopyPaste ? plain : calculator.display();
            clipboard->setText(text);
            calculator.pasteNumber();
            model.paste(text);
            break;
        }
        }
        const qint64 elapsed = timer.elapsed();
        if (elapsed > kSlowKeyMilliseconds)
            return Failure{step, QStringLiteral("took ") + QString::number(elapsed)
                                     + QStringLiteral(" ms")};
        if (stats) {
            ++stats->keys;
            if (elapsed > stats->slowestMilliseconds) {
                stats->slowestMilliseconds = elapsed;
                stats->slowest = describe(actions.mid(0, step + 1));
            }
        }

        const QString problem = model.check(calculator.display(), calculator.expression(),
                                            exactValue(calculator));
        if (!problem.isEmpty())
            return Failure{step, problem};
        if (model.lost()) {
            if (stats)
                ++stats->lost;
            return std::nullopt;
        }
    }
    if (stats && model.approximated())
        ++stats->approximated;
    return std::nullopt;
}

// The fewest actions that still fail: everything after the failing step goes,
// then runs of actions, halving the run, then single actions.
QList<Action> shrink(QList<Action> actions, int failingStep) {
    actions = actions.mid(0, failingStep + 1);
    for (int chunk = qMax(1, int(actions.size()) / 2); chunk >= 1; chunk /= 2) {
        bool removedAny = true;
        while (removedAny) {
            removedAny = false;
            for (int start = 0; start + chunk <= actions.size();) {
                QList<Action> candidate = actions;
                candidate.remove(start, chunk);
                if (!candidate.isEmpty() && run(candidate)) {
                    actions = candidate;
                    removedAny = true;
                } else {
                    start += chunk;
                }
            }
        }
    }
    return actions;
}

QString typed(Generator &generator, const QString &number) {
    QList<Action> actions;
    generator.type(actions, number);
    QStringList keys;
    for (const Action &action : actions)
        keys << action.text;
    return keys.join(QLatin1Char(' '));
}

QString displayAfter(Backend &calculator, const QString &keys) {
    calculator.pressKey(QStringLiteral("clear"));
    for (const QString &key : keys.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        calculator.pressKey(key);
    return calculator.display();
}

QString canonical(const QString &number) {
    return reference::group(reference::format(*reference::parse(number)));
}
}

class FuzzTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Backend drops saved window keys; keep it off the real settings.
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void matchesTheReferenceModel() {
        const quint64 seed = environmentNumber("OMACALC_FUZZ_SEED", 1);
        const quint64 sequences = environmentNumber("OMACALC_FUZZ_SEQUENCES", 6000);
        Stats stats;
        QElapsedTimer timer;
        timer.start();
        for (quint64 index = 0; index < sequences; ++index) {
            std::seed_seq sequenceSeed{seed, index};
            Generator generator(sequenceSeed);
            const QList<Action> actions = generator.sequence();
            ++stats.sequences;
            const std::optional<Failure> failure = run(actions, &stats);
            if (!failure)
                continue;

            const QList<Action> smallest = shrink(actions, failure->step);
            const std::optional<Failure> smallestFailure = run(smallest);
            QFAIL(qPrintable(QStringLiteral("seed %1, sequence %2\n  keys: %3\n  %4\n"
                                            "  as first found, at key %5 of: %6\n  %7")
                                 .arg(seed)
                                 .arg(index)
                                 .arg(describe(smallest))
                                 .arg(smallestFailure ? smallestFailure->message
                                                      : QStringLiteral("(passes when shrunk)"))
                                 .arg(failure->step + 1)
                                 .arg(describe(actions))
                                 .arg(failure->message)));
        }

        qInfo().noquote() << QStringLiteral(
            "%1 calculations, %2 keys in %3 s; %4 needed an approximation, %5 stopped early "
            "where no bound exists; slowest key %6 ms")
                                 .arg(stats.sequences)
                                 .arg(stats.keys)
                                 .arg(timer.elapsed() / 1000.0, 0, 'f', 1)
                                 .arg(stats.approximated)
                                 .arg(stats.lost)
                                 .arg(stats.slowestMilliseconds);
        // A model that gives up on many calculations checks little.
        QVERIFY2(stats.lost * 100 < stats.sequences,
                 qPrintable(QString::number(stats.lost) + " calculations stopped early"));
    }

    // Truths any calculator must keep, checked on the backend alone, with
    // random numbers of up to fifteen digits.
    void holdsArithmeticIdentities() {
        const quint64 seed = environmentNumber("OMACALC_FUZZ_SEED", 1);
        const quint64 rounds = environmentNumber("OMACALC_FUZZ_SEQUENCES", 6000) / 6;
        Backend calculator;
        const auto after = [&calculator](const QString &keys) {
            return displayAfter(calculator, keys);
        };
        for (quint64 index = 0; index < rounds; ++index) {
            std::seed_seq roundSeed{seed, index, quint64(0x1de)};
            Generator generator(roundSeed);
            const QString a = generator.number();
            const QString b = generator.number();
            const QString keysA = typed(generator, a);
            const QString keysB = typed(generator, b);
            const QString shownA = canonical(a);
            const bool bIsZero = reference::parse(b) == mpq_class(0);
            QString problem;
            const auto expect = [&](const QString &keys, const QString &expected) {
                const QString shown = after(keys);
                if (problem.isEmpty() && shown != expected)
                    problem = QStringLiteral("a = %1, b = %2: %3 shows %4, expected %5")
                                  .arg(a, b, keys, shown, expected);
            };

            expect(keysA + " + " + keysB + " - " + keysB + " =", shownA);
            expect(keysA + " - " + keysB + " + " + keysB + " =", shownA);
            expect(keysA + " - " + keysA + " =", "0");
            expect(keysA + " + " + keysB + " =", after(keysB + " + " + keysA + " ="));
            expect(keysA + " × " + keysB + " =", after(keysB + " × " + keysA + " ="));
            expect(keysA + " × 5 0 % =", after(keysA + " ÷ 2 ="));
            expect(keysA + " %", after(keysA + " ÷ 1 0 0 ="));
            if (!bIsZero) {
                expect(keysA + " × " + keysB + " ÷ " + keysB + " =", shownA);
                expect(keysA + " ÷ " + keysB + " × " + keysB + " =", shownA);
                expect(keysA + " ÷ " + keysB + " = × " + keysB + " =", shownA);
            }
            // Squaring then taking the square root finds the exact root.
            QString magnitude = shownA;
            if (magnitude.startsWith(QLatin1Char('-')))
                magnitude.remove(0, 1);
            expect(keysA + " ^ 2 = ^ . 5 =", magnitude);

            // An odd power of -x is minus the power of x, also when the power
            // is too long to hold exactly and is rounded.
            static const QStringList oddPowers = {"3", "12345", "54321", "99999", "100001"};
            const QString nearOne = QStringLiteral("1.0") + QString::number(generator.below(100000));
            const QString odd = oddPowers.at(generator.below(oddPowers.size()));
            const QString positive = after(typed(generator, nearOne) + " ^ " + typed(generator, odd) + " =");
            if (positive != QStringLiteral("Error"))
                expect(typed(generator, nearOne) + " sign ^ " + typed(generator, odd) + " =",
                       QStringLiteral("-") + positive);

            if (!problem.isEmpty())
                QFAIL(qPrintable(problem));
        }
    }

    // Calculations chosen to be as expensive as the calculator allows; every
    // key must still answer at once.
    void staysResponsive() {
        const QList<QList<Action>> heavy = {
            {Generator::key("1"), Generator::key("."), Generator::key("0"), Generator::key("0"),
             Generator::key("0"), Generator::key("0"), Generator::key("0"), Generator::key("0"),
             Generator::key("1"), Generator::key("^"), Action{Action::Paste, "1e9999"},
             Generator::key("=")},
            {Generator::key("2"), Generator::key("^"), Action{Action::Paste, "1e-9998"},
             Generator::key("=")},
            {Action{Action::Paste, "1.0000000000000000000000000000000000000001"},
             Generator::key("^"), Action{Action::Paste, "1e45"}, Generator::key("=")},
            {Action{Action::Paste, "0.333333333333333333333333333333333333333"},
             Generator::key("^"), Action{Action::Paste, "123456.789"}, Generator::key("=")},
            // 2^63 times the base's 2 bits wraps a 64-bit size check to 0.
            {Generator::key("2"), Generator::key("^"), Generator::key("2"), Generator::key("^"),
             Generator::key("6"), Generator::key("3"), Generator::key("=")},
        };
        QList<Action> squaring = {Generator::key("7"), Generator::key("÷"), Generator::key("3"),
                                  Generator::key("=")};
        for (int i = 0; i < 24; ++i) {
            squaring << Generator::key("^") << Generator::key("2") << Generator::key("=")
                     << Generator::key("+") << Generator::key("1") << Generator::key("÷")
                     << Generator::key("3") << Generator::key("=");
        }
        QList<QList<Action>> all = heavy;
        all << squaring;
        for (const QList<Action> &actions : all) {
            Stats stats;
            const std::optional<Failure> failure = run(actions, &stats);
            QVERIFY2(!failure, failure ? qPrintable(describe(actions) + ": " + failure->message) : "");
            QVERIFY2(stats.slowestMilliseconds < 250,
                     qPrintable(stats.slowest + ": " + QString::number(stats.slowestMilliseconds) + " ms"));
        }
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(FuzzTest)
#include "fuzz_tests.moc"
