#pragma once

#include <deque>
#include <string>
#include <vector>

namespace me::calculator {

enum class UnitCategory {
    Length,
    Weight,
    Temperature,
    CurrencyPlaceholder
};

struct HistoryEntry {
    std::string expression;
    std::string result;
};

class Calculator {
public:
    // Expression evaluation using radians for trigonometry.
    double evaluate(const std::string& expression);

    void append(const std::string& token);
    void backspace();
    void clear();
    const std::string& currentExpression() const;
    bool hasResult() const;
    double lastResult() const;

    double convert(UnitCategory category, const std::string& fromUnit,
                   const std::string& toUnit, double value) const;
    std::vector<std::string> supportedUnits(UnitCategory category) const;

    const std::vector<HistoryEntry>& history() const;
    void clearHistory();

private:
    std::string expression_;
    double lastResult_ = 0.0;
    bool hasResult_ = false;
    std::deque<std::string> tokens_;
    std::vector<HistoryEntry> history_;
};

}  // namespace me::calculator
