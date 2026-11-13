// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <asmjit/core/api-build_p.h>
#if !defined(ASMJIT_NO_PPC64) && !defined(ASMJIT_NO_LOGGING)

#include <asmjit/core/misc_p.h>
#include <asmjit/support/support.h>
#include <asmjit/ppc/ppc64formatter_p.h>
#include <asmjit/ppc/ppc64instapi_p.h>
#include <asmjit/ppc/ppc64operand.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

// ppc::FormatterInternal - Format Register
// ========================================

ASMJIT_FAVOR_SIZE Error FormatterInternal::format_register(
  String& sb,
  FormatFlags flags,
  const BaseEmitter* emitter,
  Arch arch,
  RegType reg_type,
  uint32_t reg_id) noexcept {

  Support::maybe_unused(flags, emitter, arch);

  switch (reg_type) {
    case RegType::kGp64:
      return sb.append_format("r%u", reg_id);

    case RegType::kVec64:
      return sb.append_format("f%u", reg_id);

    case RegType::kPPC_Cr:
      return sb.append_format("cr%u", reg_id);

    default:
      return sb.append_format("<Reg-%u>?%u", uint32_t(reg_type), reg_id);
  }
}

// ppc::FormatterInternal - Format Operand
// =======================================

ASMJIT_FAVOR_SIZE Error FormatterInternal::format_operand(
  String& sb,
  FormatFlags flags,
  const BaseEmitter* emitter,
  Arch arch,
  const Operand_& op) noexcept {

  if (op.is_reg()) {
    const Reg& reg = op.as<Reg>();
    return format_register(sb, flags, emitter, arch, reg.reg_type(), reg.id());
  }

  if (op.is_mem()) {
    const Mem& m = op.as<Mem>();
    int64_t off = int64_t(m.offset());

    // Label based address is formatted as `label`, `label+offset`, or `label-offset`.
    if (m.has_base_label()) {
      ASMJIT_PROPAGATE(Formatter::format_label(sb, flags, emitter, m.base_id()));
      if (off != 0) {
        ASMJIT_PROPAGATE(sb.append(off < 0 ? '-' : '+'));
        ASMJIT_PROPAGATE(sb.append_uint(off < 0 ? uint64_t(0) - uint64_t(off) : uint64_t(off), 10));
      }
      return Error::kOk;
    }

    // Indexed address is formatted as `base, index`, which is how assemblers write it.
    if (m.has_index()) {
      if (m.has_base_reg()) {
        ASMJIT_PROPAGATE(format_register(sb, flags, emitter, arch, m.base_type(), m.base_id()));
      }
      else {
        ASMJIT_PROPAGATE(sb.append('0'));
      }

      ASMJIT_PROPAGATE(sb.append(", "));
      return format_register(sb, flags, emitter, arch, m.index_type(), m.index_id());
    }

    // Address with a displacement is formatted as `displacement(base)`.
    if (Support::test(flags, FormatFlags::kHexOffsets) && uint64_t(off) > 9) {
      ASMJIT_PROPAGATE(sb.append("0x"));
      ASMJIT_PROPAGATE(sb.append_uint(uint64_t(off), 16));
    }
    else {
      ASMJIT_PROPAGATE(sb.append_int(off, 10));
    }

    ASMJIT_PROPAGATE(sb.append('('));
    if (m.has_base_reg()) {
      ASMJIT_PROPAGATE(format_register(sb, flags, emitter, arch, m.base_type(), m.base_id()));
    }
    else {
      ASMJIT_PROPAGATE(sb.append('0'));
    }
    return sb.append(')');
  }

  if (op.is_imm()) {
    int64_t val = op.as<Imm>().value();

    if (Support::test(flags, FormatFlags::kHexImms) && uint64_t(val) > 9) {
      ASMJIT_PROPAGATE(sb.append("0x"));
      return sb.append_uint(uint64_t(val), 16);
    }
    else {
      return sb.append_int(val, 10);
    }
  }

  if (op.is_label()) {
    return Formatter::format_label(sb, flags, emitter, op.id());
  }

  return sb.append("<None>");
}

// ppc::FormatterInternal - Format Instruction
// ===========================================

ASMJIT_FAVOR_SIZE Error FormatterInternal::format_instruction(
  String& sb,
  FormatFlags flags,
  const BaseEmitter* emitter,
  Arch arch,
  const BaseInst& inst, Span<const Operand_> operands) noexcept {

  // Format instruction mnemonic.
  InstId inst_id = inst.inst_id();

#ifndef ASMJIT_NO_TEXT
  if (inst_id != Inst::kIdNone && inst_id < Inst::_kIdCount) {
    ASMJIT_PROPAGATE(InstInternal::inst_id_to_string(inst_id, InstStringifyOptions::kNone, sb));
  }
  else {
    ASMJIT_PROPAGATE(sb.append_format("[InstId=#%u]", unsigned(inst_id)));
  }
#else
  ASMJIT_PROPAGATE(sb.append_format("[InstId=#%u]", unsigned(inst_id)));
#endif

  for (size_t i = 0u; i < operands.size(); i++) {
    const Operand_& op = operands[i];
    if (op.is_none()) {
      break;
    }

    ASMJIT_PROPAGATE(sb.append(i == 0 ? " " : ", "));
    ASMJIT_PROPAGATE(format_operand(sb, flags, emitter, arch, op));
  }

  return Error::kOk;
}

// ppc::FormatterInternal - Tests
// ==============================

#if defined(ASMJIT_TEST) && !defined(ASMJIT_NO_TEXT)
static void test_format_instruction(const char* expected, FormatFlags flags, InstId inst_id, const Operand_& o0 = Operand(), const Operand_& o1 = Operand(), const Operand_& o2 = Operand()) noexcept {
  Operand_ operands[] = { o0, o1, o2 };
  StringTmp<128> sb;

  // The formatter is called through the architecture independent API to verify the dispatch as well.
  EXPECT_EQ(Formatter::format_instruction(sb, flags, nullptr, Arch::kPPC64_LE, BaseInst(inst_id), Span<const Operand_>(operands, 3)), Error::kOk);
  EXPECT_EQ(sb, expected)
    .message("Formatted '%s', expected '%s'", sb.data(), expected);
}

UNIT(ppc64_formatter) {
  constexpr FormatFlags kNoFlags = FormatFlags::kNone;

  INFO("Checking instructions with register and immediate operands");
  test_format_instruction("add r3, r4, r5", kNoFlags, Inst::kIdAdd, r3, r4, r5);
  test_format_instruction("addi r3, r1, -32768", kNoFlags, Inst::kIdAddi, r3, r1, Imm(-32768));
  test_format_instruction("ori r31, r30, 65535", kNoFlags, Inst::kIdOri, r31, r30, Imm(65535));
  test_format_instruction("ori r31, r30, 0xFFFF", FormatFlags::kHexImms, Inst::kIdOri, r31, r30, Imm(65535));
  test_format_instruction("mfspr r0, 8", kNoFlags, Inst::kIdMfspr, r0, Imm(8));
  test_format_instruction("mtspr 9, r12", kNoFlags, Inst::kIdMtspr, Imm(9), r12);
  test_format_instruction("bclr 20, 0, 0", kNoFlags, Inst::kIdBclr, Imm(20), Imm(0), Imm(0));

  INFO("Checking instructions with condition register operands");
  test_format_instruction("mcrf cr0, cr7", kNoFlags, Inst::kIdMcrf, cr0, cr7);
  test_format_instruction("crand 2, 2, 6", kNoFlags, Inst::kIdCrand, cr0.eq(), cr0.eq(), cr1.eq());

  INFO("Checking memory operands");
  test_format_instruction("ld r3, 0(r4)", kNoFlags, Inst::kIdLd, r3, ptr(r4));
  test_format_instruction("ld r3, 8(r4)", kNoFlags, Inst::kIdLd, r3, ptr(r4, 8));
  test_format_instruction("std r0, -16(r1)", kNoFlags, Inst::kIdStd, r0, ptr(r1, -16));
  test_format_instruction("std r0, 0x7FFC(r1)", FormatFlags::kHexOffsets, Inst::kIdStd, r0, ptr(r1, 32764));
  test_format_instruction("ld r3, r4, r5", kNoFlags, Inst::kIdLd, r3, ptr(r4, r5));
  test_format_instruction("ld r3, L7", kNoFlags, Inst::kIdLd, r3, ptr(Label(7)));
  test_format_instruction("ld r3, L7+16", kNoFlags, Inst::kIdLd, r3, ptr(Label(7), 16));
  test_format_instruction("ld r3, L7-16", kNoFlags, Inst::kIdLd, r3, ptr(Label(7), -16));

  INFO("Checking other register types");
  StringTmp<32> sb;
  EXPECT_EQ(Formatter::format_register(sb, kNoFlags, nullptr, Arch::kPPC64_BE, RegType::kVec64, 31), Error::kOk);
  EXPECT_EQ(sb, "f31");

  sb.clear();
  EXPECT_EQ(Formatter::format_register(sb, kNoFlags, nullptr, Arch::kPPC64_BE, RegType::kPPC_Cr, 7), Error::kOk);
  EXPECT_EQ(sb, "cr7");

  INFO("Checking an instruction id that is not defined");
  test_format_instruction("[InstId=#0]", kNoFlags, Inst::kIdNone);
}
#endif

ASMJIT_END_SUB_NAMESPACE

#endif // !ASMJIT_NO_PPC64 && !ASMJIT_NO_LOGGING
