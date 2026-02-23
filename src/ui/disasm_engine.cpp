/*
 * RAII wrapper around Capstone for simple disassembly
 * src/ui/disasm_engine.cpp
 */

#include "disasm_engine.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Oneg4FM {

namespace {
DisasmInstr::Kind classifyMnemonic(const cs_insn& insn) {
    std::string m(insn.mnemonic);
    if (m.empty()) {
        return DisasmInstr::Kind::Normal;
    }
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto startsWith = [&m](const char* prefix) {
        return m.size() >= std::strlen(prefix) && m.compare(0, std::strlen(prefix), prefix) == 0;
    };

    if (startsWith("ret")) {
        return DisasmInstr::Kind::ReturnIns;
    }
    if (startsWith("call") || m == "bl" || m == "blr" || m == "jal" || m == "jalr") {
        return DisasmInstr::Kind::Call;
    }
    if (startsWith("jmp") || (startsWith("j") && m != "jmp") || startsWith("b") || startsWith("br")) {
        return DisasmInstr::Kind::Branch;
    }
    if (startsWith("nop")) {
        return DisasmInstr::Kind::Nop;
    }
    return DisasmInstr::Kind::Normal;
}
}  // namespace

DisasmEngine::~DisasmEngine() {
    if (handle_) {
        cs_close(&handle_);
        handle_ = 0;
    }
}

bool DisasmEngine::configure(CpuArch arch, bool littleEndian) {
    if (handle_) {
        cs_close(&handle_);
        handle_ = 0;
    }

    cs_arch csArch = CS_ARCH_X86;
    cs_mode csMode = CS_MODE_LITTLE_ENDIAN;

    switch (arch) {
        case CpuArch::X86_64:
            csArch = CS_ARCH_X86;
            csMode = CS_MODE_64;
            break;
        case CpuArch::X86_32:
            csArch = CS_ARCH_X86;
            csMode = CS_MODE_32;
            break;
        case CpuArch::ARM64:
            csArch = CS_ARCH_ARM64;
            csMode = CS_MODE_LITTLE_ENDIAN;
            break;
        case CpuArch::ARM:
            csArch = CS_ARCH_ARM;
            csMode = CS_MODE_ARM;
            break;
        case CpuArch::MIPS64:
            csArch = CS_ARCH_MIPS;
            csMode = CS_MODE_MIPS64;
            break;
        case CpuArch::MIPS32:
            csArch = CS_ARCH_MIPS;
            csMode = CS_MODE_MIPS32;
            break;
        case CpuArch::PPC64:
            csArch = CS_ARCH_PPC;
            csMode = CS_MODE_64;
            break;
        case CpuArch::PPC32:
            csArch = CS_ARCH_PPC;
            csMode = CS_MODE_32;
            break;
        case CpuArch::RISCV64:
            csArch = CS_ARCH_RISCV;
            csMode = CS_MODE_RISCV64;
            break;
        case CpuArch::RISCV32:
            csArch = CS_ARCH_RISCV;
            csMode = CS_MODE_RISCV32;
            break;
        case CpuArch::Unknown:
            csArch = CS_ARCH_X86;
            csMode = CS_MODE_64;
            break;
    }

    if (!littleEndian) {
        csMode = static_cast<cs_mode>(csMode | CS_MODE_BIG_ENDIAN);
    }

    cs_err err = cs_open(csArch, csMode, &handle_);
    if (err != CS_ERR_OK) {
        handle_ = 0;
        return false;
    }

    cs_option(handle_, CS_OPT_DETAIL, CS_OPT_OFF);
    return true;
}

bool DisasmEngine::disassemble(const std::uint8_t* code,
                               std::size_t codeSize,
                               std::uint64_t baseAddress,
                               std::vector<DisasmInstr>& out,
                               std::string& errorOut) const {
    if (!handle_ || !code || codeSize == 0) {
        errorOut = "Invalid input to disassemble";
        return false;
    }

    cs_insn* insn = nullptr;
    const size_t count = cs_disasm(handle_, code, codeSize, baseAddress, 0, &insn);
    if (count == 0) {
        errorOut = cs_strerror(cs_errno(handle_));
        return false;
    }

    out.clear();
    out.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        DisasmInstr di;
        di.address = insn[i].address;
        di.mnemonic = insn[i].mnemonic;
        di.opStr = insn[i].op_str;
        di.bytes.assign(insn[i].bytes, insn[i].bytes + insn[i].size);
        di.kind = classifyMnemonic(insn[i]);
        out.push_back(std::move(di));
    }

    cs_free(insn, count);
    return true;
}

}  // namespace Oneg4FM
