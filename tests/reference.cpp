#include "reference.h"

#include "number.h"

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cstdlib>
#include <mpfr.h>

namespace reference {
namespace {

// The backend's promises, restated rather than read from Number, so changing
// one there is a deliberate change here too.
constexpr int kDigits = 15;
constexpr long kMaxExponent = 9999;
constexpr unsigned long kExactBits = 65536;
// A rounded result is within a relative 2^-250 of the true result of the
// numbers it was computed from (Number rounds to 256 bits).
constexpr unsigned long kToleranceBits = 250;
static_assert(kDigits == Number::kDisplayDigits);
static_assert(kMaxExponent == Number::kMaxDecimalExponent);
static_assert(kExactBits == Number::kMaxExactBits);
static_assert(kToleranceBits + 6 <= Number::kApproximationBits);

const QString plusSign = QStringLiteral("+");
const QString minusSign = QStringLiteral("−");
const QString timesSign = QStringLiteral("×");
const QString divideSign = QStringLiteral("÷");
const QString powerSign = QStringLiteral("^");

struct Float {
    mpfr_t v;
    explicit Float(mpfr_prec_t precision) { mpfr_init2(v, precision); }
    ~Float() { mpfr_clear(v); }
    Float(const Float &) = delete;
    Float &operator=(const Float &) = delete;
};

long bits(const mpz_class &value) {
    return long(mpz_sizeinbase(value.get_mpz_t(), 2));
}

mpq_class tenTo(long exponent) {
    mpz_class power;
    mpz_ui_pow_ui(power.get_mpz_t(), 10, std::labs(exponent));
    if (exponent >= 0)
        return mpq_class(power);
    mpq_class inverse(mpz_class(1), power);
    inverse.canonicalize();
    return inverse;
}

mpq_class twoTo(long exponent) {
    mpz_class power = 1;
    mpz_mul_2exp(power.get_mpz_t(), power.get_mpz_t(), std::labs(exponent));
    if (exponent >= 0)
        return mpq_class(power);
    mpq_class inverse(mpz_class(1), power);
    inverse.canonicalize();
    return inverse;
}

// floor(log10 a) for a > 0: a float estimate, then settled exactly.
long floorLog10(const mpq_class &a) {
    Float estimate(64);
    mpfr_set_q(estimate.v, a.get_mpq_t(), MPFR_RNDN);
    mpfr_log10(estimate.v, estimate.v, MPFR_RNDN);
    long exponent = mpfr_get_si(estimate.v, MPFR_RNDD);
    while (a < tenTo(exponent))
        --exponent;
    while (a >= tenTo(exponent + 1))
        ++exponent;
    return exponent;
}

struct Rounded {
    mpz_class significand;  // exactly kDigits digits
    long exponent;
};

// a > 0 rounded to kDigits significant digits, half away from zero.
Rounded roundPositive(const mpq_class &a) {
    long exponent = floorLog10(a);
    const mpq_class scaled = a / tenTo(exponent - (kDigits - 1));
    mpz_class whole;
    mpz_fdiv_q(whole.get_mpz_t(), scaled.get_num_mpz_t(), scaled.get_den_mpz_t());
    const mpq_class rest = scaled - mpq_class(whole);
    if (rest >= mpq_class(mpz_class(1), mpz_class(2)))
        ++whole;
    if (mpq_class(whole) == tenTo(kDigits)) {
        whole = tenTo(kDigits - 1).get_num();
        ++exponent;
    }
    return {whole, exponent};
}

enum class Range { Below, Inside, Above };

Range rangeOf(const mpq_class &value) {
    if (value == 0)
        return Range::Inside;
    const long magnitude = bits(value.get_num()) - bits(value.get_den());
    if (magnitude > 34000)
        return Range::Above;
    if (magnitude < -34000)
        return Range::Below;
    const long exponent = roundPositive(abs(value)).exponent;
    if (exponent > kMaxExponent)
        return Range::Above;
    if (exponent < -kMaxExponent)
        return Range::Below;
    return Range::Inside;
}

// Plain decimal text exactly, without the range check.
std::optional<mpq_class> parseUnbounded(const QString &text) {
    static const QRegularExpression pattern(
        QStringLiteral("^([+-]?)([0-9]*)(?:\\.([0-9]*))?(?:[eE]([+-]?[0-9]{1,9}))?$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    const QString whole = match.captured(2);
    const QString fraction = match.captured(3);
    QString digits = whole + fraction;
    if (digits.isEmpty())
        return std::nullopt;

    while (digits.size() > 1 && digits.startsWith(QLatin1Char('0')))
        digits.remove(0, 1);
    if (digits == QStringLiteral("0"))
        return mpq_class(0);

    long exponent = match.captured(4).isEmpty() ? 0 : match.captured(4).toLong();
    exponent -= fraction.size();
    // Far outside the range whatever the digits; don't build the power.
    const long leading = exponent + digits.size() - 1;
    if (leading > 20000 || leading < -20000)
        return leading > 0 ? std::optional<mpq_class>(tenTo(20000))
                           : std::optional<mpq_class>(tenTo(-20000));

    mpq_class value(mpz_class(digits.toStdString(), 10));
    value *= tenTo(exponent);
    if (match.captured(1) == QStringLiteral("-"))
        value = -value;
    return value;
}

Enclosure negate(const Enclosure &e) {
    return {-e.hi, -e.lo};
}

Enclosure widen(const Enclosure &e) {
    const mpq_class size = std::max(abs(e.lo), abs(e.hi)) * twoTo(-long(kToleranceBits));
    return {e.lo - size, e.hi + size};
}

Outcome failure() {
    Outcome outcome;
    outcome.canFail = true;
    outcome.canSucceed = false;
    return outcome;
}

Outcome lostTrack() {
    Outcome outcome;
    outcome.lost = true;
    return outcome;
}

// Whether the backend can show a value, or Error, for a result it holds
// somewhere in e.
Outcome classify(const Enclosure &e) {
    Outcome outcome;
    outcome.value = e;
    const Range low = rangeOf(e.lo);
    const Range high = rangeOf(e.hi);
    bool inside;
    bool outside;
    if (e.isExact()) {
        inside = low == Range::Inside;
        outside = !inside;
    } else if (e.lo <= 0 && e.hi >= 0) {
        // Zero, and nonzero values too small for the display, are both in it.
        inside = true;
        outside = true;
    } else if (low != high) {
        inside = true;
        outside = true;
    } else {
        inside = low == Range::Inside;
        outside = !inside;
    }
    outcome.canSucceed = inside;
    outcome.canFail = outside;
    return outcome;
}

// The backend may round a result of exact numbers only when its numerator or
// denominator outgrows kExactBits; anything that fits must come out exact.
Outcome settle(Enclosure raw, bool mayRound) {
    if (!mayRound && raw.isExact()
            && std::max(bits(raw.lo.get_num()), bits(raw.lo.get_den())) > long(kExactBits))
        mayRound = true;
    if (mayRound)
        raw = widen(raw);
    return classify(raw);
}

Outcome succeedExactly(const mpq_class &value) {
    return settle(Enclosure::exact(value), false);
}

// An MPFR bound turned into a fraction, clamped well outside the range where
// MPFR's own range is wider than any fraction worth building.
mpq_class boundToFraction(const mpfr_t bound) {
    if (mpfr_inf_p(bound) || (mpfr_regular_p(bound) && mpfr_get_exp(bound) > 34000))
        return twoTo(34000);
    if (mpfr_zero_p(bound) || mpfr_get_exp(bound) < -34000)
        return twoTo(-34000);
    mpq_class value;
    mpfr_get_q(value.get_mpq_t(), bound);
    value.canonicalize();
    return value;
}

// Proven bounds on x^y over x in [x.lo, x.hi] (x.lo > 0) and y in
// [y.lo, y.hi]. y·ln x is bilinear in y and ln x, so the extremes are at the
// corners; each corner is rounded outward.
Outcome boundedPower(const Enclosure &x, const Enclosure &y) {
    long exponentBits = 0;
    for (const mpq_class *end : {&y.lo, &y.hi})
        exponentBits = std::max(exponentBits, bits(end->get_num()) - bits(end->get_den()));
    const mpfr_prec_t precision = 512 + exponentBits;

    Float xs[2] = {Float(precision), Float(precision)};
    Float ys[2] = {Float(precision), Float(precision)};
    mpfr_set_q(xs[0].v, x.lo.get_mpq_t(), MPFR_RNDD);
    mpfr_set_q(xs[1].v, x.hi.get_mpq_t(), MPFR_RNDU);
    mpfr_set_q(ys[0].v, y.lo.get_mpq_t(), MPFR_RNDD);
    mpfr_set_q(ys[1].v, y.hi.get_mpq_t(), MPFR_RNDU);

    Float low(precision);
    Float high(precision);
    Float corner(precision);
    bool first = true;
    for (Float &base : xs) {
        for (Float &exponent : ys) {
            mpfr_pow(corner.v, base.v, exponent.v, MPFR_RNDD);
            if (first || mpfr_less_p(corner.v, low.v))
                mpfr_set(low.v, corner.v, MPFR_RNDD);
            mpfr_pow(corner.v, base.v, exponent.v, MPFR_RNDU);
            if (first || mpfr_greater_p(corner.v, high.v))
                mpfr_set(high.v, corner.v, MPFR_RNDU);
            first = false;
        }
    }
    return settle({boundToFraction(low.v), boundToFraction(high.v)}, true);
}

bool containsInteger(const Enclosure &e) {
    mpz_class ceiling;
    mpz_cdiv_q(ceiling.get_mpz_t(), e.lo.get_num_mpz_t(), e.lo.get_den_mpz_t());
    return mpq_class(ceiling) <= e.hi;
}

// c ≥ 1. A whole q-th root of c ≥ 2 is at least 2, so c ≥ 2^q.
bool wholeRoot(const mpz_class &c, const mpz_class &q, mpz_class &root) {
    if (c == 1) {
        root = 1;
        return true;
    }
    if (!q.fits_ulong_p() || q.get_ui() >= mpz_sizeinbase(c.get_mpz_t(), 2))
        return false;
    mpz_class remainder;
    mpz_rootrem(root.get_mpz_t(), remainder.get_mpz_t(), c.get_mpz_t(), q.get_ui());
    if (remainder != 0)
        return false;
    mpz_class back;
    mpz_pow_ui(back.get_mpz_t(), root.get_mpz_t(), q.get_ui());
    return back == c;
}

Outcome exactWholePower(const mpq_class &x, const mpz_class &n) {
    if (n == 0)
        return succeedExactly(1);
    if (x == 0)
        return n > 0 ? succeedExactly(0) : failure();
    if (abs(x) == 1)
        return succeedExactly(x > 0 || mpz_even_p(n.get_mpz_t()) ? 1 : -1);

    // Big enough to cover every power whose fraction fits kExactBits, which
    // must come out exact. Divided rather than multiplied: 2 ^ 2 ^ 63 would
    // wrap 2^63 × 2 to zero.
    const unsigned long widest = std::max(bits(x.get_num()), bits(x.get_den()));
    const mpz_class count = abs(n);
    if (count.fits_ulong_p() && count.get_ui() <= 4 * kExactBits / widest) {
        mpz_class numerator;
        mpz_class denominator;
        mpz_pow_ui(numerator.get_mpz_t(), x.get_num_mpz_t(), count.get_ui());
        mpz_pow_ui(denominator.get_mpz_t(), x.get_den_mpz_t(), count.get_ui());
        mpq_class result(numerator, denominator);
        result.canonicalize();
        if (n < 0)
            result = 1 / result;
        return succeedExactly(result);
    }

    // Too long to multiply out; bound |x|^n and give it the sign of x^n.
    const Enclosure magnitude = Enclosure::exact(abs(x));
    Outcome outcome = boundedPower(magnitude, Enclosure::exact(mpq_class(n)));
    if (x < 0 && mpz_odd_p(n.get_mpz_t()))
        outcome.value = negate(outcome.value);
    return outcome;
}

Outcome exactPower(const mpq_class &x, const mpq_class &y) {
    if (y.get_den() == 1)
        return exactWholePower(x, y.get_num());
    if (x < 0)
        return failure();
    if (x == 0)
        return y > 0 ? succeedExactly(0) : failure();

    mpz_class numeratorRoot;
    mpz_class denominatorRoot;
    if (wholeRoot(x.get_num(), y.get_den(), numeratorRoot)
            && wholeRoot(x.get_den(), y.get_den(), denominatorRoot)) {
        mpq_class root(numeratorRoot, denominatorRoot);
        root.canonicalize();
        return exactWholePower(root, y.get_num());
    }
    return boundedPower(Enclosure::exact(x), Enclosure::exact(y));
}

Outcome add(const Enclosure &a, const Enclosure &b) {
    return settle({a.lo + b.lo, a.hi + b.hi}, !a.isExact() || !b.isExact());
}

Outcome subtract(const Enclosure &a, const Enclosure &b) {
    return settle({a.lo - b.hi, a.hi - b.lo}, !a.isExact() || !b.isExact());
}

Enclosure productRange(const Enclosure &a, const Enclosure &b) {
    const mpq_class corners[4] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    return {*std::min_element(corners, corners + 4), *std::max_element(corners, corners + 4)};
}

Outcome multiply(const Enclosure &a, const Enclosure &b) {
    return settle(productRange(a, b), !a.isExact() || !b.isExact());
}

Outcome divide(const Enclosure &a, const Enclosure &b) {
    if (b.isExact() && b.lo == 0)
        return failure();
    if (b.lo <= 0 && b.hi >= 0)
        return lostTrack();
    const Enclosure inverse = {1 / b.hi, 1 / b.lo};
    return settle(productRange(a, inverse), !a.isExact() || !b.isExact());
}

Outcome power(const Enclosure &x, const Enclosure &y) {
    if (x.isExact() && y.isExact())
        return exactPower(x.lo, y.lo);

    if (y.isExact() && y.lo.get_den() == 1) {
        const mpz_class &n = y.lo.get_num();
        if (n == 0)
            return succeedExactly(1);
        if (x.lo > 0)
            return boundedPower(x, y);
        if (x.hi < 0) {
            Outcome outcome = boundedPower(negate(x), y);
            if (mpz_odd_p(n.get_mpz_t()))
                outcome.value = negate(outcome.value);
            return outcome;
        }
        return lostTrack();
    }

    if (x.lo > 0)
        return boundedPower(x, y);
    if (x.isExact() && x.lo == 0) {
        if (y.lo > 0)
            return succeedExactly(0);
        if (y.hi < 0)
            return failure();
        return lostTrack();
    }
    if (x.hi < 0 && !containsInteger(y))
        return failure();
    return lostTrack();
}

QString sealed(const QString &entry) {
    QString text = entry;
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    if (text.isEmpty() || text == QStringLiteral("-"))
        return QStringLiteral("0");
    return text;
}

bool exponentForm(const QString &entry) {
    return entry.contains(QLatin1Char('e'));
}

// A fraction in full when it is short, and by its size when a failure
// message would otherwise run to thousands of digits.
QString fraction(const mpq_class &value) {
    const std::string full = value.get_str();
    if (full.size() <= 80)
        return QString::fromStdString(full);
    return QStringLiteral("a fraction of %1 over %2 bits")
        .arg(bits(value.get_num()))
        .arg(bits(value.get_den()));
}

QString describe(const Enclosure &e) {
    if (e.isExact())
        return fraction(e.lo) + QStringLiteral(" = ") + format(e.lo);
    return QStringLiteral("[") + format(e.lo) + QStringLiteral(", ") + format(e.hi)
        + QStringLiteral("]");
}
}

QString format(const mpq_class &value) {
    if (value == 0)
        return QStringLiteral("0");

    const Rounded rounded = roundPositive(abs(value));
    QString digits = QString::fromStdString(rounded.significand.get_str());
    int used = digits.size();
    while (used > 1 && digits.at(used - 1) == QLatin1Char('0'))
        --used;
    digits.truncate(used);

    QString body;
    const long exponent = rounded.exponent;
    if (exponent >= kDigits || exponent < -4) {
        body = digits.left(1);
        if (digits.size() > 1)
            body += QLatin1Char('.') + digits.mid(1);
        body += exponent < 0 ? QStringLiteral("e-") : QStringLiteral("e+");
        body += QString::number(std::labs(exponent)).rightJustified(2, QLatin1Char('0'));
    } else if (exponent >= 0) {
        const int whole = int(exponent) + 1;
        const QString padded = digits.leftJustified(whole, QLatin1Char('0'));
        body = padded.left(whole);
        if (padded.size() > whole)
            body += QLatin1Char('.') + padded.mid(whole);
    } else {
        body = QStringLiteral("0.") + QString(int(-exponent - 1), QLatin1Char('0')) + digits;
    }
    return value < 0 ? QStringLiteral("-") + body : body;
}

std::optional<mpq_class> parse(const QString &text) {
    const std::optional<mpq_class> value = parseUnbounded(text);
    if (!value || rangeOf(*value) != Range::Inside)
        return std::nullopt;
    return value;
}

QString group(const QString &plain) {
    const int start = plain.startsWith(QLatin1Char('-')) ? 1 : 0;
    int end = start;
    while (end < plain.size() && plain.at(end) >= QLatin1Char('0') && plain.at(end) <= QLatin1Char('9'))
        ++end;
    const QString whole = plain.mid(start, end - start);
    QString grouped;
    for (int i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0)
            grouped += QLatin1Char(',');
        grouped += whole.at(i);
    }
    return plain.left(start) + grouped + plain.mid(end);
}

bool matches(const QString &shown, const Enclosure &value) {
    const QString low = format(value.lo);
    if (value.isExact())
        return shown == low;
    const QString high = format(value.hi);
    if (low == high)
        return shown == low;

    // Rounding never decreases as the value grows, so the right displays are
    // exactly the canonical fifteen-digit numbers between these two.
    const std::optional<mpq_class> number = parseUnbounded(shown);
    if (!number || format(*number) != shown)
        return false;
    return *parseUnbounded(low) <= *number && *number <= *parseUnbounded(high);
}

void Calculator::press(const QString &key) {
    if (m_lost)
        return;
    if (key.size() == 1 && key.at(0) >= QLatin1Char('0') && key.at(0) <= QLatin1Char('9'))
        pressDigit(key);
    else if (key == QStringLiteral("."))
        pressDecimal();
    else if (key == QStringLiteral("+"))
        pressOperator(plusSign);
    else if (key == QStringLiteral("-") || key == minusSign)
        pressOperator(minusSign);
    else if (key == QStringLiteral("*") || key == timesSign)
        pressOperator(timesSign);
    else if (key == QStringLiteral("/") || key == divideSign)
        pressOperator(divideSign);
    else if (key == QStringLiteral("^"))
        pressOperator(powerSign);
    else if (key == QStringLiteral("="))
        pressEquals();
    else if (key == QStringLiteral("%"))
        pressPercent();
    else if (key == QStringLiteral("sign"))
        pressSign();
    else if (key == QStringLiteral("backspace"))
        pressBackspace();
    else if (key == QStringLiteral("clear"))
        clearAll();
}

void Calculator::paste(const QString &clipboardText) {
    if (m_lost)
        return;
    QString text = clipboardText.trimmed();
    text.replace(minusSign, QStringLiteral("-"));
    text.remove(QLatin1Char(' '));
    static const QRegularExpression grouped(
        QStringLiteral("^-?[0-9]{1,3}(,[0-9]{3})+(\\.[0-9]*)?$"));
    if (grouped.match(text).hasMatch())
        text.remove(QLatin1Char(','));

    std::optional<mpq_class> value = parse(text);
    if (!value) {
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        value = parse(text);
    }
    if (!value)
        return;
    if (m_errored || m_justEvaluated)
        clearAll();
    setEntryValue(Enclosure::exact(*value));
}

void Calculator::pressDigit(const QString &digit) {
    if (m_errored || m_justEvaluated)
        clearAll();
    QString entry = exponentForm(m_entry) ? QString() : m_entry;
    if (entry == QStringLiteral("0")) {
        entry = digit;
    } else if (entry == QStringLiteral("-0")) {
        entry = QStringLiteral("-") + digit;
    } else {
        int count = 0;
        for (const QChar character : entry)
            count += character.isDigit() ? 1 : 0;
        if (entry.startsWith(QStringLiteral("0.")) || entry.startsWith(QStringLiteral("-0.")))
            --count;
        if (count < kDigits)
            entry += digit;
    }
    if (entry != m_entry)
        setEntryText(entry);
}

void Calculator::pressDecimal() {
    if (m_errored || m_justEvaluated)
        clearAll();
    QString entry = exponentForm(m_entry) ? QString() : m_entry;
    if (entry.isEmpty())
        entry = QStringLiteral("0.");
    else if (!entry.contains(QLatin1Char('.')))
        entry += QLatin1Char('.');
    if (entry != m_entry)
        setEntryText(entry);
}

void Calculator::pressOperator(const QString &op) {
    if (m_errored)
        return;
    if (m_justEvaluated) {
        m_tokens = {Item{QString(), m_result}};
        clearEvaluation();
    }
    if (!m_entry.isEmpty()) {
        m_tokens << Item{QString(), m_entryValue} << Item{op, {}};
        clearEntry();
    } else if (m_tokens.isEmpty()) {
        m_tokens << Item{QString(), Enclosure::exact(0)} << Item{op, {}};
    } else if (m_tokens.last().isOperator()) {
        m_tokens.last().op = op;
    } else {
        m_tokens << Item{op, {}};
    }
}

void Calculator::pressEquals() {
    if (m_errored || m_justEvaluated)
        return;
    QList<Item> items = m_tokens;
    if (!m_entry.isEmpty())
        items << Item{QString(), m_entryValue};
    while (!items.isEmpty() && items.last().isOperator())
        items.removeLast();
    if (items.isEmpty())
        return;

    m_evaluated = items;
    m_evaluatedSuffix.clear();
    const Outcome outcome = evaluate(items);
    m_tokens.clear();
    clearEntry();
    if (outcome.lost) {
        m_lost = true;
    } else if (!outcome.canSucceed) {
        m_errored = true;
    } else if (!outcome.canFail) {
        setResult(outcome.value);
    } else {
        const Enclosure value = outcome.value;
        m_pending = [this, value](bool errored) {
            if (errored)
                m_errored = true;
            else
                setResult(value);
        };
    }
}

void Calculator::pressPercent() {
    if (m_errored)
        return;
    const Enclosure value = m_justEvaluated ? m_result : currentValue();
    if (m_justEvaluated)
        clearEvaluation();

    Outcome percent = settle({value.lo / 100, value.hi / 100}, !value.isExact());
    if (!m_tokens.isEmpty()
            && (m_tokens.last().op == plusSign || m_tokens.last().op == minusSign)) {
        const Outcome total = evaluate(m_tokens.mid(0, m_tokens.size() - 1));
        if (total.lost || (total.canFail && total.canSucceed)) {
            // Which of the two percent rules the backend used depends on
            // its rounding; rare enough to stop checking here.
            m_lost = true;
            return;
        }
        if (total.canSucceed) {
            const Enclosure product = productRange(total.value, value);
            percent = settle({product.lo / 100, product.hi / 100},
                             !total.value.isExact() || !value.isExact());
        }
    }

    const auto fail = [this, value]() {
        m_evaluated = m_tokens;
        m_evaluated << Item{QString(), value};
        m_evaluatedSuffix = QStringLiteral(" %");
        m_errored = true;
        m_tokens.clear();
        clearEntry();
    };
    if (!percent.canSucceed) {
        fail();
    } else if (!percent.canFail) {
        setEntryValue(percent.value);
    } else {
        const Enclosure result = percent.value;
        m_pending = [this, fail, result](bool errored) {
            if (errored)
                fail();
            else
                setEntryValue(result);
        };
    }
}

void Calculator::pressSign() {
    if (m_errored)
        return;
    if (m_justEvaluated)
        beginEditingAfterResult();
    if (m_entry.isEmpty()) {
        setEntryText(QStringLiteral("-0"));
        return;
    }
    m_entryValue = negate(m_entryValue);
    if (m_entry.startsWith(QLatin1Char('-')))
        m_entry.remove(0, 1);
    else
        m_entry.prepend(QLatin1Char('-'));
}

void Calculator::pressBackspace() {
    if (m_errored) {
        clearAll();
        return;
    }
    if (m_justEvaluated)
        beginEditingAfterResult();
    if (exponentForm(m_entry)) {
        setEntryText(QStringLiteral("0"));
        return;
    }
    QString entry = m_entry;
    entry.chop(1);
    if (entry == QStringLiteral("-"))
        entry.clear();
    if (entry.isEmpty())
        clearEntry();
    else
        setEntryText(entry);
}

void Calculator::clearAll() {
    m_tokens.clear();
    clearEntry();
    clearEvaluation();
    m_errored = false;
}

void Calculator::setEntryText(const QString &text) {
    m_entry = text;
    m_entryTextKnown = true;
    m_entryValue = Enclosure::exact(parse(sealed(text)).value_or(0));
}

void Calculator::setEntryValue(const Enclosure &value) {
    m_entryValue = value;
    const QString low = format(value.lo);
    m_entryTextKnown = value.isExact() || low == format(value.hi);
    // Settled from the backend's display at the next check when unknown.
    m_entry = m_entryTextKnown ? low : QStringLiteral("?");
    if (!value.isExact())
        m_approximated = true;
}

void Calculator::clearEntry() {
    m_entry.clear();
    m_entryTextKnown = true;
    m_entryValue = Enclosure::exact(0);
}

void Calculator::setResult(const Enclosure &value) {
    m_result = value;
    const QString low = format(value.lo);
    m_resultTextKnown = value.isExact() || low == format(value.hi);
    m_resultText = m_resultTextKnown ? low : QString();
    m_justEvaluated = true;
    if (!value.isExact())
        m_approximated = true;
}

void Calculator::beginEditingAfterResult() {
    m_entry = m_resultText;
    m_entryTextKnown = true;
    m_entryValue = m_result;
    m_tokens.clear();
    clearEvaluation();
}

void Calculator::clearEvaluation() {
    m_result = Enclosure::exact(0);
    m_resultText.clear();
    m_resultTextKnown = true;
    m_evaluated.clear();
    m_evaluatedSuffix.clear();
    m_justEvaluated = false;
}

Enclosure Calculator::currentValue() const {
    if (!m_entry.isEmpty())
        return m_entryValue;
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (!m_tokens.at(i).isOperator())
            return m_tokens.at(i).value;
    }
    return Enclosure::exact(0);
}

Outcome Calculator::evaluate(const QList<Item> &items) {
    if (items.size() % 2 == 0)
        return failure();
    QList<Enclosure> values;
    QStringList ops;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).isOperator() != (i % 2 == 1))
            return failure();
        if (i % 2 == 1)
            ops << items.at(i).op;
        else
            values << items.at(i).value;
    }

    bool canFail = false;
    // Folds one step's outcome into the running one; false when the
    // calculation can go no further.
    Outcome stopped;
    const auto take = [&](const Outcome &step, Enclosure &into) {
        if (step.lost) {
            stopped = lostTrack();
            return false;
        }
        canFail = canFail || step.canFail;
        if (!step.canSucceed) {
            stopped = failure();
            return false;
        }
        if (!step.value.isExact())
            m_approximated = true;
        into = step.value;
        return true;
    };

    for (int i = ops.size() - 1; i >= 0; --i) {
        if (ops.at(i) != powerSign)
            continue;
        if (!take(power(values.at(i), values.at(i + 1)), values[i]))
            return stopped;
        values.removeAt(i + 1);
        ops.removeAt(i);
    }

    QList<Enclosure> terms = {values.first()};
    QStringList termOps;
    for (int i = 0; i < ops.size(); ++i) {
        if (ops.at(i) == timesSign || ops.at(i) == divideSign) {
            const Outcome step = ops.at(i) == timesSign ? multiply(terms.last(), values.at(i + 1))
                                                        : divide(terms.last(), values.at(i + 1));
            if (!take(step, terms.last()))
                return stopped;
        } else {
            termOps << ops.at(i);
            terms << values.at(i + 1);
        }
    }

    Enclosure total = terms.first();
    for (int i = 0; i < termOps.size(); ++i) {
        const Outcome step = termOps.at(i) == plusSign ? add(total, terms.at(i + 1))
                                                       : subtract(total, terms.at(i + 1));
        if (!take(step, total))
            return stopped;
    }

    Outcome outcome;
    outcome.canFail = canFail;
    outcome.value = total;
    return outcome;
}

QString Calculator::checkNumberText(const QString &what, const QString &shown,
                                    const Enclosure &value, QString *adopt) {
    QString plain = shown;
    plain.remove(QLatin1Char(','));
    if (group(plain) != shown)
        return what + QStringLiteral(" \"") + shown + QStringLiteral("\" is grouped wrongly");
    if (!matches(plain, value))
        return what + QStringLiteral(" shows \"") + shown + QStringLiteral("\" but the value is ")
            + describe(value);
    if (adopt)
        *adopt = plain;
    return {};
}

QString Calculator::checkExpression(const QString &expression) const {
    QList<Item> items;
    QString suffix;
    if (m_errored || m_justEvaluated) {
        items = m_evaluated;
        suffix = m_evaluatedSuffix;
    } else {
        items = m_tokens;
    }

    const QStringList parts = expression.isEmpty() ? QStringList()
                                                   : expression.split(QLatin1Char(' '));
    const int expectedParts = items.size() + (suffix.isEmpty() ? 0 : 1);
    if (parts.size() != expectedParts)
        return QStringLiteral("expression \"") + expression + QStringLiteral("\" has ")
            + QString::number(parts.size()) + QStringLiteral(" parts, expected ")
            + QString::number(expectedParts);
    for (int i = 0; i < items.size(); ++i) {
        const Item &item = items.at(i);
        if (item.isOperator()) {
            if (parts.at(i) != item.op)
                return QStringLiteral("expression \"") + expression + QStringLiteral("\" has \"")
                    + parts.at(i) + QStringLiteral("\" where \"") + item.op
                    + QStringLiteral("\" belongs");
            continue;
        }
        QString plain = parts.at(i);
        plain.remove(QLatin1Char(','));
        if (group(plain) != parts.at(i) || !matches(plain, item.value))
            return QStringLiteral("expression \"") + expression + QStringLiteral("\" shows \"")
                + parts.at(i) + QStringLiteral("\" for ") + describe(item.value);
    }
    if (!suffix.isEmpty() && parts.last() != suffix.trimmed())
        return QStringLiteral("expression \"") + expression + QStringLiteral("\" lacks \"")
            + suffix + QStringLiteral("\"");
    return {};
}

QString Calculator::check(const QString &display, const QString &expression,
                          const std::optional<mpq_class> &value) {
    if (m_lost)
        return {};
    if (m_pending) {
        const auto settleOutcome = std::move(m_pending);
        m_pending = nullptr;
        settleOutcome(display == QStringLiteral("Error"));
    }

    if (m_errored) {
        if (display != QStringLiteral("Error"))
            return QStringLiteral("display shows \"") + display + QStringLiteral("\", expected Error");
    } else if (!m_entry.isEmpty()) {
        if (m_entryTextKnown) {
            if (display != group(m_entry))
                return QStringLiteral("display shows \"") + display + QStringLiteral("\", expected \"")
                    + group(m_entry) + QStringLiteral("\"");
        } else {
            const QString problem = checkNumberText(QStringLiteral("entry"), display, m_entryValue,
                                                    &m_entry);
            if (!problem.isEmpty())
                return problem;
            m_entryTextKnown = true;
        }
    } else if (m_justEvaluated) {
        if (m_resultTextKnown) {
            if (display != group(m_resultText))
                return QStringLiteral("display shows \"") + display + QStringLiteral("\", expected \"")
                    + group(m_resultText) + QStringLiteral("\" (") + describe(m_result)
                    + QStringLiteral(")");
        } else {
            const QString problem = checkNumberText(QStringLiteral("result"), display, m_result,
                                                    &m_resultText);
            if (!problem.isEmpty())
                return problem;
            m_resultTextKnown = true;
        }
    } else {
        const QString problem = checkNumberText(QStringLiteral("display"), display, currentValue(),
                                                nullptr);
        if (!problem.isEmpty())
            return problem;
    }

    const QString expressionProblem = checkExpression(expression);
    if (!expressionProblem.isEmpty())
        return expressionProblem;

    if (m_errored) {
        if (value)
            return QStringLiteral("holds a value while showing Error");
    } else {
        const Enclosure expected = m_justEvaluated ? m_result : currentValue();
        if (!value)
            return QStringLiteral("holds no value, expected ") + describe(expected);
        if (!expected.contains(*value))
            return QStringLiteral("holds ") + fraction(*value) + QStringLiteral(" = ") + format(*value)
                + QStringLiteral(", outside ") + describe(expected);
    }
    return {};
}

}
