// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef ASMJIT_PPC_PPC64INSTDB_P_H_INCLUDED
#define ASMJIT_PPC_PPC64INSTDB_P_H_INCLUDED

#include <asmjit/ppc/ppc64instdb.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

//! \cond INTERNAL
//! \addtogroup asmjit_ppc64
//! \{

namespace InstDB {

//! Instruction encoding (PPC64).
//!
//! The encoding describes which operands the instruction accepts and where they are placed in the instruction
//! word. Operands are always passed in the order used by the assembly language.
enum EncodingId : uint32_t {
  //! Invalid instruction.
  kEncodingNone = 0,

  //! `inst RT, RA, RB` - three general purpose registers.
  kEncodingRtRaRb,
  //! `inst RT, RA` - two general purpose registers.
  kEncodingRtRa,
  //! `inst RT, RA, SI` - SI is a signed 16-bit immediate.
  kEncodingRtRaSi,
  //! `inst RT, RA, SI` - RA cannot be R0 (the instruction would read it as zero), SI is a signed 16-bit immediate.
  kEncodingRtRaSiNoR0,
  //! `inst RT, SI` - SI is a signed 16-bit immediate.
  kEncodingRtSi,
  //! `inst RT, D` - D is a signed 16-bit immediate that is split into three fields.
  kEncodingRtDx,
  //! `inst RA, RS, RB` - three general purpose registers, the destination is RA.
  kEncodingRaRsRb,
  //! `inst RA, RS` - two general purpose registers, the destination is RA.
  kEncodingRaRs,
  //! `inst RA, RS, UI` - UI is an unsigned 16-bit immediate.
  kEncodingRaRsUi,
  //! `inst RA, RS, SH` - SH is a 5-bit shift amount.
  kEncodingRaRsSh5,
  //! `inst RA, RS, SH` - SH is a 6-bit shift amount.
  kEncodingRaRsSh6,
  //! `inst RA, RS, SH, MB|ME` - SH is a 6-bit shift amount, MB|ME is a 6-bit mask begin or mask end.
  kEncodingRaRsSh6Mb6,
  //! `inst RA, RS, RB, MB|ME` - MB|ME is a 6-bit mask begin or mask end.
  kEncodingRaRsRbMb6,
  //! `inst RA, RS, SH, MB, ME` - SH is a 5-bit shift amount, MB and ME are a 5-bit mask begin and mask end.
  kEncodingRaRsSh5MbMe,
  //! `inst RA, RS, RB, MB, ME` - MB and ME are a 5-bit mask begin and mask end.
  kEncodingRaRsRbMbMe,
  //! `inst RT|RS, D(RA)` - load or store, D is a signed 16-bit displacement.
  kEncodingRtMemD,
  //! `inst RT, D(RA)` - load that updates RA, which must not be RT.
  kEncodingRtMemDLu,
  //! `inst RT|RS, DS(RA)` - load or store, DS is a signed 16-bit displacement that must be a multiple of 4.
  kEncodingRtMemDs,
  //! `inst RT, DS(RA)` - load that updates RA, which must not be RT.
  kEncodingRtMemDsLu,
  //! `inst RT|RS, RA, RB` - indexed load or store.
  kEncodingRtMemX,
  //! `inst RT, RA, RB` - indexed load that updates RA, which must not be RT.
  kEncodingRtMemXLu,
  //! `inst FRT|FRS, D(RA)` - floating point load or store, D is a signed 16-bit displacement.
  kEncodingFtMemD,
  //! `inst FRT|FRS, RA, RB` - indexed floating point load or store.
  kEncodingFtMemX,
  //! `inst RT, SPR` - SPR is a special purpose register number.
  kEncodingRtSpr,
  //! `inst SPR, RS` - SPR is a special purpose register number.
  kEncodingSprRs,
  //! `inst BO, BI, BH` - branch to LR or CTR, operands are immediates.
  kEncodingBoBiBh,
  //! `inst target` - branch to a label that is within +-32 MiB.
  kEncodingRel24,
  //! `inst BO, BI, target` - conditional branch to a label that is within +-32 KiB.
  kEncodingBoBiRel14,

  //! `inst BF, L, RA, RB` - compare, BF is a condition register field, L selects a 32-bit (0) or 64-bit (1) compare.
  kEncodingBfLRaRb,
  //! `inst BF, L, RA, SI` - compare with a signed 16-bit immediate.
  kEncodingBfLRaSi,
  //! `inst BF, L, RA, UI` - compare with an unsigned 16-bit immediate.
  kEncodingBfLRaUi,
  //! `inst BT, BA, BB` - operands are condition register bits (0-31).
  kEncodingBtBaBb,
  //! `inst BF, BFA` - two condition register fields.
  kEncodingBfBfa,
  //! `inst BF` - condition register field.
  kEncodingBf,
  //! `inst RT` - general purpose register.
  kEncodingRt,
  //! `inst RT, BFA` - general purpose register and a condition register field.
  kEncodingRtBfa,
  //! `inst RT, RA, RB, BC` - RA cannot be R0 (the instruction would read it as zero), BC is a condition register bit.
  kEncodingRtRaRbBc
};

#ifndef ASMJIT_NO_TEXT
//! Maximum length of an instruction name (without a null terminator).
static constexpr uint32_t kMaxInstNameSize = 11;

//! Instruction names indexed by instruction id.
extern const char _inst_name_table[][kMaxInstNameSize + 1u];
#endif // !ASMJIT_NO_TEXT

} // {InstDB}

//! \}
//! \endcond

ASMJIT_END_SUB_NAMESPACE

#endif // ASMJIT_PPC_PPC64INSTDB_P_H_INCLUDED
