// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <asmjit/core/api-build_p.h>
#if !defined(ASMJIT_NO_PPC64)

#include <asmjit/core/misc_p.h>
#include <asmjit/ppc/ppc64operand.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

// ppc::Operand - Tests
// ====================

#if defined(ASMJIT_TEST)
UNIT(ppc64_operand) {
  INFO("Checking if ppc::reg(...) matches built-in IDs");
  EXPECT_EQ(r(5), r5);
  EXPECT_EQ(gp64(5), r5);
  EXPECT_EQ(f(5), f5);

  INFO("Checking Gp register properties");
  EXPECT_TRUE(Gp().is_reg());
  EXPECT_TRUE(r0.is_reg());
  EXPECT_EQ(r0.id(), 0u);
  EXPECT_EQ(r31.id(), 31u);
  EXPECT_EQ(r1.id(), Gp::kIdSp);
  EXPECT_EQ(r2.id(), Gp::kIdToc);
  EXPECT_EQ(r13.id(), Gp::kIdThread);
  EXPECT_EQ(r31.id(), Gp::kIdFp);
  EXPECT_EQ(r0.size(), 8u);
  EXPECT_EQ(r0.reg_type(), RegType::kGp64);
  EXPECT_EQ(r0.reg_group(), RegGroup::kGp);
  EXPECT_TRUE(r0.is_r0());
  EXPECT_FALSE(r1.is_r0());
  EXPECT_TRUE(r1.is_sp());
  EXPECT_FALSE(r0.is_sp());

  INFO("Checking Vec register properties");
  EXPECT_TRUE(f0.is_reg());
  EXPECT_EQ(f0.id(), 0u);
  EXPECT_EQ(f31.id(), 31u);
  EXPECT_EQ(f0.size(), 8u);
  EXPECT_EQ(f0.reg_type(), RegType::kVec64);
  EXPECT_EQ(f0.reg_group(), RegGroup::kVec);
  EXPECT_NE(Operand(f3), Operand(r3));

  INFO("Checking Cr register properties");
  EXPECT_EQ(cr(5), cr5);
  EXPECT_TRUE(cr0.is_reg());
  EXPECT_EQ(cr7.id(), 7u);
  EXPECT_EQ(cr0.reg_type(), RegType::kPPC_Cr);
  EXPECT_EQ(cr0.reg_group(), RegGroup::kPPC_Cr);
  EXPECT_NE(Operand(cr3), Operand(r3));

  INFO("Checking Cr register bits");
  EXPECT_EQ(cr0.lt().value(), 0);
  EXPECT_EQ(cr0.gt().value(), 1);
  EXPECT_EQ(cr0.eq().value(), 2);
  EXPECT_EQ(cr0.so().value(), 3);
  EXPECT_EQ(cr1.eq().value(), 6);
  EXPECT_EQ(cr7.lt().value(), 28);
  EXPECT_EQ(cr7.un().value(), 31);

  INFO("Checking Mem [base + displacement]");
  Mem m_disp = ptr(r4, -8);
  EXPECT_TRUE(m_disp.is_mem());
  EXPECT_TRUE(m_disp.has_base_reg());
  EXPECT_FALSE(m_disp.has_base_label());
  EXPECT_FALSE(m_disp.has_index());
  EXPECT_EQ(m_disp.base_id(), 4u);
  EXPECT_EQ(m_disp.base_reg(), r4);
  EXPECT_EQ(m_disp.offset(), -8);
  EXPECT_EQ(m_disp.clone_adjusted(24).offset(), 16);

  INFO("Checking Mem [base + index]");
  Mem m_index = ptr(r4, r12);
  EXPECT_TRUE(m_index.has_base_reg());
  EXPECT_TRUE(m_index.has_index());
  EXPECT_EQ(m_index.base_reg(), r4);
  EXPECT_EQ(m_index.index_reg(), r12);
  EXPECT_EQ(m_index.offset(), 0);

  INFO("Checking Mem [label + displacement]");
  Label label;
  Mem m_label = ptr(label, 16);
  EXPECT_TRUE(m_label.has_base_label());
  EXPECT_FALSE(m_label.has_base_reg());
  EXPECT_FALSE(m_label.has_index());
  EXPECT_EQ(m_label.base_id(), label.id());
  EXPECT_EQ(m_label.offset(), 16);
}
#endif

ASMJIT_END_SUB_NAMESPACE

#endif // !ASMJIT_NO_PPC64
