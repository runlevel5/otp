// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef ASMJIT_PPC_PPC64ARCHTRAITS_P_H_INCLUDED
#define ASMJIT_PPC_PPC64ARCHTRAITS_P_H_INCLUDED

#include <asmjit/core/archtraits.h>
#include <asmjit/core/misc_p.h>
#include <asmjit/core/type.h>
#include <asmjit/ppc/ppc64operand.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

//! \cond INTERNAL
//! \addtogroup asmjit_ppc64
//! \{

static const constexpr ArchTraits ppc64_arch_traits = {
  // SP/FP/LR/PC (link register is a special purpose register, which doesn't have a general purpose register id).
  Gp::kIdSp, Gp::kIdFp, 0xFFu, 0xFFu,

  // Reserved.
  { 0u, 0u, 0u },

  // HW stack alignment (PPC64 ABIs require stack aligned to 16 bytes).
  16u,

  // Min/max stack offset - 64-bit loads and stores require the offset to be a multiple of 4.
  32764, 32767,

  // Supported register types.
  0u | (1u << uint32_t(RegType::kGp64  ))
     | (1u << uint32_t(RegType::kVec64 ))
     | (1u << uint32_t(RegType::kPPC_Cr)),

  // Instruction hints [Gp, Vec, Mask, Extra].
  {{
    InstHints::kNoHints,
    InstHints::kNoHints,
    InstHints::kNoHints,
    InstHints::kNoHints
  }},

  // TypeIdToRegType.
  #define V(index) (index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kInt8)    ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kUInt8)   ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kInt16)   ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kUInt16)  ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kInt32)   ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kUInt32)  ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kInt64)   ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kUInt64)  ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kIntPtr)  ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kUIntPtr) ? RegType::kGp64   : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kFloat32) ? RegType::kVec64  : \
                    index + uint32_t(TypeId::_kBaseStart) == uint32_t(TypeId::kFloat64) ? RegType::kVec64  : RegType::kNone)
  {{ ASMJIT_LOOKUP_TABLE_32(V, 0) }},
  #undef V

  // Word names of 8-bit, 16-bit, 32-bit, and 64-bit quantities.
  {
    ArchTypeNameId::kByte,
    ArchTypeNameId::kShort,
    ArchTypeNameId::kLong,
    ArchTypeNameId::kQuad
  }
};

//! \}
//! \endcond

ASMJIT_END_SUB_NAMESPACE

#endif // ASMJIT_PPC_PPC64ARCHTRAITS_P_H_INCLUDED
