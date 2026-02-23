/*
 * Tests for DisasmEngine wrapper
 * tests/disasm_engine_test.cpp
 */

#include <QTest>

#include "../src/ui/disasm_engine.h"

using namespace Oneg4FM;

class DisasmEngineTest : public QObject {
    Q_OBJECT

   private slots:
    void disassemblesSimpleX86();
};

void DisasmEngineTest::disassemblesSimpleX86() {
    const std::uint8_t code[] = {0x55, 0x48, 0x89, 0xe5, 0xc3};  // push rbp; mov rbp,rsp; ret
    DisasmEngine engine;
    QVERIFY(engine.configure(CpuArch::X86_64, true));

    std::vector<DisasmInstr> out;
    std::string err;
    QVERIFY(engine.disassemble(code, sizeof(code), 0x1000, out, err));
    QCOMPARE(out.size(), static_cast<std::size_t>(3));
    QCOMPARE(out[0].address, static_cast<std::uint64_t>(0x1000));
    QCOMPARE(QString::fromStdString(out[0].mnemonic).toLower(), QStringLiteral("push"));
    QCOMPARE(QString::fromStdString(out[1].mnemonic).toLower(), QStringLiteral("mov"));
    QCOMPARE(out[2].address, static_cast<std::uint64_t>(0x1004));
}

QTEST_MAIN(DisasmEngineTest)
#include "disasm_engine_test.moc"
