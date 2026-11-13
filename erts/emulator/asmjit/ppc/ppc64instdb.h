// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef ASMJIT_PPC_PPC64INSTDB_H_INCLUDED
#define ASMJIT_PPC_PPC64INSTDB_H_INCLUDED

#include <asmjit/ppc/ppc64globals.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

//! \addtogroup asmjit_ppc64
//! \{

//! Instruction database (PPC64).
namespace InstDB {

//! Instruction information (PPC64).
struct InstInfo {
  //! Instruction word having all operand fields zeroed.
  uint32_t _opcode;
  //! Instruction encoding type.
  uint8_t _encoding;
  uint8_t _reserved[3];

  //! \name Accessors
  //! \{

  [[nodiscard]]
  ASMJIT_INLINE_NODEBUG uint32_t opcode() const noexcept { return _opcode; }

  //! \}
};

ASMJIT_VARAPI const InstInfo _inst_info_table[];

[[nodiscard]]
static ASMJIT_INLINE const InstInfo& inst_info_by_id(InstId inst_id) noexcept {
  ASMJIT_ASSERT(Inst::is_defined_id(inst_id));
  return _inst_info_table[inst_id];
}

} // {InstDB}

//! \}

ASMJIT_END_SUB_NAMESPACE

#endif // ASMJIT_PPC_PPC64INSTDB_H_INCLUDED
