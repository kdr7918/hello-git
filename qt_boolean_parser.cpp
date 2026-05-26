#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QSharedPointer>
#include <QDebug>

// 1. 토큰 및 렉서
enum class TokenType { Number, Plus, Minus, Star, Caret, Su, Sd, LParen, RParen, EndOfFile, Error };

struct Token {
    TokenType type;
    QString value;
};

class Lexer {
    QString text;
    int pos = 0;
    void skipWhitespace() { while(pos < text.length() && text.at(pos).isSpace()) pos++; }
    
    QString readNumber() {
        int start = pos;
        while(pos < text.length() && (text.at(pos).isDigit() || text.at(pos) == '.')) pos++;
        return text.mid(start, pos - start);
    }
    
    QString readAlpha() {
        int start = pos;
        while(pos < text.length() && text.at(pos).isLetter()) pos++;
        return text.mid(start, pos - start).toLower();
    }

public:
    explicit Lexer(const QString& input) : text(input) {}

    Token getNextToken() {
        skipWhitespace();
        if(pos >= text.length()) return {TokenType::EndOfFile, QString()};

        QChar c = text.at(pos);
        if(c.isDigit()) return {TokenType::Number, readNumber()};
        if(c == '+') { pos++; return {TokenType::Plus, "+"}; }
        if(c == '-') { pos++; return {TokenType::Minus, "-"}; }
        if(c == '*') { pos++; return {TokenType::Star, "*"}; }
        if(c == '^') { pos++; return {TokenType::Caret, "^"}; }
        if(c == '(') { pos++; return {TokenType::LParen, "("}; }
        if(c == ')') { pos++; return {TokenType::RParen, ")"}; }

        if(c.isLetter()) {
            QString kw = readAlpha();
            if(kw == "or") return {TokenType::Plus, "OR"};
            if(kw == "sub") return {TokenType::Minus, "SUB"};
            if(kw == "and") return {TokenType::Star, "AND"};
            if(kw == "xor") return {TokenType::Caret, "XOR"};
            if(kw == "su") return {TokenType::Su, "SU"};
            if(kw == "sd") return {TokenType::Sd, "SD"};
            return {TokenType::Error, QString("Unknown keyword: '%1'").arg(kw)};
        }

        return {TokenType::Error, QString("Unknown character: '%1'").arg(c)};
    }
};

// 2. 구문 트리 및 실행 컨텍스트
class ExecutionContext {
    int tempVarCounter = 0;
    QStringList instructions;
public:
    QString getNewTempVar() { return QString("T%1").arg(++tempVarCounter); }
    void addInstruction(const QString& inst) { instructions.append(inst); }
    QStringList getInstructions() const { return instructions; }
};

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual QString buildPlan(ExecutionContext& ctx) const = 0; 
};

class LayerNode : public ASTNode {
    int layer, datatype;
public:
    explicit LayerNode(const QString& val) {
        int dotPos = val.indexOf('.');
        if (dotPos != -1) { layer = val.mid(0, dotPos).toInt(); datatype = val.mid(dotPos + 1).toInt(); } 
        else { layer = val.toInt(); datatype = 0; }
    }
    QString buildPlan(ExecutionContext& ctx) const override {
        QString outVar = ctx.getNewTempVar();
        ctx.addInstruction(QString("%1 = LOAD_LAYER(%2.%3)").arg(outVar).arg(layer).arg(datatype));
        return outVar;
    }
};

class BooleanNode : public ASTNode {
    int opType; // 1:UNION(+), 2:DIFF(-), 3:AND(*), 4:XOR(^)
    QSharedPointer<ASTNode> left, right;
public:
    BooleanNode(int op, QSharedPointer<ASTNode> l, QSharedPointer<ASTNode> r)
        : opType(op), left(l), right(r) {}

    QString buildPlan(ExecutionContext& ctx) const override {
        QString l_var = left->buildPlan(ctx);
        QString r_var = right->buildPlan(ctx);
        
        QString outVar = ctx.getNewTempVar();
        QString opStr = (opType == 1) ? "UNION" : (opType == 2) ? "DIFF" : (opType == 3) ? "AND" : "XOR";
        
        ctx.addInstruction(QString("%1 = BOOLEAN_%2(%3, %4)").arg(outVar, opStr, l_var, r_var));
        return outVar;
    }
};

class ResizeNode : public ASTNode {
    bool isUp;
    double delta;
    QSharedPointer<ASTNode> child;
public:
    ResizeNode(bool isUp, double delta, QSharedPointer<ASTNode> child)
        : isUp(isUp), delta(delta), child(child) {}

    QString buildPlan(ExecutionContext& ctx) const override {
        QString child_var = child->buildPlan(ctx);
        QString outVar = ctx.getNewTempVar();
        QString opStr = isUp ? "INFLATE" : "SHRINK";
        
        ctx.addInstruction(QString("%1 = RESIZE_%2(%3, delta=%4)").arg(outVar, opStr, child_var).arg(delta));
        return outVar;
    }
};

// 3. 파서 (타입 불일치 버그 완벽 수정)
struct ParseResult {
    QSharedPointer<ASTNode> rootNode;
    bool hasError = false;
    QString errorMessage;
};

class Parser {
    Lexer lexer;
    Token currentToken;
    QString errorMsg;

    void setError(const QString& msg) { if (errorMsg.isEmpty()) errorMsg = msg; }

    bool eat(TokenType type) {
        if (!errorMsg.isEmpty()) return false;
        if (currentToken.type == type) {
            currentToken = lexer.getNextToken();
            if (currentToken.type == TokenType::Error) {
                setError(currentToken.value);
                return false;
            }
            return true;
        } 
        setError(QString("Syntax error: Unexpected token '%1'").arg(currentToken.value));
        return false;
    }

    QSharedPointer<ASTNode> parseBase() {
        if (!errorMsg.isEmpty()) return nullptr;

        if (currentToken.type == TokenType::Number) {
            auto node = QSharedPointer<LayerNode>::create(currentToken.value);
            if (!eat(TokenType::Number)) return nullptr;
            return node;
        } 
        else if (currentToken.type == TokenType::LParen) {
            if (!eat(TokenType::LParen)) return nullptr;
            auto node = parseExpr();
            if (!eat(TokenType::RParen)) return nullptr;
            return node;
        }
        
        setError("Syntax error: Expected layer number or '('");
        return nullptr;
    }

    QSharedPointer<ASTNode> parseFactor() {
        if (!errorMsg.isEmpty()) return nullptr;
        auto node = parseBase();
        if (!node) return nullptr; 

        while (currentToken.type == TokenType::Su || currentToken.type == TokenType::Sd) {
            bool isUp = (currentToken.type == TokenType::Su);
            if (!eat(currentToken.type)) return nullptr;
            
            if (currentToken.type != TokenType::Number) {
                setError("Syntax error: Expected number after su/sd");
                return nullptr;
            }
            double delta = currentToken.value.toDouble();
            if (!eat(TokenType::Number)) return nullptr;
            
            node = QSharedPointer<ResizeNode>::create(isUp, delta, node);
        }
        return node;
    }

    QSharedPointer<ASTNode> parseTerm() {
        if (!errorMsg.isEmpty()) return nullptr;
        auto node = parseFactor();
        if (!node) return nullptr;

        while (currentToken.type == TokenType::Star) {
            int op = 3; // AND
            if (!eat(TokenType::Star)) return nullptr;
            
            auto rightNode = parseFactor();
            if (!rightNode) return nullptr;

            node = QSharedPointer<BooleanNode>::create(op, node, rightNode);
        }
        return node;
    }

    QSharedPointer<ASTNode> parseExpr() {
        if (!errorMsg.isEmpty()) return nullptr;
        auto node = parseTerm();
        if (!node) return nullptr;

        while (currentToken.type == TokenType::Plus || 
               currentToken.type == TokenType::Minus || 
               currentToken.type == TokenType::Caret) {
            
            // 🚨 핵심 수정: TokenType을 int로 안전하게 캐스팅
            int op = (currentToken.type == TokenType::Plus) ? 1 : 
                     (currentToken.type == TokenType::Minus) ? 2 : 4;
            
            TokenType opToken = currentToken.type;
            if (!eat(opToken)) return nullptr;
            
            auto rightNode = parseTerm();
            if (!rightNode) return nullptr;

            node = QSharedPointer<BooleanNode>::create(op, node, rightNode);
        }
        return node;
    }

public:
    explicit Parser(const QString& text) : lexer(text) {
        currentToken = lexer.getNextToken();
        if (currentToken.type == TokenType::Error) setError(currentToken.value);
    }

    ParseResult parse() {
        auto root = parseExpr();
        if (!errorMsg.isEmpty() || !root) return {nullptr, true, errorMsg}; 
        return {root, false, QString()}; 
    }
};

// 4. 실행
int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    // 🚨 문제의 수식 테스트
    QString expr_str = "1.0 + 4.0"; 

    Parser parser(expr_str);
    ParseResult result = parser.parse(); 

    // 🚨 여기서 방어 로직을 쳐주지 않으면 에러 시 프로그램이 죽습니다 (SegFault 방어)
    if (result.hasError) {
        qDebug().noquote() << "❌ Parse Error:" << result.errorMessage;
        return -1; 
    }

    ExecutionContext ctx;
    QString finalVar = result.rootNode->buildPlan(ctx); 
    QStringList opList = ctx.getInstructions(); 

    qDebug().noquote() << "입력 수식:" << expr_str << "\n";
    for (int i = 0; i < opList.size(); ++i) {
        qDebug().noquote() << QString("[%1] %2").arg(i + 1).arg(opList[i]);
    }
    qDebug().noquote() << "\n최종 결과 저장 변수:" << finalVar;

    return 0;
}
