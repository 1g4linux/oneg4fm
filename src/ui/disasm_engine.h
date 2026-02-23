/*
 * RAII wrapper around Capstone for simple disassembly
 * src/ui/disasm_engine.h
 */

#ifndef PCMANFM_DISASM_ENGINE_H
#define PCMANFM_DISASM_ENGINE_H

#include <capstone/capstone.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Oneg4FM {

enum class CpuArch { X86_64, X86_32, ARM64, ARM, MIPS64, MIPS32, PPC64, PPC32, RISCV64, RISCV32, Unknown };

struct DisasmInstr {
    std::uint64_t address = 0;
    std::string mnemonic;
    std::string opStr;
    std::vector<std::uint8_t> bytes;
    // lightweight classification based on mnemonic/opcodes
    enum class Kind { Normal, Branch, Call, ReturnIns, Nop } kind = Kind::Normal;
};

class DisasmEngine {
   public:
    DisasmEngine() = default;
    ~DisasmEngine();

    DisasmEngine(const DisasmEngine&) = delete;
    DisasmEngine& operator=(const DisasmEngine&) = delete;

    bool configure(CpuArch arch, bool littleEndian);
    bool isValid() const { return handle_ != 0; }

    bool disassemble(const std::uint8_t* code,
                     std::size_t codeSize,
                     std::uint64_t baseAddress,
                     std::vector<DisasmInstr>& out,
                     std::string& errorOut) const;

   private:
    csh handle_ = 0;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_DISASM_ENGINE_H
