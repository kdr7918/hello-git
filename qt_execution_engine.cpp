#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QRegularExpression>
#include <QDebug>
#include <stdexcept>

// ==========================================
// 1. 가상 데이터 타입 (Clipper2 Paths64 Mock)
// ==========================================
struct Paths64 {
    QString log; // 실제로는 std::vector<std::vector<Point64>> 데이터가 들어감
};

// ==========================================
// 2. 명령어 실행기 (Instruction Executor)
// ==========================================
class ExecutionEngine {
    QMap<QString, Paths64> memoryPool; // 임시 변수(T1, T2...)를 저장하는 RAM 역할

    // --- 실제 Clipper2 래퍼 함수들 (Mock) ---
    Paths64 loadLayerFromDB(const QString& layerName) {
        return { QString("Data(Layer %1)").arg(layerName) };
    }

    Paths64 executeBoolean(const QString& op, const Paths64& left, const Paths64& right) {
        return { QString("[%1 %2 %3]").arg(left.log, op, right.log) };
    }

    Paths64 executeResize(const QString& op, const Paths64& target, double delta) {
        return { QString("%1_BY_%2(%3)").arg(op).arg(delta).arg(target.log) };
    }

public:
    void run(const QStringList& instructions) {
        // 정규식 컴파일 (초기화 시 한 번만 빌드되므로 파싱 속도가 빠름)
        QRegularExpression rxLoad("^([T0-9]+) = LOAD_LAYER\\(([0-9\\.]+)\\)$");
        QRegularExpression rxBool("^([T0-9]+) = BOOLEAN_([A-Z]+)\\(([T0-9]+),\\s*([T0-9]+)\\)$");
        QRegularExpression rxResize("^([T0-9]+) = RESIZE_([A-Z]+)\\(([T0-9]+),\\s*delta=([0-9\\.]+)\\)$");

        for (const QString& inst : instructions) {
            // 1. 레이어 로드 파싱
            if (auto match = rxLoad.match(inst); match.hasMatch()) {
                QString outVar = match.captured(1);
                QString layerInfo = match.captured(2);
                
                memoryPool[outVar] = loadLayerFromDB(layerInfo);
                qDebug().noquote() << "▶ 실행: 레이어 로드 ->" << outVar << "=" << memoryPool[outVar].log;
            }
            // 2. Boolean 연산 파싱
            else if (auto match = rxBool.match(inst); match.hasMatch()) {
                QString outVar = match.captured(1);
                QString op = match.captured(2);
                QString leftVar = match.captured(3);
                QString rightVar = match.captured(4);

                Paths64 leftData = memoryPool[leftVar];
                Paths64 rightData = memoryPool[rightVar];

                memoryPool[outVar] = executeBoolean(op, leftData, rightData);
                qDebug().noquote() << "▶ 실행: 부울 연산 ->" << outVar << "=" << memoryPool[outVar].log;
            }
            // 3. Resize 연산 파싱
            else if (auto match = rxResize.match(inst); match.hasMatch()) {
                QString outVar = match.captured(1);
                QString op = match.captured(2);
                QString targetVar = match.captured(3);
                double delta = match.captured(4).toDouble();

                Paths64 targetData = memoryPool[targetVar];

                memoryPool[outVar] = executeResize(op, targetData, delta);
                qDebug().noquote() << "▶ 실행: 리사이즈 ->" << outVar << "=" << memoryPool[outVar].log;
            }
            else {
                qDebug().noquote() << "❌ 파싱 실패 (알 수 없는 명령어):" << inst;
            }
        }
    }

    // 최종 결과물 반환
    Paths64 getResult(const QString& finalVar) {
        return memoryPool.value(finalVar, { "NULL" });
    }
};

// ==========================================
// 3. 메인 테스트
// ==========================================
int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    // 이전 파서가 만들어준 결과물이라고 가정
    QStringList executionPlan = {
        "T1 = LOAD_LAYER(1.0)",
        "T2 = LOAD_LAYER(2.0)",
        "T3 = BOOLEAN_AND(T1, T2)",
        "T4 = LOAD_LAYER(3.0)",
        "T5 = LOAD_LAYER(4.0)",
        "T6 = BOOLEAN_UNION(T4, T5)",
        "T7 = RESIZE_INFLATE(T6, delta=10)",
        "T8 = BOOLEAN_DIFF(T3, T7)"
    };
    QString finalVariable = "T8";

    qDebug().noquote() << "--- 엔진 구동 시작 ---";
    ExecutionEngine engine;
    engine.run(executionPlan);

    qDebug().noquote() << "\n--- 최종 연산 결과 ---";
    Paths64 finalData = engine.getResult(finalVariable);
    qDebug().noquote() << finalData.log;

    return 0;
}
