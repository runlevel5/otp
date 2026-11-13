// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <asmjit/core/api-build_p.h>
#if !defined(ASMJIT_NO_PPC64)

#include <asmjit/core/misc_p.h>
#include <asmjit/core/string.h>
#include <asmjit/ppc/ppc64instapi_p.h>
#include <asmjit/ppc/ppc64instdb_p.h>

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

namespace InstInternal {

// ppc::InstInternal - Text
// ========================

#ifndef ASMJIT_NO_TEXT
Error inst_id_to_string(InstId inst_id, InstStringifyOptions options, String& output) noexcept {
  Support::maybe_unused(options);

  if (ASMJIT_UNLIKELY(!Inst::is_defined_id(inst_id))) {
    return make_error(Error::kInvalidInstruction);
  }

  return output.append(InstDB::_inst_name_table[inst_id]);
}

InstId string_to_inst_id(const char* s, size_t len) noexcept {
  if (ASMJIT_UNLIKELY(!s)) {
    return BaseInst::kIdNone;
  }

  if (len == SIZE_MAX) {
    len = strlen(s);
  }

  if (len == 0u || len > InstDB::kMaxInstNameSize) {
    return BaseInst::kIdNone;
  }

  for (uint32_t inst_id = 1; inst_id < Inst::_kIdCount; inst_id++) {
    const char* name = InstDB::_inst_name_table[inst_id];
    if (strlen(name) == len && memcmp(name, s, len) == 0) {
      return inst_id;
    }
  }

  return BaseInst::kIdNone;
}
#endif // !ASMJIT_NO_TEXT

} // {InstInternal}

// ppc::InstInternal - Tests
// =========================

#if defined(ASMJIT_TEST) && !defined(ASMJIT_NO_TEXT)
UNIT(ppc64_inst_api_text) {
  INFO("Checking that every instruction name converts back to its id");
  for (InstId inst_id = 1; inst_id < Inst::_kIdCount; inst_id++) {
    StringTmp<32> name;

    EXPECT_EQ(InstInternal::inst_id_to_string(inst_id, InstStringifyOptions::kNone, name), Error::kOk);
    EXPECT_FALSE(name.is_empty());
    EXPECT_EQ(InstInternal::string_to_inst_id(name.data(), name.size()), inst_id);
    EXPECT_EQ(InstInternal::string_to_inst_id(name.data(), SIZE_MAX), inst_id);
  }

  INFO("Checking names that are not instructions");
  EXPECT_EQ(InstInternal::string_to_inst_id("", 0), BaseInst::kIdNone);
  EXPECT_EQ(InstInternal::string_to_inst_id("ad", 2), BaseInst::kIdNone);
  EXPECT_EQ(InstInternal::string_to_inst_id("addx", 4), BaseInst::kIdNone);
  EXPECT_EQ(InstInternal::string_to_inst_id("an_instruction_name_that_is_too_long", SIZE_MAX), BaseInst::kIdNone);

  INFO("Checking an instruction id that is not defined");
  StringTmp<32> name;
  EXPECT_EQ(InstInternal::inst_id_to_string(Inst::_kIdCount, InstStringifyOptions::kNone, name), Error::kInvalidInstruction);
}
#endif

ASMJIT_END_SUB_NAMESPACE

#endif // !ASMJIT_NO_PPC64
