// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef ASMJIT_PPC_PPC64OPERAND_H_INCLUDED
#define ASMJIT_PPC_PPC64OPERAND_H_INCLUDED

#include <asmjit/core/operand.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

//! \addtogroup asmjit_ppc64
//! \{

//! General purpose register (PPC64).
class Gp : public UniGp {
public:
  ASMJIT_DEFINE_ABSTRACT_REG(Gp, UniGp)

  //! \name Constants
  //! \{

  //! Special register id.
  enum Id : uint32_t {
    //! Stack pointer register id.
    kIdSp = 1,
    //! TOC pointer register id.
    kIdToc = 2,
    //! Thread pointer register id.
    kIdThread = 13,
    //! Frame pointer register id (PPC64 has no dedicated frame pointer, R31 is used by convention).
    kIdFp = 31
  };

  //! \}

  //! \name Static Constructors
  //! \{

  //! Creates a new 64-bit general purpose register having the given register id `reg_id`.
  [[nodiscard]]
  static ASMJIT_INLINE_CONSTEXPR Gp make_r64(uint32_t reg_id) noexcept { return Gp(signature_of_t<RegType::kGp64>(), reg_id); }

  //! \}

  //! \name Gp Register Accessors
  //! \{

  //! Tests whether this register is R0.
  //!
  //! \note R0 is special - instructions that use it as a base register of a memory operand (and few others such
  //! as `addi`) read it as zero.
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR bool is_r0() const noexcept { return id() == 0u; }

  //! Tests whether this register is the stack pointer (R1).
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR bool is_sp() const noexcept { return id() == kIdSp; }

  //! Clones and casts this register to a 64-bit register.
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Gp r64() const noexcept { return make_r64(id()); }

  //! \}
};

//! Floating point and vector register (PPC64).
//!
//! \note Only the 64-bit floating point registers (F0-F31) are provided at the moment.
class Vec : public UniVec {
public:
  ASMJIT_DEFINE_ABSTRACT_REG(Vec, UniVec)

  //! \name Static Constructors
  //! \{

  //! Creates a new 64-bit floating point register (F) having the given register id `reg_id`.
  [[nodiscard]]
  static ASMJIT_INLINE_CONSTEXPR Vec make_v64(uint32_t reg_id) noexcept { return Vec(signature_of_t<RegType::kVec64>(), reg_id); }

  //! Creates a new 64-bit floating point register (F) having the given register id `reg_id`.
  [[nodiscard]]
  static ASMJIT_INLINE_CONSTEXPR Vec make_f(uint32_t reg_id) noexcept { return make_v64(reg_id); }

  //! \}

  //! \name Vec Register Accessors
  //! \{

  //! Clones and casts this register to a 64-bit floating point register (F).
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Vec v64() const noexcept { return make_v64(id()); }

  //! Clones and casts this register to a 64-bit floating point register (F).
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Vec f() const noexcept { return make_v64(id()); }

  //! \}
};

//! Condition register field (PPC64).
//!
//! The condition register consists of eight 4-bit fields (CR0-CR7). Compare instructions write their result to
//! one field, conditional branches and few other instructions test a single bit of a field.
class Cr : public Reg {
public:
  ASMJIT_DEFINE_FINAL_REG(Cr, Reg, RegTraits<RegType::kPPC_Cr>)

  //! \name Condition Register Bits
  //!
  //! Each function returns the number of a bit within the whole condition register (0-31) as an immediate value,
  //! which is how instructions that operate on condition register bits take it.
  //!
  //! \{

  //! Returns the `LT` (less than, negative) bit of this field.
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Imm lt() const noexcept { return Imm(id() * 4u + 0u); }

  //! Returns the `GT` (greater than, positive) bit of this field.
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Imm gt() const noexcept { return Imm(id() * 4u + 1u); }

  //! Returns the `EQ` (equal, zero) bit of this field.
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Imm eq() const noexcept { return Imm(id() * 4u + 2u); }

  //! Returns the `SO` (summary overflow) bit of this field.
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Imm so() const noexcept { return Imm(id() * 4u + 3u); }

  //! Returns the `UN` (unordered) bit of this field, which is what a floating point compare stores in `SO`.
  [[nodiscard]]
  ASMJIT_INLINE_CONSTEXPR Imm un() const noexcept { return so(); }

  //! \}
};

//! Memory operand (PPC64).
//!
//! PPC64 provides two addressing forms, `base + displacement` and `base + index`, and a memory operand never has
//! both a displacement and an index. Forms that update the base register are separate instructions (for example
//! `stdu`), so the memory operand doesn't describe it.
class Mem : public BaseMem {
public:
  //! \name Construction & Destruction
  //! \{

  //! Construct a default `Mem` operand, that points to [0].
  ASMJIT_INLINE_CONSTEXPR Mem() noexcept
    : BaseMem() {}

  ASMJIT_INLINE_CONSTEXPR Mem(const Mem& other) noexcept
    : BaseMem(other) {}

  ASMJIT_INLINE_NODEBUG explicit Mem(Globals::NoInit_) noexcept
    : BaseMem(Globals::NoInit) {}

  ASMJIT_INLINE_CONSTEXPR Mem(const Signature& signature, uint32_t base_id, uint32_t index_id, int32_t offset) noexcept
    : BaseMem(signature, base_id, index_id, offset) {}

  ASMJIT_INLINE_CONSTEXPR explicit Mem(const Label& base, int32_t off = 0, Signature signature = Signature{0}) noexcept
    : BaseMem(Signature::from_op_type(OperandType::kMem) |
              Signature::from_mem_base_type(RegType::kLabelTag) |
              signature, base.id(), 0, off) {}

  ASMJIT_INLINE_CONSTEXPR explicit Mem(const Reg& base, int32_t off = 0, Signature signature = Signature{0}) noexcept
    : BaseMem(Signature::from_op_type(OperandType::kMem) |
              Signature::from_mem_base_type(base.reg_type()) |
              signature, base.id(), 0, off) {}

  ASMJIT_INLINE_CONSTEXPR Mem(const Reg& base, const Reg& index, Signature signature = Signature{0}) noexcept
    : BaseMem(Signature::from_op_type(OperandType::kMem) |
              Signature::from_mem_base_type(base.reg_type()) |
              Signature::from_mem_index_type(index.reg_type()) |
              signature, base.id(), index.id(), 0) {}

  //! \}

  //! \name Overloaded Operators
  //! \{

  ASMJIT_INLINE_CONSTEXPR Mem& operator=(const Mem& other) noexcept {
    copy_from(other);
    return *this;
  }

  //! \}

  //! \name Clone
  //! \{

  //! Clones the memory operand.
  ASMJIT_INLINE_CONSTEXPR Mem clone() const noexcept { return Mem(*this); }

  //! Gets new memory operand adjusted by `off`.
  ASMJIT_INLINE_CONSTEXPR Mem clone_adjusted(int64_t off) const noexcept {
    Mem result(*this);
    result.add_offset(off);
    return result;
  }

  //! \}

  //! \name Base & Index
  //! \{

  //! Converts memory `base_type` and `base_id` to `ppc::Gp` instance.
  //!
  //! The memory must have a valid base register otherwise the result will be wrong.
  ASMJIT_INLINE_NODEBUG Gp base_reg() const noexcept { return Gp::from_type_and_id(base_type(), base_id()); }

  //! Converts memory `index_type` and `index_id` to `ppc::Gp` instance.
  //!
  //! The memory must have a valid index register otherwise the result will be wrong.
  ASMJIT_INLINE_NODEBUG Gp index_reg() const noexcept { return Gp::from_type_and_id(index_type(), index_id()); }

  //! \}
};

#ifndef _DOXYGEN
namespace regs {
#endif

//! Creates a 64-bit general purpose register operand.
[[nodiscard]]
static ASMJIT_INLINE_CONSTEXPR Gp r(uint32_t id) noexcept { return Gp::make_r64(id); }

//! Creates a 64-bit general purpose register operand.
[[nodiscard]]
static ASMJIT_INLINE_CONSTEXPR Gp gp64(uint32_t id) noexcept { return Gp::make_r64(id); }

//! Creates a 64-bit floating point register operand.
[[nodiscard]]
static ASMJIT_INLINE_CONSTEXPR Vec f(uint32_t id) noexcept { return Vec::make_v64(id); }

//! Creates a condition register field operand.
[[nodiscard]]
static ASMJIT_INLINE_CONSTEXPR Cr cr(uint32_t id) noexcept { return Cr(id); }

static constexpr Gp r0 = Gp::make_r64(0);
static constexpr Gp r1 = Gp::make_r64(1);
static constexpr Gp r2 = Gp::make_r64(2);
static constexpr Gp r3 = Gp::make_r64(3);
static constexpr Gp r4 = Gp::make_r64(4);
static constexpr Gp r5 = Gp::make_r64(5);
static constexpr Gp r6 = Gp::make_r64(6);
static constexpr Gp r7 = Gp::make_r64(7);
static constexpr Gp r8 = Gp::make_r64(8);
static constexpr Gp r9 = Gp::make_r64(9);
static constexpr Gp r10 = Gp::make_r64(10);
static constexpr Gp r11 = Gp::make_r64(11);
static constexpr Gp r12 = Gp::make_r64(12);
static constexpr Gp r13 = Gp::make_r64(13);
static constexpr Gp r14 = Gp::make_r64(14);
static constexpr Gp r15 = Gp::make_r64(15);
static constexpr Gp r16 = Gp::make_r64(16);
static constexpr Gp r17 = Gp::make_r64(17);
static constexpr Gp r18 = Gp::make_r64(18);
static constexpr Gp r19 = Gp::make_r64(19);
static constexpr Gp r20 = Gp::make_r64(20);
static constexpr Gp r21 = Gp::make_r64(21);
static constexpr Gp r22 = Gp::make_r64(22);
static constexpr Gp r23 = Gp::make_r64(23);
static constexpr Gp r24 = Gp::make_r64(24);
static constexpr Gp r25 = Gp::make_r64(25);
static constexpr Gp r26 = Gp::make_r64(26);
static constexpr Gp r27 = Gp::make_r64(27);
static constexpr Gp r28 = Gp::make_r64(28);
static constexpr Gp r29 = Gp::make_r64(29);
static constexpr Gp r30 = Gp::make_r64(30);
static constexpr Gp r31 = Gp::make_r64(31);

static constexpr Vec f0 = Vec::make_v64(0);
static constexpr Vec f1 = Vec::make_v64(1);
static constexpr Vec f2 = Vec::make_v64(2);
static constexpr Vec f3 = Vec::make_v64(3);
static constexpr Vec f4 = Vec::make_v64(4);
static constexpr Vec f5 = Vec::make_v64(5);
static constexpr Vec f6 = Vec::make_v64(6);
static constexpr Vec f7 = Vec::make_v64(7);
static constexpr Vec f8 = Vec::make_v64(8);
static constexpr Vec f9 = Vec::make_v64(9);
static constexpr Vec f10 = Vec::make_v64(10);
static constexpr Vec f11 = Vec::make_v64(11);
static constexpr Vec f12 = Vec::make_v64(12);
static constexpr Vec f13 = Vec::make_v64(13);
static constexpr Vec f14 = Vec::make_v64(14);
static constexpr Vec f15 = Vec::make_v64(15);
static constexpr Vec f16 = Vec::make_v64(16);
static constexpr Vec f17 = Vec::make_v64(17);
static constexpr Vec f18 = Vec::make_v64(18);
static constexpr Vec f19 = Vec::make_v64(19);
static constexpr Vec f20 = Vec::make_v64(20);
static constexpr Vec f21 = Vec::make_v64(21);
static constexpr Vec f22 = Vec::make_v64(22);
static constexpr Vec f23 = Vec::make_v64(23);
static constexpr Vec f24 = Vec::make_v64(24);
static constexpr Vec f25 = Vec::make_v64(25);
static constexpr Vec f26 = Vec::make_v64(26);
static constexpr Vec f27 = Vec::make_v64(27);
static constexpr Vec f28 = Vec::make_v64(28);
static constexpr Vec f29 = Vec::make_v64(29);
static constexpr Vec f30 = Vec::make_v64(30);
static constexpr Vec f31 = Vec::make_v64(31);

static constexpr Cr cr0 = Cr(0);
static constexpr Cr cr1 = Cr(1);
static constexpr Cr cr2 = Cr(2);
static constexpr Cr cr3 = Cr(3);
static constexpr Cr cr4 = Cr(4);
static constexpr Cr cr5 = Cr(5);
static constexpr Cr cr6 = Cr(6);
static constexpr Cr cr7 = Cr(7);

#ifndef _DOXYGEN
} // {regs}

// Make `ppc::regs` accessible through `ppc` namespace as well.
using namespace regs;
#endif

//! \name Memory Operand Construction
//! \{

//! Creates `offset(base)` memory operand (PPC64).
[[nodiscard]]
static ASMJIT_INLINE_CONSTEXPR Mem ptr(const Gp& base, int32_t offset = 0) noexcept {
  return Mem(base, offset);
}

//! Creates `base, index` memory operand (PPC64).
[[nodiscard]]
static ASMJIT_INLINE_CONSTEXPR Mem ptr(const Gp& base, const Gp& index) noexcept {
  return Mem(base, index);
}

//! Creates `label + offset` memory operand (PPC64).
[[nodiscard]]
static ASMJIT_INLINE_CONSTEXPR Mem ptr(const Label& base, int32_t offset = 0) noexcept {
  return Mem(base, offset);
}

//! \}

//! \}

ASMJIT_END_SUB_NAMESPACE

#endif // ASMJIT_PPC_PPC64OPERAND_H_INCLUDED
