#include "calculator.hpp"

#include <cmath>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace me::calculator {
namespace {

using TokenCallback = std::function<void(std::string)>;

bool isOperator(char value) {
    return value == '+' || value == '-' || value == '*' || value == '/' || value == '^';
}

int precedence(const std::string& operation) {
    if (operation == "^") return 3;
    if (operation == "*" || operation == "/") return 2;
    return 1;
}

double apply(double lhs, char operation, double rhs) {
    switch (operation) {
        case '+': return lhs + rhs;
        case '-': return lhs - rhs;
        case '*': return lhs * rhs;
        case '/':
            if (std::abs(rhs) < std::numeric_limits<double>::epsilon()) {
                throw std::domain_error("division by zero");
            }
            return lhs / rhs;
        case '^': return std::pow(lhs, rhs);
        default: throw std::invalid_argument("unknown operator");
    }
}

void tokenize(const std::string& input, TokenCallback emit) {
    static const std::regex numberPattern(R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)");
    std::size_t index = 0;

    while (index < input.size()) {
        const unsigned char next = static_cast<unsigned char>(input[index]);
        if (std::isspace(next)) {
            ++index;
            continue;
        }
        if (input.compare(index, 2, "pi") == 0 || input.compare(index, 1, "π") == 0) {
            emit("pi");
            index += input[index] == 'p' ? 2 : 1;
            continue;
        }

        bool matchedFunction = false;
        for (const std::string_view name : {"sin", "cos", "tan", "log", "sqrt"}) {
            if (input.compare(index, name.size(), name.data()) == 0) {
                emit(std::string(name));
                index += name.size();
                matchedFunction = true;
                break;
            }
        }
        if (matchedFunction) continue;

        std::smatch match;
        if (std::regex_search(input.begin() + static_cast<std::ptrdiff_t>(index), input.end(),
                              match, numberPattern)) {
            if (match.position(0) != 0) throw std::invalid_argument("malformed number");
            emit(match.str());
            index += match.length();
            continue;
        }
        if (isOperator(input[index]) || input[index] == '(' || input[index] == ')') {
            emit(std::string(1, input[index]));
            ++index;
            continue;
        }
        throw std::invalid_argument("unexpected character");
    }
}

std::deque<std::string> shuntingYard(const std::string& expression) {
    std::deque<std::string> output;
    std::vector<std::string> operators;
    std::size_t cursor = 0;

    tokenize(expression, [&](std::string token) {
        (void)cursor++;
        if (token == "(") { operators.push_back(token); return; }
        if (token == ")") {
            while (!operators.empty() && operators.back() != "(") {
                output.push_back(operators.back());
                operators.pop_back();
            }
            if (operators.empty()) throw std::invalid_argument("unbalanced parentheses");
            operators.pop_back();
            return;
        }
        if (token.size() == 1 && isOperator(token[0])) {
            while (!operators.empty() && operators.back() != "(" &&
                   precedence(operators.back()) >= precedence(token)) {
                output.push_back(operators.back());
                operators.pop_back();
            }
            operators.push_back(token);
            return;
        }
        output.push_back(token);
    });

    while (!operators.empty()) {
        if (operators.back() == "(") throw std::invalid_argument("unbalanced parentheses");
        output.push_back(operators.back());
        operators.pop_back();
    }
    return output;
}

double evaluateRpn(const std::deque<std::string>& rpn) {
    std::vector<double> values;
    for (const auto& token : rpn) {
        if (token == "+" || token == "-" || token == "*" || token == "/" || token == "^") {
            if (values.size() < 2) throw std::invalid_argument("missing operand");
            const double rhs = values.back(); values.pop_back();
            const double lhs = values.back(); values.pop_back();
            values.push_back(apply(lhs, token[0], rhs));
            continue;
        }
        if (token == "sin" || token == "cos" || token == "tan" || token == "log" ||
            token == "sqrt") {
            if (values.empty()) throw std::invalid_argument("missing function operand");
            const double value = values.back(); values.pop_back();
            if (token == "sin") values.push_back(std::sin(value));
            else if (token == "cos") values.push_back(std::cos(value));
            else if (token == "tan") values.push_back(std::tan(value));
            else if (token == "log") values.push_back(std::log10(value));
            else values.push_back(std::sqrt(value));
            continue;
        }
        if (token == "pi") { values.push_back(std::numbers::pi); continue; }
        try {
            values.push_back(std::stod(token));
        } catch (...) {
            throw std::invalid_argument("invalid numeric token");
        }
    }
    if (values.size() != 1) throw std::invalid_argument("incomplete expression");
    if (!std::isfinite(values.front())) throw std::overflow_error("result is not finite");
    return values.front();
}

const std::map<UnitCategory, std::map<std::string, double>>& lengthAndWeightTables() {
    static const std::map<UnitCategory, std::map<std::string, double>> tables{
        {UnitCategory::Length, {{"mm", 0.001}, {"cm", 0.01}, {"m", 1.0}, {"km", 1000.0},
                                {"in", 0.0254}, {"ft", 0.3048}, {"mi", 1609.344}}},
        {UnitCategory::Weight, {{"g", 0.001}, {"kg", 1.0}, {"t", 1000.0},
                                {"oz", 0.028349523125}, {"lb", 0.45359237}}}};
    return tables;
}

}  // namespace

double Calculator::evaluate(const std::string& input) {
    const std::string normalized = input.empty() ? expression_ : input;
    if (normalized.empty()) throw std::invalid_argument("empty expression");
    lastResult_ = evaluateRpn(shuntingYard(normalized));
    hasResult_ = true;
    expression_.clear();
    tokens_.clear();
    history_.push_back({normalized, std::to_string(lastResult_)});
    return lastResult_;
}

void Calculator::append(const std::string& token) {
    if (hasResult_ && !token.empty() && (std::isdigit(static_cast<unsigned char>(token[0])) ||
                                         token[0] == '.')) {
        expression_.clear();
    }
    expression_ += token;
}

void Calculator::backspace() {
    if (!expression_.empty()) expression_.pop_back();
}

void Calculator::clear() {
    expression_.clear();
    hasResult_ = false;
}

const std::string& Calculator::currentExpression() const { return expression_; }
bool Calculator::hasResult() const { return hasResult_; }
double Calculator::lastResult() const { return lastResult_; }

double Calculator::convert(UnitCategory category, const std::string& fromUnit,
                           const std::string& toUnit, double value) const {
    if (category == UnitCategory::Temperature) {
        double celsius = value;
        if (fromUnit == "F") celsius = (value - 32.0) * 5.0 / 9.0;
        else if (fromUnit == "K" || fromUnit == "k") celsius = value - 273.15;
        else if (fromUnit != "C" && fromUnit != "c") throw std::invalid_argument("unknown temperature unit");
        if (toUnit == "C" || toUnit == "c") return celsius;
        if (toUnit == "F") return celsius * 9.0 / 5.0 + 32.0;
        if (toUnit == "K" || toUnit == "k") return celsius + 273.15;
        throw std::invalid_argument("unknown temperature unit");
    }
    if (category == UnitCategory::CurrencyPlaceholder) {
        // Currency rates are intentionally placeholders until a bounded offline source ships.
        if (fromUnit == toUnit) return value;
        throw std::logic_error("currency conversion requires a configured placeholder rate");
    }
    const auto& table = lengthAndWeightTables().at(category);
    const auto source = table.find(fromUnit);
    const auto target = table.find(toUnit);
    if (source == table.end() || target == table.end())
        throw std::invalid_argument("unknown unit");
    return value * source->second / target->second;
}

std::vector<std::string> Calculator::supportedUnits(UnitCategory category) const {
    if (category == UnitCategory::Temperature) return {"C", "F", "K"};
    if (category == UnitCategory::CurrencyPlaceholder) return {};
    const auto& table = lengthAndWeightTables().at(category);
    std::vector<std::string> units;
    units.reserve(table.size());
    for (const auto& [unit, factor] : table) {
        (void)factor;
        units.push_back(unit);
    }
    return units;
}

const std::vector<HistoryEntry>& Calculator::history() const { return history_; }

void Calculator::clearHistory() { history_.clear(); }

}  // namespace me::calculator
