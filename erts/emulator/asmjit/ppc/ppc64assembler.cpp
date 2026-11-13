// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <asmjit/core/api-build_p.h>
#if !defined(ASMJIT_NO_PPC64)

#include <asmjit/core/codewriter_p.h>
#include <asmjit/core/emitterutils_p.h>
#include <asmjit/core/formatter.h>
#include <asmjit/core/logger.h>
#include <asmjit/core/misc_p.h>
#include <asmjit/support/support.h>
#include <asmjit/ppc/ppc64assembler.h>
#include <asmjit/ppc/ppc64formatter_p.h>
#include <asmjit/ppc/ppc64instdb_p.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

// ppc::Assembler - Operand Checks
// ===============================

// Tests whether the operand, which must be a register, is a 64-bit general purpose register.
static ASMJIT_INLINE_NODEBUG bool is_gp64(const Operand_& op) noexcept {
  return op.as<Reg>().is_gp64();
}

// Tests whether the operand, which must be a register, is a condition register field.
static ASMJIT_INLINE_NODEBUG bool is_cr(const Operand_& op) noexcept {
  return op.as<Reg>().is_reg(RegType::kPPC_Cr);
}

// Tests whether the given register ids are ids of physical registers.
static ASMJIT_INLINE_NODEBUG bool is_phys_id(uint32_t id0, uint32_t id1 = 0, uint32_t id2 = 0) noexcept {
  return (id0 | id1 | id2) <= 31u;
}

// ppc::Assembler - Construction & Destruction
// ===========================================

Assembler::Assembler(CodeHolder* code) noexcept : BaseAssembler() {
  _arch_mask = (uint64_t(1) << uint32_t(Arch::kPPC64_LE)) |
               (uint64_t(1) << uint32_t(Arch::kPPC64_BE)) ;

#ifndef ASMJIT_NO_LOGGING
  _funcs.format_instruction = FormatterInternal::format_instruction;
#endif

  if (code) {
    code->attach(this);
  }
}

Assembler::~Assembler() noexcept {}

// ppc::Assembler - Emit
// =====================

#define ENC_OPS1(OP0) \
  (uint32_t(OperandType::k##OP0))

#define ENC_OPS2(OP0, OP1) \
  (uint32_t(OperandType::k##OP0) + \
  (uint32_t(OperandType::k##OP1) << 3))

#define ENC_OPS3(OP0, OP1, OP2) \
  (uint32_t(OperandType::k##OP0) + \
  (uint32_t(OperandType::k##OP1) << 3) + \
  (uint32_t(OperandType::k##OP2) << 6))

#define ENC_OPS4(OP0, OP1, OP2, OP3) \
  (uint32_t(OperandType::k##OP0) + \
  (uint32_t(OperandType::k##OP1) << 3) + \
  (uint32_t(OperandType::k##OP2) << 6) + \
  (uint32_t(OperandType::k##OP3) << 9))

Error Assembler::_emit(InstId inst_id, const Operand_& o0, const Operand_& o1, const Operand_& o2, const Operand_* op_ext) {
  // Logging/Validation/Error.
  constexpr InstOptions kRequiresSpecialHandling = InstOptions::kReserved;

  Error err;
  CodeWriter writer(this);

  // Combine all instruction options and also check whether the instruction
  // is valid. All options that require special handling (including invalid
  // instruction) are handled by the next branch.
  InstOptions options = InstOptions(inst_id - 1 >= Inst::_kIdCount - 1) | InstOptions((size_t)(_buffer_end - writer.cursor()) < 4) | inst_options() | forced_inst_options();

  if (inst_id >= Inst::_kIdCount) {
    inst_id = 0;
  }

  const InstDB::InstInfo* inst_info = &InstDB::_inst_info_table[inst_id];
  uint32_t opcode = inst_info->opcode();
  uint32_t isign4;

  // These are only used when instruction uses a relative displacement.
  const Operand_* rel_op = nullptr;  // Operand that describes the target.
  OffsetFormat offset_format;        // Offset format.

  const Operand_& o3 = op_ext[EmitterUtils::kOp3];

  if (ASMJIT_UNLIKELY(Support::test(options, kRequiresSpecialHandling))) {
    if (ASMJIT_UNLIKELY(!_code)) {
      return report_error(make_error(Error::kNotInitialized));
    }

    // Unknown instruction.
    if (ASMJIT_UNLIKELY(inst_id == 0)) {
      goto InvalidInstruction;
    }

    // Grow request, happens rarely.
    err = writer.ensure_space(this, 4);
    if (ASMJIT_UNLIKELY(err != Error::kOk)) {
      goto Failed;
    }
  }

  // Signature of the first 4 operands.
  isign4 = (uint32_t(o0.op_type())     ) +
           (uint32_t(o1.op_type()) << 3) +
           (uint32_t(o2.op_type()) << 6) +
           (uint32_t(o3.op_type()) << 9);

  switch (inst_info->_encoding) {
    case InstDB::kEncodingRtRaRb: {
      if (isign4 == ENC_OPS3(Reg, Reg, Reg)) {
        if (!is_gp64(o0) || !is_gp64(o1) || !is_gp64(o2)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id(), o2.id())) {
          goto InvalidPhysId;
        }

        opcode |= (o0.id() << 21) | (o1.id() << 16) | (o2.id() << 11);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRtRa: {
      if (isign4 == ENC_OPS2(Reg, Reg)) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        opcode |= (o0.id() << 21) | (o1.id() << 16);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRtRaSi:
    case InstDB::kEncodingRtRaSiNoR0: {
      if (isign4 == ENC_OPS3(Reg, Reg, Imm)) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        // R0 used as RA would be read as zero and not as a register.
        if (inst_info->_encoding == InstDB::kEncodingRtRaSiNoR0 && o1.id() == 0u) {
          goto InvalidPhysId;
        }

        int64_t imm = o2.as<Imm>().value();
        if (!Support::is_int_n<16>(imm)) {
          goto InvalidImmediate;
        }

        opcode |= (o0.id() << 21) | (o1.id() << 16) | (uint32_t(imm) & 0xFFFFu);
        goto EmitOp;
      }

      // `addi RT, RA, label` - the immediate is the distance from this instruction to the label.
      if (isign4 == ENC_OPS3(Reg, Reg, Label) && inst_id == Inst::kIdAddi) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id()) || o1.id() == 0u) {
          goto InvalidPhysId;
        }

        opcode |= (o0.id() << 21) | (o1.id() << 16);

        rel_op = &o2;
        offset_format.reset_to_imm_value(OffsetType::kSignedOffset, 4, 0, 16, 0);
        goto EmitOp_Rel;
      }

      break;
    }

    case InstDB::kEncodingRtDx: {
      if (isign4 == ENC_OPS2(Reg, Imm)) {
        if (!is_gp64(o0)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id())) {
          goto InvalidPhysId;
        }

        int64_t d = o1.as<Imm>().value();
        if (!Support::is_int_n<16>(d)) {
          goto InvalidImmediate;
        }

        // The immediate is stored as d0 (10 most significant bits), d1 (5 bits), and d2 (the least significant bit).
        uint32_t d0 = (uint32_t(d) >> 6) & 0x3FFu;
        uint32_t d1 = (uint32_t(d) >> 1) & 0x1Fu;
        uint32_t d2 = uint32_t(d) & 0x1u;

        opcode |= (o0.id() << 21) | (d1 << 16) | (d0 << 6) | d2;
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRtSi: {
      if (isign4 == ENC_OPS2(Reg, Imm)) {
        if (!is_gp64(o0)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id())) {
          goto InvalidPhysId;
        }

        int64_t imm = o1.as<Imm>().value();
        if (!Support::is_int_n<16>(imm)) {
          goto InvalidImmediate;
        }

        opcode |= (o0.id() << 21) | (uint32_t(imm) & 0xFFFFu);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRaRsRb: {
      if (isign4 == ENC_OPS3(Reg, Reg, Reg)) {
        if (!is_gp64(o0) || !is_gp64(o1) || !is_gp64(o2)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id(), o2.id())) {
          goto InvalidPhysId;
        }

        opcode |= (o1.id() << 21) | (o0.id() << 16) | (o2.id() << 11);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRaRs: {
      if (isign4 == ENC_OPS2(Reg, Reg)) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        opcode |= (o1.id() << 21) | (o0.id() << 16);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRaRsUi: {
      if (isign4 == ENC_OPS3(Reg, Reg, Imm)) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        int64_t imm = o2.as<Imm>().value();
        if (!Support::is_uint_n<16>(imm)) {
          goto InvalidImmediate;
        }

        opcode |= (o1.id() << 21) | (o0.id() << 16) | uint32_t(imm);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRaRsSh5:
    case InstDB::kEncodingRaRsSh6: {
      if (isign4 == ENC_OPS3(Reg, Reg, Imm)) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        int64_t sh = o2.as<Imm>().value();
        opcode |= (o1.id() << 21) | (o0.id() << 16);

        if (inst_info->_encoding == InstDB::kEncodingRaRsSh5) {
          if (!Support::is_uint_n<5>(sh)) {
            goto InvalidImmediate;
          }

          opcode |= uint32_t(sh) << 11;
        }
        else {
          if (!Support::is_uint_n<6>(sh)) {
            goto InvalidImmediate;
          }

          // The most significant bit of a 6-bit shift amount is stored separately.
          opcode |= ((uint32_t(sh) & 0x1Fu) << 11) | ((uint32_t(sh) >> 5) << 1);
        }

        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRaRsSh6Mb6:
    case InstDB::kEncodingRaRsRbMb6: {
      bool has_rb = inst_info->_encoding == InstDB::kEncodingRaRsRbMb6;

      if (isign4 == (has_rb ? ENC_OPS4(Reg, Reg, Reg, Imm) : ENC_OPS4(Reg, Reg, Imm, Imm))) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        int64_t mb = o3.as<Imm>().value();
        if (!Support::is_uint_n<6>(mb)) {
          goto InvalidImmediate;
        }

        // The most significant bit of a 6-bit mask begin (or mask end) is stored last.
        opcode |= (o1.id() << 21) | (o0.id() << 16) | ((uint32_t(mb) & 0x1Fu) << 6) | ((uint32_t(mb) >> 5) << 5);

        if (has_rb) {
          if (!is_gp64(o2)) {
            goto InvalidInstruction;
          }

          if (!is_phys_id(o2.id())) {
            goto InvalidPhysId;
          }

          opcode |= o2.id() << 11;
        }
        else {
          int64_t sh = o2.as<Imm>().value();
          if (!Support::is_uint_n<6>(sh)) {
            goto InvalidImmediate;
          }

          opcode |= ((uint32_t(sh) & 0x1Fu) << 11) | ((uint32_t(sh) >> 5) << 1);
        }

        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRaRsSh5MbMe:
    case InstDB::kEncodingRaRsRbMbMe: {
      bool has_rb = inst_info->_encoding == InstDB::kEncodingRaRsRbMbMe;
      const Operand_& o4 = op_ext[EmitterUtils::kOp4];

      if (isign4 == (has_rb ? ENC_OPS4(Reg, Reg, Reg, Imm) : ENC_OPS4(Reg, Reg, Imm, Imm)) && o4.is_imm()) {
        if (!is_gp64(o0) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        int64_t mb = o3.as<Imm>().value();
        int64_t me = o4.as<Imm>().value();

        if (!Support::is_uint_n<5>(mb) || !Support::is_uint_n<5>(me)) {
          goto InvalidImmediate;
        }

        opcode |= (o1.id() << 21) | (o0.id() << 16) | (uint32_t(mb) << 6) | (uint32_t(me) << 1);

        if (has_rb) {
          if (!is_gp64(o2)) {
            goto InvalidInstruction;
          }

          if (!is_phys_id(o2.id())) {
            goto InvalidPhysId;
          }

          opcode |= o2.id() << 11;
        }
        else {
          int64_t sh = o2.as<Imm>().value();
          if (!Support::is_uint_n<5>(sh)) {
            goto InvalidImmediate;
          }

          opcode |= uint32_t(sh) << 11;
        }

        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRtMemD:
    case InstDB::kEncodingRtMemDLu:
    case InstDB::kEncodingRtMemDs:
    case InstDB::kEncodingRtMemDsLu:
    case InstDB::kEncodingRtMemX:
    case InstDB::kEncodingRtMemXLu:
    case InstDB::kEncodingFtMemD:
    case InstDB::kEncodingFtMemX: {
      if (isign4 == ENC_OPS2(Reg, Mem)) {
        uint32_t encoding = inst_info->_encoding;
        const Mem& m = o1.as<Mem>();

        bool is_fp = encoding == InstDB::kEncodingFtMemD || encoding == InstDB::kEncodingFtMemX;
        bool is_indexed = encoding == InstDB::kEncodingRtMemX || encoding == InstDB::kEncodingRtMemXLu || encoding == InstDB::kEncodingFtMemX;
        bool is_ds = encoding == InstDB::kEncodingRtMemDs || encoding == InstDB::kEncodingRtMemDsLu;
        bool is_load_update = encoding == InstDB::kEncodingRtMemDLu || encoding == InstDB::kEncodingRtMemDsLu || encoding == InstDB::kEncodingRtMemXLu;

        if (is_fp ? !o0.as<Reg>().is_vec64() : !is_gp64(o0)) {
          goto InvalidInstruction;
        }

        if (!m.has_base_reg() || m.base_type() != RegType::kGp64) {
          goto InvalidAddress;
        }

        if (!is_phys_id(o0.id(), m.base_id())) {
          goto InvalidPhysId;
        }

        // R0 used as a base would be read as zero and not as a register.
        if (m.base_id() == 0u) {
          goto InvalidAddress;
        }

        // A load that updates its base register cannot load into that register.
        if (is_load_update && o0.id() == m.base_id()) {
          goto OverlappedRegs;
        }

        opcode |= (o0.id() << 21) | (m.base_id() << 16);

        if (is_indexed) {
          if (!m.has_index() || m.index_type() != RegType::kGp64 || m.has_offset()) {
            goto InvalidAddress;
          }

          if (!is_phys_id(m.index_id())) {
            goto InvalidPhysId;
          }

          opcode |= m.index_id() << 11;
        }
        else {
          if (m.has_index()) {
            goto InvalidAddress;
          }

          int64_t disp = m.offset();
          if (!Support::is_int_n<16>(disp) || (is_ds && (disp & 0x3) != 0)) {
            goto InvalidDisplacement;
          }

          opcode |= uint32_t(disp) & (is_ds ? 0xFFFCu : 0xFFFFu);
        }

        goto EmitOp;
      }

      // `inst RT, RA, label` - the displacement is the distance from this instruction to the label.
      if (isign4 == ENC_OPS3(Reg, Reg, Label)) {
        uint32_t encoding = inst_info->_encoding;

        bool is_fp = encoding == InstDB::kEncodingFtMemD;
        bool is_ds = encoding == InstDB::kEncodingRtMemDs;

        if (!is_fp && !is_ds && encoding != InstDB::kEncodingRtMemD) {
          goto InvalidInstruction;
        }

        if ((is_fp ? !o0.as<Reg>().is_vec64() : !is_gp64(o0)) || !is_gp64(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id(), o1.id())) {
          goto InvalidPhysId;
        }

        // R0 used as a base would be read as zero and not as a register.
        if (o1.id() == 0u) {
          goto InvalidAddress;
        }

        opcode |= (o0.id() << 21) | (o1.id() << 16);
        rel_op = &o2;

        if (is_ds) {
          offset_format.reset_to_imm_value(OffsetType::kSignedOffset, 4, 2, 14, 2);
        }
        else {
          offset_format.reset_to_imm_value(OffsetType::kSignedOffset, 4, 0, 16, 0);
        }
        goto EmitOp_Rel;
      }

      break;
    }

    case InstDB::kEncodingRtSpr:
    case InstDB::kEncodingSprRs: {
      bool spr_first = inst_info->_encoding == InstDB::kEncodingSprRs;

      const Operand_& reg = spr_first ? o1 : o0;
      const Operand_& spr = spr_first ? o0 : o1;

      if (isign4 == (spr_first ? ENC_OPS2(Imm, Reg) : ENC_OPS2(Reg, Imm))) {
        if (!is_gp64(reg)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(reg.id())) {
          goto InvalidPhysId;
        }

        int64_t spr_id = spr.as<Imm>().value();
        if (!Support::is_uint_n<10>(spr_id)) {
          goto InvalidImmediate;
        }

        // The two 5-bit halves of the SPR number are swapped in the instruction word.
        uint32_t spr_field = ((uint32_t(spr_id) & 0x1Fu) << 5) | (uint32_t(spr_id) >> 5);

        opcode |= (reg.id() << 21) | (spr_field << 11);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingBoBiBh: {
      if (isign4 == ENC_OPS3(Imm, Imm, Imm)) {
        int64_t bo = o0.as<Imm>().value();
        int64_t bi = o1.as<Imm>().value();
        int64_t bh = o2.as<Imm>().value();

        if (!Support::is_uint_n<5>(bo) || !Support::is_uint_n<5>(bi) || !Support::is_uint_n<2>(bh)) {
          goto InvalidImmediate;
        }

        opcode |= (uint32_t(bo) << 21) | (uint32_t(bi) << 16) | (uint32_t(bh) << 11);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRel24: {
      if (isign4 == ENC_OPS1(Label)) {
        rel_op = &o0;
        offset_format.reset_to_imm_value(OffsetType::kSignedOffset, 4, 2, 24, 2);
        goto EmitOp_Rel;
      }

      break;
    }

    case InstDB::kEncodingBoBiRel14: {
      if (isign4 == ENC_OPS3(Imm, Imm, Label)) {
        int64_t bo = o0.as<Imm>().value();
        int64_t bi = o1.as<Imm>().value();

        if (!Support::is_uint_n<5>(bo) || !Support::is_uint_n<5>(bi)) {
          goto InvalidImmediate;
        }

        opcode |= (uint32_t(bo) << 21) | (uint32_t(bi) << 16);

        rel_op = &o2;
        offset_format.reset_to_imm_value(OffsetType::kSignedOffset, 4, 2, 14, 2);
        goto EmitOp_Rel;
      }

      break;
    }

    case InstDB::kEncodingBfLRaRb: {
      if (isign4 == ENC_OPS4(Reg, Imm, Reg, Reg)) {
        if (!is_cr(o0) || !is_gp64(o2) || !is_gp64(o3)) {
          goto InvalidInstruction;
        }

        if (o0.id() > 7u || !is_phys_id(o2.id(), o3.id())) {
          goto InvalidPhysId;
        }

        int64_t l = o1.as<Imm>().value();
        if (!Support::is_uint_n<1>(l)) {
          goto InvalidImmediate;
        }

        opcode |= (o0.id() << 23) | (uint32_t(l) << 21) | (o2.id() << 16) | (o3.id() << 11);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingBfLRaSi:
    case InstDB::kEncodingBfLRaUi: {
      if (isign4 == ENC_OPS4(Reg, Imm, Reg, Imm)) {
        if (!is_cr(o0) || !is_gp64(o2)) {
          goto InvalidInstruction;
        }

        if (o0.id() > 7u || !is_phys_id(o2.id())) {
          goto InvalidPhysId;
        }

        int64_t l = o1.as<Imm>().value();
        int64_t imm = o3.as<Imm>().value();

        if (!Support::is_uint_n<1>(l)) {
          goto InvalidImmediate;
        }

        if (inst_info->_encoding == InstDB::kEncodingBfLRaSi ? !Support::is_int_n<16>(imm) : !Support::is_uint_n<16>(imm)) {
          goto InvalidImmediate;
        }

        opcode |= (o0.id() << 23) | (uint32_t(l) << 21) | (o2.id() << 16) | (uint32_t(imm) & 0xFFFFu);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingBtBaBb: {
      if (isign4 == ENC_OPS3(Imm, Imm, Imm)) {
        int64_t bt = o0.as<Imm>().value();
        int64_t ba = o1.as<Imm>().value();
        int64_t bb = o2.as<Imm>().value();

        if (!Support::is_uint_n<5>(bt) || !Support::is_uint_n<5>(ba) || !Support::is_uint_n<5>(bb)) {
          goto InvalidImmediate;
        }

        opcode |= (uint32_t(bt) << 21) | (uint32_t(ba) << 16) | (uint32_t(bb) << 11);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingBfBfa: {
      if (isign4 == ENC_OPS2(Reg, Reg)) {
        if (!is_cr(o0) || !is_cr(o1)) {
          goto InvalidInstruction;
        }

        if (o0.id() > 7u || o1.id() > 7u) {
          goto InvalidPhysId;
        }

        opcode |= (o0.id() << 23) | (o1.id() << 18);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingBf: {
      if (isign4 == ENC_OPS1(Reg)) {
        if (!is_cr(o0)) {
          goto InvalidInstruction;
        }

        if (o0.id() > 7u) {
          goto InvalidPhysId;
        }

        opcode |= o0.id() << 23;
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRt: {
      if (isign4 == ENC_OPS1(Reg)) {
        if (!is_gp64(o0)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id())) {
          goto InvalidPhysId;
        }

        opcode |= o0.id() << 21;
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRtBfa: {
      if (isign4 == ENC_OPS2(Reg, Reg)) {
        if (!is_gp64(o0) || !is_cr(o1)) {
          goto InvalidInstruction;
        }

        if (!is_phys_id(o0.id()) || o1.id() > 7u) {
          goto InvalidPhysId;
        }

        opcode |= (o0.id() << 21) | (o1.id() << 18);
        goto EmitOp;
      }

      break;
    }

    case InstDB::kEncodingRtRaRbBc: {
      if (isign4 == ENC_OPS4(Reg, Reg, Reg, Imm)) {
        if (!is_gp64(o0) || !is_gp64(o1) || !is_gp64(o2)) {
          goto InvalidInstruction;
        }

        // R0 used as RA would be read as zero and not as a register.
        if (!is_phys_id(o0.id(), o1.id(), o2.id()) || o1.id() == 0u) {
          goto InvalidPhysId;
        }

        int64_t bc = o3.as<Imm>().value();
        if (!Support::is_uint_n<5>(bc)) {
          goto InvalidImmediate;
        }

        opcode |= (o0.id() << 21) | (o1.id() << 16) | (o2.id() << 11) | (uint32_t(bc) << 6);
        goto EmitOp;
      }

      break;
    }

    default:
      break;
  }

  goto InvalidInstruction;

  // --------------------------------------------------------------------------
  // [EmitOp - PC Relative]
  // --------------------------------------------------------------------------

EmitOp_Rel:
  {
    uint32_t label_id = rel_op->as<Label>().id();

    if (ASMJIT_UNLIKELY(!_code->is_label_valid(label_id))) {
      goto InvalidLabel;
    }

    LabelEntry& le = _code->label_entry_of(label_id);
    offset_format.set_big_endian(_environment.is_big_endian());

    if (le.is_bound_to(_section)) {
      // Label bound to the current section - the displacement is relative to the address of this instruction.
      int64_t disp = int64_t(le.offset() - uint64_t(offset()));

      if ((disp & int64_t(Support::lsb_mask<uint32_t>(offset_format.imm_discard_lsb()))) != 0) {
        goto InvalidDisplacement;
      }

      disp >>= offset_format.imm_discard_lsb();
      if (!EmitterUtils::is_encodable_offset_64(disp, offset_format.imm_bit_count())) {
        goto InvalidDisplacement;
      }

      opcode |= (uint32_t(disp) & Support::lsb_mask<uint32_t>(offset_format.imm_bit_count())) << offset_format.imm_bit_shift();
    }
    else {
      // Create a fixup referencing a non-bound label.
      size_t code_offset = writer.offset_from(_buffer_data);
      Fixup* fixup = _code->new_fixup(le, _section->section_id(), code_offset, 0, offset_format);

      if (ASMJIT_UNLIKELY(!fixup)) {
        goto OutOfMemory;
      }
    }
  }

  // --------------------------------------------------------------------------
  // [EmitOp - Opcode]
  // --------------------------------------------------------------------------

EmitOp:
  if (_environment.is_big_endian()) {
    writer.emit32u_be(opcode);
  }
  else {
    writer.emit32u_le(opcode);
  }

  // --------------------------------------------------------------------------
  // [Done]
  // --------------------------------------------------------------------------

  if (Support::test(options, InstOptions::kReserved)) {
#ifndef ASMJIT_NO_LOGGING
    if (_logger) {
      EmitterUtils::log_instruction_emitted(this, inst_id, options, o0, o1, o2, op_ext, 0, 0, writer.cursor());
    }
#endif
  }

  reset_state();

  writer.done(this);
  return Error::kOk;

  // --------------------------------------------------------------------------
  // [Error Handler]
  // --------------------------------------------------------------------------

#define ERROR_HANDLER(ERR) ERR: err = make_error(Error::k##ERR); goto Failed;
  ERROR_HANDLER(OutOfMemory)
  ERROR_HANDLER(InvalidAddress)
  ERROR_HANDLER(InvalidDisplacement)
  ERROR_HANDLER(InvalidImmediate)
  ERROR_HANDLER(InvalidInstruction)
  ERROR_HANDLER(InvalidLabel)
  ERROR_HANDLER(InvalidPhysId)
  ERROR_HANDLER(OverlappedRegs)
#undef ERROR_HANDLER

Failed:
#ifndef ASMJIT_NO_LOGGING
  return EmitterUtils::log_instruction_failed(this, err, inst_id, options, o0, o1, o2, op_ext);
#else
  reset_state();
  return report_error(err);
#endif
}

#undef ENC_OPS1
#undef ENC_OPS2
#undef ENC_OPS3
#undef ENC_OPS4

// ppc::Assembler - Align
// ======================

Error Assembler::align(AlignMode align_mode, uint32_t alignment) {
  constexpr uint32_t kNopPPC = 0x60000000u; // ori r0, r0, 0

  if (ASMJIT_UNLIKELY(!_code)) {
    return report_error(make_error(Error::kNotInitialized));
  }

  if (ASMJIT_UNLIKELY(uint32_t(align_mode) > uint32_t(AlignMode::kMaxValue))) {
    return report_error(make_error(Error::kInvalidArgument));
  }

  if (alignment <= 1) {
    return Error::kOk;
  }

  if (ASMJIT_UNLIKELY(!Support::is_power_of_2_up_to(alignment, Globals::kMaxAlignment))) {
    return report_error(make_error(Error::kInvalidArgument));
  }

  uint32_t i = uint32_t(Support::align_up_diff<size_t>(offset(), alignment));
  if (i == 0) {
    return Error::kOk;
  }

  CodeWriter writer(this);
  ASMJIT_PROPAGATE(writer.ensure_space(this, i));

  switch (align_mode) {
    case AlignMode::kCode: {
      if (ASMJIT_UNLIKELY(offset() & 0x3u)) {
        return make_error(Error::kInvalidState);
      }

      while (i >= 4) {
        if (_environment.is_big_endian()) {
          writer.emit32u_be(kNopPPC);
        }
        else {
          writer.emit32u_le(kNopPPC);
        }
        i -= 4;
      }

      ASMJIT_ASSERT(i == 0);
      break;
    }

    case AlignMode::kData:
    case AlignMode::kZero:
      writer.emit_zeros(i);
      break;
  }

  writer.done(this);

#ifndef ASMJIT_NO_LOGGING
  if (_logger) {
    StringTmp<128> sb;
    sb.append_chars(' ', _logger->indentation(FormatIndentationGroup::kCode));
    sb.append_format("align %u\n", alignment);
    _logger->log(sb);
  }
#endif

  return Error::kOk;
}

// ppc::Assembler - Events
// =======================

Error Assembler::on_attach(CodeHolder& code) noexcept {
  ASMJIT_PROPAGATE(Base::on_attach(code));

  _instruction_alignment = uint8_t(4);
  return Error::kOk;
}

Error Assembler::on_detach(CodeHolder& code) noexcept {
  return Base::on_detach(code);
}

ASMJIT_END_SUB_NAMESPACE

#endif // !ASMJIT_NO_PPC64
