/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

/*
 * Stubs for the instructions and shared fragments that have not been
 * implemented yet. Each one makes the emulator exit with the name of the
 * instruction or fragment when it is executed.
 *
 * A stub is removed from this file when the real implementation is added,
 * and the file is removed when it is empty.
 */

#include "beam_asm.hpp"

static void i_emit_nyi_fragment(const char *msg) {
    erts_exit(ERTS_ERROR_EXIT, "NYI (shared fragment): %s\n", msg);
}

void BeamGlobalAssembler::emit_nyi(const char *msg) {
    mov_imm(ARG1, msg);
    runtime_call<void (*)(const char *), i_emit_nyi_fragment>();

    /* Never returns */
}

/*
 * Shared fragments.
 */

void BeamGlobalAssembler::emit_bs_create_bin_error_shared() {
    emit_nyi("bs_create_bin_error_shared");
}

void BeamGlobalAssembler::emit_bs_get_utf8_shared() {
    emit_nyi("bs_get_utf8_shared");
}

void BeamGlobalAssembler::emit_bs_get_utf8_short_shared() {
    emit_nyi("bs_get_utf8_short_shared");
}

void BeamGlobalAssembler::emit_bs_init_bits_shared() {
    emit_nyi("bs_init_bits_shared");
}

void BeamGlobalAssembler::emit_check_float_error() {
    emit_nyi("check_float_error");
}

void BeamGlobalAssembler::emit_construct_utf8_shared() {
    emit_nyi("construct_utf8_shared");
}

void BeamGlobalAssembler::emit_create_native_record_shared() {
    emit_nyi("create_native_record_shared");
}

void BeamGlobalAssembler::emit_fconv_shared() {
    emit_nyi("fconv_shared");
}

void BeamGlobalAssembler::emit_get_sint64_shared() {
    emit_nyi("get_sint64_shared");
}

void BeamGlobalAssembler::emit_i_func_info_shared() {
    emit_nyi("i_func_info_shared");
}

void BeamGlobalAssembler::emit_int_div_rem_body_shared() {
    emit_nyi("int_div_rem_body_shared");
}

void BeamGlobalAssembler::emit_int_div_rem_guard_shared() {
    emit_nyi("int_div_rem_guard_shared");
}

void BeamGlobalAssembler::emit_store_unaligned() {
    emit_nyi("store_unaligned");
}

/*
 * Instructions.
 */

void BeamModuleAssembler::emit_bs_init_writable() {
    emit_nyi("bs_init_writable");
}

void BeamModuleAssembler::emit_fconv(const ArgSource &, const ArgFRegister &) {
    emit_nyi("fconv");
}

void BeamModuleAssembler::emit_fload(const ArgSource &, const ArgFRegister &) {
    emit_nyi("fload");
}

void BeamModuleAssembler::emit_fstore(const ArgFRegister &,
                                      const ArgRegister &) {
    emit_nyi("fstore");
}

void BeamModuleAssembler::emit_get_record_field(const ArgLabel &,
                                                const ArgRegister &,
                                                const ArgConstant &,
                                                const ArgAtom &,
                                                const ArgRegister &) {
    emit_nyi("get_record_field");
}

void BeamModuleAssembler::emit_i_bs_create_bin(const ArgLabel &,
                                               const ArgWord &,
                                               const ArgWord &,
                                               const ArgRegister &,
                                               const Span<const ArgVal> &) {
    emit_nyi("i_bs_create_bin");
}

void BeamModuleAssembler::emit_i_bs_get_utf16(const ArgRegister &,
                                              const ArgLabel &,
                                              const ArgWord &,
                                              const ArgRegister &) {
    emit_nyi("i_bs_get_utf16");
}

void BeamModuleAssembler::emit_i_bs_get_utf8(const ArgRegister &,
                                             const ArgLabel &,
                                             const ArgRegister &) {
    emit_nyi("i_bs_get_utf8");
}

void BeamModuleAssembler::emit_i_bs_skip_utf16(const ArgRegister &,
                                               const ArgLabel &,
                                               const ArgWord &) {
    emit_nyi("i_bs_skip_utf16");
}

void BeamModuleAssembler::emit_i_bs_skip_utf8(const ArgRegister &,
                                              const ArgLabel &) {
    emit_nyi("i_bs_skip_utf8");
}

void BeamModuleAssembler::emit_i_bs_validate_unicode_retract(
        const ArgLabel &,
        const ArgSource &,
        const ArgRegister &) {
    emit_nyi("i_bs_validate_unicode_retract");
}

void BeamModuleAssembler::emit_i_create_local_native_record(
        const ArgLiteral &,
        const ArgRegister &,
        const ArgWord &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_create_local_native_record");
}

void BeamModuleAssembler::emit_i_create_native_record(
        const ArgLabel &,
        const ArgConstant &,
        const ArgRegister &,
        const ArgWord &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_create_native_record");
}

void BeamModuleAssembler::emit_i_div_rem(const ArgLabel &,
                                         const ArgWord &,
                                         const ArgSource &,
                                         const ArgSource &,
                                         const ArgRegister &,
                                         const ArgRegister &) {
    emit_nyi("i_div_rem");
}

void BeamModuleAssembler::emit_i_fadd(const ArgFRegister &,
                                      const ArgFRegister &,
                                      const ArgFRegister &) {
    emit_nyi("i_fadd");
}

void BeamModuleAssembler::emit_i_fdiv(const ArgFRegister &,
                                      const ArgFRegister &,
                                      const ArgFRegister &) {
    emit_nyi("i_fdiv");
}

void BeamModuleAssembler::emit_i_fmul(const ArgFRegister &,
                                      const ArgFRegister &,
                                      const ArgFRegister &) {
    emit_nyi("i_fmul");
}

void BeamModuleAssembler::emit_i_fnegate(const ArgFRegister &,
                                         const ArgFRegister &) {
    emit_nyi("i_fnegate");
}

void BeamModuleAssembler::emit_i_fsub(const ArgFRegister &,
                                      const ArgFRegister &,
                                      const ArgFRegister &) {
    emit_nyi("i_fsub");
}

void BeamModuleAssembler::emit_i_get_local_record_elements(
        const ArgLiteral &,
        const ArgRegister &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_get_local_record_elements");
}

void BeamModuleAssembler::emit_i_get_record_elements(
        const ArgLabel &,
        const ArgRegister &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_get_record_elements");
}

void BeamModuleAssembler::emit_i_int_div(const ArgLabel &,
                                         const ArgWord &,
                                         const ArgSource &,
                                         const ArgSource &,
                                         const ArgRegister &) {
    emit_nyi("i_int_div");
}

void BeamModuleAssembler::emit_i_m_div(const ArgLabel &,
                                       const ArgWord &,
                                       const ArgSource &,
                                       const ArgSource &,
                                       const ArgRegister &) {
    emit_nyi("i_m_div");
}

void BeamModuleAssembler::emit_i_rem(const ArgLabel &,
                                     const ArgWord &,
                                     const ArgSource &,
                                     const ArgSource &,
                                     const ArgRegister &) {
    emit_nyi("i_rem");
}

void BeamModuleAssembler::emit_i_rem_div(const ArgLabel &,
                                         const ArgWord &,
                                         const ArgSource &,
                                         const ArgSource &,
                                         const ArgRegister &,
                                         const ArgRegister &) {
    emit_nyi("i_rem_div");
}

void BeamModuleAssembler::emit_i_update_local_native_record(
        const ArgAtom &,
        const ArgLiteral &,
        const ArgSource &,
        const ArgRegister &,
        const ArgWord &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_update_local_native_record");
}

void BeamModuleAssembler::emit_i_update_native_record(
        const ArgLabel &,
        const ArgSource &,
        const ArgRegister &,
        const ArgWord &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_update_native_record");
}

void BeamModuleAssembler::emit_is_any_native_record(const ArgLabel &,
                                                    const ArgRegister &) {
    emit_nyi("is_any_native_record");
}

void BeamModuleAssembler::emit_is_native_record(const ArgLabel &,
                                                const ArgRegister &,
                                                const ArgAtom &,
                                                const ArgAtom &) {
    emit_nyi("is_native_record");
}

void BeamModuleAssembler::emit_is_record_accessible(const ArgLabel &,
                                                    const ArgRegister &,
                                                    const ArgAtom &) {
    emit_nyi("is_record_accessible");
}
