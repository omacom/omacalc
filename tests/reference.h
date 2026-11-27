#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <gmpxx.h>
#include <optional>

// A second, independent model of the calculator, for the fuzzer to hold the
// backend against after every key.
//
// It shares no arithmetic code with Number. Each value is kept as an
// enclosure: a range with exact fractional ends that the true value of the
// calculation lies in. Typed and pasted numbers, + − × ÷ and percent on them,
// and roots that come out exact all give single-point enclosures, and then the
// backend must show exactly the right digits. Irrational roots and powers too
// long to hold get a proven MPFR bound, widened by what the backend promises
// for a rounded result (a relative 2^-250), and then the backend must show a
// correct rounding of some value in the range.
namespace reference {

struct Enclosure {
    mpq_class lo;
    mpq_class hi;

    static Enclosure exact(const mpq_class &value) { return {value, value}; }
    bool isExact() const { return lo == hi; }
    bool contains(const mpq_class &value) const { return lo <= value && value <= hi; }
};

// What a calculation may come to. Usually exactly one of an error or a value
// in `value`; both when the true result is so close to the edge of the
// display's range, or to zero, that the backend's rounding decides it. Lost
// when no useful bound exists, like dividing by a range around zero.
struct Outcome {
    bool canFail = false;
    bool canSucceed = true;
    bool lost = false;
    Enclosure value;
};

// Fifteen significant digits, half away from zero, %g layout.
QString format(const mpq_class &value);
// Exact value of plain decimal text; nothing when malformed or out of range.
std::optional<mpq_class> parse(const QString &text);
// Thousands separators in the whole-number digits.
QString group(const QString &plain);
// Whether `shown` (without separators) is a correct fifteen-digit display of
// some value in `value`.
bool matches(const QString &shown, const Enclosure &value);

class Calculator {
public:
    void press(const QString &key);
    void paste(const QString &clipboardText);

    // Holds the backend's display, expression line and exact value against
    // the model. Where several displays are right (a rounded value near a
    // rounding boundary, or a result at the edge of the range), the backend's
    // choice is checked and adopted. Returns what differs, or an empty string.
    QString check(const QString &display, const QString &expression,
                  const std::optional<mpq_class> &value);

    // Set once the model can no longer bound the backend's value; nothing
    // after that point is checked.
    bool lost() const { return m_lost; }
    // Whether any value so far needed an approximation.
    bool approximated() const { return m_approximated; }

private:
    struct Item {
        QString op;
        Enclosure value;
        bool isOperator() const { return !op.isEmpty(); }
    };

    void pressDigit(const QString &digit);
    void pressDecimal();
    void pressOperator(const QString &op);
    void pressEquals();
    void pressPercent();
    void pressSign();
    void pressBackspace();
    void clearAll();

    void setEntryText(const QString &text);
    void setEntryValue(const Enclosure &value);
    void clearEntry();
    void setResult(const Enclosure &value);
    void beginEditingAfterResult();
    void clearEvaluation();
    Enclosure currentValue() const;
    Outcome evaluate(const QList<Item> &items);
    QString checkNumberText(const QString &what, const QString &shown, const Enclosure &value,
                            QString *adopt);
    QString checkExpression(const QString &expression) const;

    QList<Item> m_tokens;
    QString m_entry;
    Enclosure m_entryValue;
    bool m_entryTextKnown = true;
    Enclosure m_result;
    QString m_resultText;
    bool m_resultTextKnown = true;
    bool m_justEvaluated = false;
    bool m_errored = false;
    QList<Item> m_evaluated;
    QString m_evaluatedSuffix;
    std::function<void(bool errored)> m_pending;
    bool m_lost = false;
    bool m_approximated = false;
};

}
