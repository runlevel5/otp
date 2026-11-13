// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef ASMJIT_PPC_PPC64ASSEMBLER_H_INCLUDED
#define ASMJIT_PPC_PPC64ASSEMBLER_H_INCLUDED

#include <asmjit/core/assembler.h>
#include <asmjit/ppc/ppc64emitter.h>
#include <asmjit/ppc/ppc64operand.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

//! \addtogroup asmjit_ppc64
//! \{

//! PPC64 assembler implementation.
//!
//! Instructions are written in the byte order of the target architecture, which is either \ref Arch::kPPC64_LE
//! or \ref Arch::kPPC64_BE.
class ASMJIT_VIRTAPI Assembler
  : public BaseAssembler,
    public EmitterExplicitT<Assembler> {

public:
  using Base = BaseAssembler;

  //! \name Construction & Destruction
  //! \{

  ASMJIT_API Assembler(CodeHolder* code = nullptr) noexcept;
  ASMJIT_API ~Assembler() noexcept override;

  //! \}

  //! \name Emit
  //! \{

  ASMJIT_API Error _emit(InstId inst_id, const Operand_& o0, const Operand_& o1, const Operand_& o2, const Operand_* op_ext) override;

  //! \}

  //! \name Align
  //! \{

  ASMJIT_API Error align(AlignMode align_mode, uint32_t alignment) override;

  //! \}

  //! \name Events
  //! \{

  ASMJIT_API Error on_attach(CodeHolder& code) noexcept override;
  ASMJIT_API Error on_detach(CodeHolder& code) noexcept override;

  //! \}
};

//! \}

ASMJIT_END_SUB_NAMESPACE

#endif // ASMJIT_PPC_PPC64ASSEMBLER_H_INCLUDED
