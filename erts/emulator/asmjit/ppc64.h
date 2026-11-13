// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef ASMJIT_PPC64_H_INCLUDED
#define ASMJIT_PPC64_H_INCLUDED

//! \addtogroup asmjit_ppc64
//!
//! \note The PPC64 backend is under construction, it provides an assembler that supports only few instructions.
//!
//! ### Emitters
//!
//!   - \ref ppc::Assembler - PPC64 assembler.
//!   - \ref ppc::Emitter - PPC64 emitter (abstract).
//!
//! ### Supported Instructions
//!
//!   - Emitters:
//!     - \ref ppc::EmitterExplicitT - Provides all instructions that use explicit operands, provides also utility
//!       functions. The member functions provided are part of all PPC64 emitters.
//!
//!   - Instruction representation:
//!     - \ref ppc::Inst::Id - instruction identifiers.
//!
//! ### Register Operands
//!
//!   - \ref ppc::Gp - General purpose register (64-bit).
//!   - \ref ppc::Vec - Floating point register (64-bit).
//!   - \ref ppc::Cr - Condition register field.
//!
//! ### Memory Operands
//!
//!   - \ref ppc::Mem - PPC64 memory operand that provides `base + displacement`, `base + index`, and label based
//!     addressing.

#include <asmjit/core.h>

#include <asmjit/asmjit-scope-begin.h>
#include <asmjit/ppc/ppc64assembler.h>
#include <asmjit/ppc/ppc64emitter.h>
#include <asmjit/ppc/ppc64globals.h>
#include <asmjit/ppc/ppc64instdb.h>
#include <asmjit/ppc/ppc64operand.h>
#include <asmjit/asmjit-scope-end.h>

#endif // ASMJIT_PPC64_H_INCLUDED
