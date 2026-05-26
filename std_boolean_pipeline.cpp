#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ==========================================
// 1. Lexer
// ==========================================
enum class TokenType {
    Number,
    Plus,
    Minus,
    Star,
    Caret,
    Su,
    Sd,
    LParen,
    RParen,
    EndOfFile,
    Error
};

struct Token {
    TokenType type;
    std::string value;
};

class Lexer {
    std::string text;
    std::size_t pos = 0;

    void skipWhitespace() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }

    std::string readNumber() {
        const std::size_t start = pos;
        while (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.')) {
            ++pos;
        }
        return text.substr(start, pos - start);
    }

    std::string readAlpha() {
        const std::size_t start = pos;
        while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) {
            text[pos] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[pos])));
            ++pos;
        }
        return text.substr(start, pos - start);
    }

public:
    explicit Lexer(const std::string& input) : text(input) {}

    Token getNextToken() {
        skipWhitespace();
        if (pos >= text.size()) {
            return {TokenType::EndOfFile, ""};
        }

        const char c = text[pos];
        if (std::isdigit(static_cast<unsigned char>(c))) return {TokenType::Number, readNumber()};
        if (c == '+') { ++pos; return {TokenType::Plus, "+"}; }
        if (c == '-') { ++pos; return {TokenType::Minus, "-"}; }
        if (c == '*') { ++pos; return {TokenType::Star, "*"}; }
        if (c == '^') { ++pos; return {TokenType::Caret, "^"}; }
        if (c == '(') { ++pos; return {TokenType::LParen, "("}; }
        if (c == ')') { ++pos; return {TokenType::RParen, ")"}; }

        if (std::isalpha(static_cast<unsigned char>(c))) {
            const std::string kw = readAlpha();
            if (kw == "or") return {TokenType::Plus, "OR"};
            if (kw == "sub") return {TokenType::Minus, "SUB"};
            if (kw == "and") return {TokenType::Star, "AND"};
            if (kw == "xor") return {TokenType::Caret, "XOR"};
            if (kw == "su") return {TokenType::Su, "SU"};
            if (kw == "sd") return {TokenType::Sd, "SD"};
            return {TokenType::Error, "Unknown keyword: '" + kw + "'"};
        }

        return {TokenType::Error, std::string("Unknown character: '") + c + "'"};
    }
};

// ==========================================
// 2. AST and plan builder
// ==========================================
class ExecutionContext {
    int tempVarCounter = 0;
    std::vector<std::string> instructions;

public:
    std::string getNewTempVar() {
        std::ostringstream out;
        out << "T" << ++tempVarCounter;
        return out.str();
    }

    void addInstruction(const std::string& inst) {
        instructions.push_back(inst);
    }

    const std::vector<std::string>& getInstructions() const {
        return instructions;
    }
};

class ASTNode {
public:
    virtual ~ASTNode() {}
    virtual std::string buildPlan(ExecutionContext& ctx) const = 0;
};

class LayerNode : public ASTNode {
    int layer = 0;
    int datatype = 0;

public:
    explicit LayerNode(const std::string& value) {
        const std::size_t dotPos = value.find('.');
        if (dotPos != std::string::npos) {
            layer = std::atoi(value.substr(0, dotPos).c_str());
            datatype = std::atoi(value.substr(dotPos + 1).c_str());
        } else {
            layer = std::atoi(value.c_str());
            datatype = 0;
        }
    }

    std::string buildPlan(ExecutionContext& ctx) const {
        const std::string outVar = ctx.getNewTempVar();
        std::ostringstream inst;
        inst << outVar << " = LOAD_LAYER(" << layer << "." << datatype << ")";
        ctx.addInstruction(inst.str());
        return outVar;
    }
};

class BooleanNode : public ASTNode {
    int opType;
    std::shared_ptr<ASTNode> left;
    std::shared_ptr<ASTNode> right;

public:
    BooleanNode(int op, std::shared_ptr<ASTNode> l, std::shared_ptr<ASTNode> r)
        : opType(op), left(l), right(r) {}

    std::string buildPlan(ExecutionContext& ctx) const {
        const std::string leftVar = left->buildPlan(ctx);
        const std::string rightVar = right->buildPlan(ctx);
        const std::string outVar = ctx.getNewTempVar();
        const std::string op =
            opType == 1 ? "UNION" :
            opType == 2 ? "DIFF" :
            opType == 3 ? "AND" : "XOR";

        std::ostringstream inst;
        inst << outVar << " = BOOLEAN_" << op << "(" << leftVar << ", " << rightVar << ")";
        ctx.addInstruction(inst.str());
        return outVar;
    }
};

class ResizeNode : public ASTNode {
    bool isUp;
    double delta;
    std::shared_ptr<ASTNode> child;

public:
    ResizeNode(bool up, double d, std::shared_ptr<ASTNode> node)
        : isUp(up), delta(d), child(node) {}

    std::string buildPlan(ExecutionContext& ctx) const {
        const std::string childVar = child->buildPlan(ctx);
        const std::string outVar = ctx.getNewTempVar();
        const std::string op = isUp ? "INFLATE" : "SHRINK";

        std::ostringstream inst;
        inst << outVar << " = RESIZE_" << op << "(" << childVar << ", delta=" << delta << ")";
        ctx.addInstruction(inst.str());
        return outVar;
    }
};

struct ParseResult {
    std::shared_ptr<ASTNode> rootNode;
    bool hasError = false;
    std::string errorMessage;
};

class Parser {
    Lexer lexer;
    Token currentToken;
    std::string errorMsg;

    void setError(const std::string& msg) {
        if (errorMsg.empty()) {
            errorMsg = msg;
        }
    }

    bool eat(TokenType type) {
        if (!errorMsg.empty()) return false;

        if (currentToken.type == type) {
            currentToken = lexer.getNextToken();
            if (currentToken.type == TokenType::Error) {
                setError(currentToken.value);
                return false;
            }
            return true;
        }

        setError("Syntax error: Unexpected token '" + currentToken.value + "'");
        return false;
    }

    std::shared_ptr<ASTNode> parseBase() {
        if (!errorMsg.empty()) return std::shared_ptr<ASTNode>();

        if (currentToken.type == TokenType::Number) {
            std::shared_ptr<ASTNode> node(new LayerNode(currentToken.value));
            if (!eat(TokenType::Number)) return std::shared_ptr<ASTNode>();
            return node;
        }

        if (currentToken.type == TokenType::LParen) {
            if (!eat(TokenType::LParen)) return std::shared_ptr<ASTNode>();
            std::shared_ptr<ASTNode> node = parseExpr();
            if (!eat(TokenType::RParen)) return std::shared_ptr<ASTNode>();
            return node;
        }

        setError("Syntax error: Expected layer number or '('");
        return std::shared_ptr<ASTNode>();
    }

    std::shared_ptr<ASTNode> parseFactor() {
        std::shared_ptr<ASTNode> node = parseBase();
        if (!node) return std::shared_ptr<ASTNode>();

        while (currentToken.type == TokenType::Su || currentToken.type == TokenType::Sd) {
            const bool isUp = currentToken.type == TokenType::Su;
            if (!eat(currentToken.type)) return std::shared_ptr<ASTNode>();

            if (currentToken.type != TokenType::Number) {
                setError("Syntax error: Expected number after su/sd");
                return std::shared_ptr<ASTNode>();
            }

            const double delta = std::atof(currentToken.value.c_str());
            if (!eat(TokenType::Number)) return std::shared_ptr<ASTNode>();

            node.reset(new ResizeNode(isUp, delta, node));
        }

        return node;
    }

    std::shared_ptr<ASTNode> parseTerm() {
        std::shared_ptr<ASTNode> node = parseFactor();
        if (!node) return std::shared_ptr<ASTNode>();

        while (currentToken.type == TokenType::Star) {
            if (!eat(TokenType::Star)) return std::shared_ptr<ASTNode>();

            std::shared_ptr<ASTNode> rightNode = parseFactor();
            if (!rightNode) return std::shared_ptr<ASTNode>();

            node.reset(new BooleanNode(3, node, rightNode));
        }

        return node;
    }

    std::shared_ptr<ASTNode> parseExpr() {
        std::shared_ptr<ASTNode> node = parseTerm();
        if (!node) return std::shared_ptr<ASTNode>();

        while (currentToken.type == TokenType::Plus ||
               currentToken.type == TokenType::Minus ||
               currentToken.type == TokenType::Caret) {
            const int op =
                currentToken.type == TokenType::Plus ? 1 :
                currentToken.type == TokenType::Minus ? 2 : 4;
            const TokenType opToken = currentToken.type;

            if (!eat(opToken)) return std::shared_ptr<ASTNode>();

            std::shared_ptr<ASTNode> rightNode = parseTerm();
            if (!rightNode) return std::shared_ptr<ASTNode>();

            node.reset(new BooleanNode(op, node, rightNode));
        }

        return node;
    }

public:
    explicit Parser(const std::string& text) : lexer(text) {
        currentToken = lexer.getNextToken();
        if (currentToken.type == TokenType::Error) {
            setError(currentToken.value);
        }
    }

    ParseResult parse() {
        std::shared_ptr<ASTNode> root = parseExpr();
        if (!errorMsg.empty() || !root) {
            ParseResult result;
            result.hasError = true;
            result.errorMessage = errorMsg;
            return result;
        }

        ParseResult result;
        result.rootNode = root;
        return result;
    }
};

// ==========================================
// 3. Execution engine
// ==========================================
struct Paths64 {
    std::string log;
};

class ExecutionEngine {
    std::map<std::string, Paths64> memoryPool;

    Paths64 loadLayerFromDB(const std::string& layerName) const {
        return {"Data(Layer " + layerName + ")"};
    }

    Paths64 executeBoolean(const std::string& op, const Paths64& left, const Paths64& right) const {
        return {"[" + left.log + " " + op + " " + right.log + "]"};
    }

    Paths64 executeResize(const std::string& op, const Paths64& target, double delta) const {
        std::ostringstream out;
        out << op << "_BY_" << delta << "(" << target.log << ")";
        return {out.str()};
    }

public:
    void run(const std::vector<std::string>& instructions) {
        const std::regex rxLoad("^([T0-9]+) = LOAD_LAYER\\(([0-9\\.]+)\\)$");
        const std::regex rxBool("^([T0-9]+) = BOOLEAN_([A-Z]+)\\(([T0-9]+),\\s*([T0-9]+)\\)$");
        const std::regex rxResize("^([T0-9]+) = RESIZE_([A-Z]+)\\(([T0-9]+),\\s*delta=([0-9\\.]+)\\)$");

        for (std::vector<std::string>::const_iterator it = instructions.begin(); it != instructions.end(); ++it) {
            const std::string& inst = *it;
            std::smatch match;

            if (std::regex_match(inst, match, rxLoad)) {
                const std::string outVar = match[1].str();
                const std::string layerInfo = match[2].str();
                memoryPool[outVar] = loadLayerFromDB(layerInfo);
                std::cout << "LOAD: " << outVar << " = " << memoryPool[outVar].log << "\n";
            } else if (std::regex_match(inst, match, rxBool)) {
                const std::string outVar = match[1].str();
                const std::string op = match[2].str();
                const std::string leftVar = match[3].str();
                const std::string rightVar = match[4].str();

                memoryPool[outVar] = executeBoolean(op, memoryPool[leftVar], memoryPool[rightVar]);
                std::cout << "BOOLEAN: " << outVar << " = " << memoryPool[outVar].log << "\n";
            } else if (std::regex_match(inst, match, rxResize)) {
                const std::string outVar = match[1].str();
                const std::string op = match[2].str();
                const std::string targetVar = match[3].str();
                const double delta = std::atof(match[4].str().c_str());

                memoryPool[outVar] = executeResize(op, memoryPool[targetVar], delta);
                std::cout << "RESIZE: " << outVar << " = " << memoryPool[outVar].log << "\n";
            } else {
                std::cerr << "Unknown instruction: " << inst << "\n";
            }
        }
    }

    Paths64 getResult(const std::string& finalVar) const {
        std::map<std::string, Paths64>::const_iterator it = memoryPool.find(finalVar);
        if (it == memoryPool.end()) {
            return {"NULL"};
        }
        return it->second;
    }
};

// ==========================================
// 4. Main
// ==========================================
int main() {
    const std::string expr = "(1.0 and 2.0) sub ((3.0 or 4.0) su 10)";

    Parser parser(expr);
    ParseResult parsed = parser.parse();
    if (parsed.hasError) {
        std::cerr << "Parse error: " << parsed.errorMessage << "\n";
        return 1;
    }

    ExecutionContext ctx;
    const std::string finalVar = parsed.rootNode->buildPlan(ctx);
    const std::vector<std::string>& plan = ctx.getInstructions();

    std::cout << "Input expression: " << expr << "\n\n";
    std::cout << "--- Execution plan ---\n";
    for (std::size_t i = 0; i < plan.size(); ++i) {
        std::cout << "[" << (i + 1) << "] " << plan[i] << "\n";
    }

    std::cout << "\n--- Engine run ---\n";
    ExecutionEngine engine;
    engine.run(plan);

    std::cout << "\n--- Final result ---\n";
    std::cout << engine.getResult(finalVar).log << "\n";

    return 0;
}
