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

enum erts_is_line_breakpoint BeamGlobalAssembler::is_line_breakpoint_trampoline(
        ErtsCodePtr addr) {
    (void)addr;

    return IS_NOT_LINE_BP;
}

void BeamModuleAssembler::emit_coverage(void *coverage, Uint index, Uint size) {
    (void)coverage;
    (void)index;
    (void)size;

    emit_nyi("coverage");
}

/*
 * Shared fragments.
 */

void BeamGlobalAssembler::emit_apply_fun_shared() {
    emit_nyi("apply_fun_shared");
}

void BeamGlobalAssembler::emit_arith_compare_shared() {
    emit_nyi("arith_compare_shared");
}

void BeamGlobalAssembler::emit_bif_nif_epilogue() {
    emit_nyi("bif_nif_epilogue");
}

void BeamGlobalAssembler::emit_bif_bit_size_body() {
    emit_nyi("bif_bit_size_body");
}

void BeamGlobalAssembler::emit_bif_byte_size_body() {
    emit_nyi("bif_byte_size_body");
}

void BeamGlobalAssembler::emit_bif_element_body_shared() {
    emit_nyi("bif_element_body_shared");
}

void BeamGlobalAssembler::emit_bif_element_guard_shared() {
    emit_nyi("bif_element_guard_shared");
}

void BeamGlobalAssembler::emit_bif_is_eq_exact_shared() {
    emit_nyi("bif_is_eq_exact_shared");
}

void BeamGlobalAssembler::emit_bif_is_ne_exact_shared() {
    emit_nyi("bif_is_ne_exact_shared");
}

void BeamGlobalAssembler::emit_bif_tuple_size_body() {
    emit_nyi("bif_tuple_size_body");
}

void BeamGlobalAssembler::emit_bif_tuple_size_guard() {
    emit_nyi("bif_tuple_size_guard");
}

void BeamGlobalAssembler::emit_bs_create_bin_error_shared() {
    emit_nyi("bs_create_bin_error_shared");
}

void BeamGlobalAssembler::emit_bs_get_tail_shared() {
    emit_nyi("bs_get_tail_shared");
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

void BeamGlobalAssembler::emit_bs_size_check_shared() {
    emit_nyi("bs_size_check_shared");
}

void BeamGlobalAssembler::emit_call_bif_shared() {
    emit_nyi("call_bif_shared");
}

void BeamGlobalAssembler::emit_call_light_bif_shared() {
    emit_nyi("call_light_bif_shared");
}

void BeamGlobalAssembler::emit_call_nif_yield_helper() {
    emit_nyi("call_nif_yield_helper");
}

void BeamGlobalAssembler::emit_catch_end_shared() {
    emit_nyi("catch_end_shared");
}

void BeamGlobalAssembler::emit_call_nif_early() {
    emit_nyi("call_nif_early");
}

void BeamGlobalAssembler::emit_call_nif_shared() {
    emit_nyi("call_nif_shared");
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

void BeamGlobalAssembler::emit_debug_bp() {
    emit_nyi("debug_bp");
}

void BeamGlobalAssembler::emit_dispatch_bif() {
    emit_nyi("dispatch_bif");
}

void BeamGlobalAssembler::emit_dispatch_nif() {
    emit_nyi("dispatch_nif");
}

void BeamGlobalAssembler::emit_dispatch_return() {
    emit_nyi("dispatch_return");
}

void BeamGlobalAssembler::emit_dispatch_save_calls_export() {
    emit_nyi("dispatch_save_calls_export");
}

void BeamGlobalAssembler::emit_dispatch_save_calls_fun() {
    emit_nyi("dispatch_save_calls_fun");
}

void BeamGlobalAssembler::emit_fconv_shared() {
    emit_nyi("fconv_shared");
}

void BeamGlobalAssembler::emit_get_sint64_shared() {
    emit_nyi("get_sint64_shared");
}

void BeamGlobalAssembler::emit_handle_and_error() {
    emit_nyi("handle_and_error");
}

void BeamGlobalAssembler::emit_handle_call_fun_error() {
    emit_nyi("handle_call_fun_error");
}

void BeamGlobalAssembler::emit_handle_element_error_shared() {
    emit_nyi("handle_element_error_shared");
}

void BeamGlobalAssembler::emit_handle_hd_error() {
    emit_nyi("handle_hd_error");
}

void BeamGlobalAssembler::emit_handle_map_get_badkey() {
    emit_nyi("handle_map_get_badkey");
}

void BeamGlobalAssembler::emit_handle_map_get_badmap() {
    emit_nyi("handle_map_get_badmap");
}

void BeamGlobalAssembler::emit_handle_map_size_error() {
    emit_nyi("handle_map_size_error");
}

void BeamGlobalAssembler::emit_handle_node_error() {
    emit_nyi("handle_node_error");
}

void BeamGlobalAssembler::emit_handle_not_error() {
    emit_nyi("handle_not_error");
}

void BeamGlobalAssembler::emit_handle_or_error() {
    emit_nyi("handle_or_error");
}

void BeamGlobalAssembler::emit_handle_tl_error() {
    emit_nyi("handle_tl_error");
}

void BeamGlobalAssembler::emit_generic_bp_global() {
    emit_nyi("generic_bp_global");
}

void BeamGlobalAssembler::emit_generic_bp_local() {
    emit_nyi("generic_bp_local");
}

void BeamGlobalAssembler::emit_i_band_body_shared() {
    emit_nyi("i_band_body_shared");
}

void BeamGlobalAssembler::emit_i_bnot_body_shared() {
    emit_nyi("i_bnot_body_shared");
}

void BeamGlobalAssembler::emit_i_bnot_guard_shared() {
    emit_nyi("i_bnot_guard_shared");
}

void BeamGlobalAssembler::emit_i_bor_body_shared() {
    emit_nyi("i_bor_body_shared");
}

void BeamGlobalAssembler::emit_i_bif_body_shared() {
    emit_nyi("i_bif_body_shared");
}

void BeamGlobalAssembler::emit_i_bif_guard_shared() {
    emit_nyi("i_bif_guard_shared");
}

void BeamGlobalAssembler::emit_i_breakpoint_trampoline_shared() {
    emit_nyi("i_breakpoint_trampoline_shared");
}

void BeamGlobalAssembler::emit_i_bsr_body_shared() {
    emit_nyi("i_bsr_body_shared");
}

void BeamGlobalAssembler::emit_i_bsl_body_shared() {
    emit_nyi("i_bsl_body_shared");
}

void BeamGlobalAssembler::emit_i_ctz_body_shared() {
    emit_nyi("i_ctz_body_shared");
}

void BeamGlobalAssembler::emit_i_func_info_shared() {
    emit_nyi("i_func_info_shared");
}

void BeamGlobalAssembler::emit_i_get_map_element_shared() {
    emit_nyi("i_get_map_element_shared");
}

void BeamGlobalAssembler::emit_i_get_map_element_hash_shared() {
    emit_nyi("i_get_map_element_hash_shared");
}

void BeamGlobalAssembler::emit_i_length_guard_shared() {
    emit_nyi("i_length_guard_shared");
}

void BeamGlobalAssembler::emit_i_length_body_shared() {
    emit_nyi("i_length_body_shared");
}

void BeamGlobalAssembler::emit_i_line_breakpoint_trampoline_shared() {
    emit_nyi("i_line_breakpoint_trampoline_shared");

    a.bind(labels[i_line_breakpoint_cleanup]);
    emit_nyi("i_line_breakpoint_cleanup");
}

void BeamGlobalAssembler::emit_i_loop_rec_shared() {
    emit_nyi("i_loop_rec_shared");
}

void BeamGlobalAssembler::emit_i_popcount_body_shared() {
    emit_nyi("i_popcount_body_shared");
}

void BeamGlobalAssembler::emit_i_signed_width_body_shared() {
    emit_nyi("i_signed_width_body_shared");
}

void BeamGlobalAssembler::emit_i_test_yield_shared() {
    emit_nyi("i_test_yield_shared");
}

void BeamGlobalAssembler::emit_i_bxor_body_shared() {
    emit_nyi("i_bxor_body_shared");
}

void BeamGlobalAssembler::emit_i_unsigned_width_body_shared() {
    emit_nyi("i_unsigned_width_body_shared");
}

void BeamGlobalAssembler::emit_int128_to_big_shared() {
    emit_nyi("int128_to_big_shared");
}

void BeamGlobalAssembler::emit_int_div_rem_body_shared() {
    emit_nyi("int_div_rem_body_shared");
}

void BeamGlobalAssembler::emit_int_div_rem_guard_shared() {
    emit_nyi("int_div_rem_guard_shared");
}

void BeamGlobalAssembler::emit_is_eq_exact_list_shared() {
    emit_nyi("is_eq_exact_list_shared");
}

void BeamGlobalAssembler::emit_is_eq_exact_shallow_boxed_shared() {
    emit_nyi("is_eq_exact_shallow_boxed_shared");
}

void BeamGlobalAssembler::emit_is_in_range_shared() {
    emit_nyi("is_in_range_shared");
}

void BeamGlobalAssembler::emit_is_ge_lt_shared() {
    emit_nyi("is_ge_lt_shared");
}

void BeamGlobalAssembler::emit_minus_body_shared() {
    emit_nyi("minus_body_shared");
}

void BeamGlobalAssembler::emit_mul_add_body_shared() {
    emit_nyi("mul_add_body_shared");
}

void BeamGlobalAssembler::emit_mul_add_guard_shared() {
    emit_nyi("mul_add_guard_shared");
}

void BeamGlobalAssembler::emit_mul_guard_shared() {
    emit_nyi("mul_guard_shared");
}

void BeamGlobalAssembler::emit_new_map_shared() {
    emit_nyi("new_map_shared");
}

void BeamGlobalAssembler::emit_plus_body_shared() {
    emit_nyi("plus_body_shared");
}

void BeamGlobalAssembler::emit_raise_shared() {
    emit_nyi("raise_shared");
}

void BeamGlobalAssembler::emit_store_unaligned() {
    emit_nyi("store_unaligned");
}

void BeamGlobalAssembler::emit_unloaded_fun() {
    emit_nyi("unloaded_fun");
}

void BeamGlobalAssembler::emit_unary_minus_body_shared() {
    emit_nyi("unary_minus_body_shared");
}

void BeamGlobalAssembler::emit_update_map_assoc_shared() {
    emit_nyi("update_map_assoc_shared");
}

void BeamGlobalAssembler::emit_update_map_single_assoc_shared() {
    emit_nyi("update_map_single_assoc_shared");
}

void BeamGlobalAssembler::emit_update_map_exact_guard_shared() {
    emit_nyi("update_map_exact_guard_shared");
}

void BeamGlobalAssembler::emit_update_map_exact_body_shared() {
    emit_nyi("update_map_exact_body_shared");
}

void BeamGlobalAssembler::emit_update_map_single_exact_body_shared() {
    emit_nyi("update_map_single_exact_body_shared");
}

/*
 * Instructions.
 */

void BeamModuleAssembler::emit_allocate(const ArgWord &, const ArgWord &) {
    emit_nyi("allocate");
}

void BeamModuleAssembler::emit_allocate_heap(const ArgWord &,
                                             const ArgWord &,
                                             const ArgWord &) {
    emit_nyi("allocate_heap");
}

void BeamModuleAssembler::emit_apply(const ArgWord &) {
    emit_nyi("apply");
}

void BeamModuleAssembler::emit_apply_last(const ArgWord &, const ArgWord &) {
    emit_nyi("apply_last");
}

void BeamModuleAssembler::emit_badmatch(const ArgSource &) {
    emit_nyi("badmatch");
}

void BeamModuleAssembler::emit_badrecord(const ArgSource &) {
    emit_nyi("badrecord");
}

void BeamModuleAssembler::emit_bif_and(const ArgLabel &,
                                       const ArgSource &,
                                       const ArgSource &,
                                       const ArgRegister &) {
    emit_nyi("bif_and");
}

void BeamModuleAssembler::emit_bif_bit_size(const ArgLabel &,
                                            const ArgSource &,
                                            const ArgRegister &) {
    emit_nyi("bif_bit_size");
}

void BeamModuleAssembler::emit_bif_byte_size(const ArgLabel &,
                                             const ArgSource &,
                                             const ArgRegister &) {
    emit_nyi("bif_byte_size");
}

void BeamModuleAssembler::emit_bif_element(const ArgLabel &,
                                           const ArgSource &,
                                           const ArgSource &,
                                           const ArgRegister &) {
    emit_nyi("bif_element");
}

void BeamModuleAssembler::emit_bif_hd(const ArgSource &, const ArgRegister &) {
    emit_nyi("bif_hd");
}

void BeamModuleAssembler::emit_bif_is_eq_exact(const ArgRegister &,
                                               const ArgSource &,
                                               const ArgRegister &) {
    emit_nyi("bif_is_eq_exact");
}

void BeamModuleAssembler::emit_bif_is_ge(const ArgSource &,
                                         const ArgSource &,
                                         const ArgRegister &) {
    emit_nyi("bif_is_ge");
}

void BeamModuleAssembler::emit_bif_is_lt(const ArgSource &,
                                         const ArgSource &,
                                         const ArgRegister &) {
    emit_nyi("bif_is_lt");
}

void BeamModuleAssembler::emit_bif_is_map_key(const ArgWord &,
                                              const ArgLabel &,
                                              const ArgSource &,
                                              const ArgSource &,
                                              const ArgRegister &) {
    emit_nyi("bif_is_map_key");
}

void BeamModuleAssembler::emit_bif_is_ne_exact(const ArgRegister &,
                                               const ArgSource &,
                                               const ArgRegister &) {
    emit_nyi("bif_is_ne_exact");
}

void BeamModuleAssembler::emit_bif_map_get(const ArgLabel &,
                                           const ArgSource &,
                                           const ArgSource &,
                                           const ArgRegister &) {
    emit_nyi("bif_map_get");
}

void BeamModuleAssembler::emit_bif_map_size(const ArgLabel &,
                                            const ArgSource &,
                                            const ArgRegister &) {
    emit_nyi("bif_map_size");
}

void BeamModuleAssembler::emit_bif_max(const ArgSource &,
                                       const ArgSource &,
                                       const ArgRegister &) {
    emit_nyi("bif_max");
}

void BeamModuleAssembler::emit_bif_min(const ArgSource &,
                                       const ArgSource &,
                                       const ArgRegister &) {
    emit_nyi("bif_min");
}

void BeamModuleAssembler::emit_bif_node(const ArgLabel &,
                                        const ArgRegister &,
                                        const ArgRegister &) {
    emit_nyi("bif_node");
}

void BeamModuleAssembler::emit_bif_not(const ArgLabel &,
                                       const ArgRegister &,
                                       const ArgRegister &) {
    emit_nyi("bif_not");
}

void BeamModuleAssembler::emit_bif_or(const ArgLabel &,
                                      const ArgSource &,
                                      const ArgSource &,
                                      const ArgRegister &) {
    emit_nyi("bif_or");
}

void BeamModuleAssembler::emit_bif_tl(const ArgSource &, const ArgRegister &) {
    emit_nyi("bif_tl");
}

void BeamModuleAssembler::emit_bif_tuple_size(const ArgLabel &,
                                              const ArgRegister &,
                                              const ArgRegister &) {
    emit_nyi("bif_tuple_size");
}

void BeamModuleAssembler::emit_bs_get_integer2(const ArgLabel &,
                                               const ArgRegister &,
                                               const ArgWord &,
                                               const ArgSource &,
                                               const ArgWord &,
                                               const ArgWord &,
                                               const ArgRegister &) {
    emit_nyi("bs_get_integer2");
}

void BeamModuleAssembler::emit_bs_get_tail(const ArgRegister &,
                                           const ArgRegister &,
                                           const ArgWord &) {
    emit_nyi("bs_get_tail");
}

void BeamModuleAssembler::emit_bs_init_writable() {
    emit_nyi("bs_init_writable");
}

void BeamModuleAssembler::emit_bs_set_position(const ArgRegister &,
                                               const ArgRegister &) {
    emit_nyi("bs_set_position");
}

void BeamModuleAssembler::emit_bs_test_tail2(const ArgLabel &,
                                             const ArgRegister &,
                                             const ArgWord &) {
    emit_nyi("bs_test_tail2");
}

void BeamModuleAssembler::emit_build_stacktrace() {
    emit_nyi("build_stacktrace");
}

void BeamModuleAssembler::emit_call_bif(const ArgWord &) {
    emit_nyi("call_bif");
}

void BeamModuleAssembler::emit_call_bif_mfa(const ArgAtom &,
                                            const ArgAtom &,
                                            const ArgWord &) {
    emit_nyi("call_bif_mfa");
}

void BeamModuleAssembler::emit_call_light_bif(const ArgWord &,
                                              const ArgExport &) {
    emit_nyi("call_light_bif");
}

void BeamModuleAssembler::emit_call_nif(const ArgWord &,
                                        const ArgWord &,
                                        const ArgWord &) {
    emit_nyi("call_nif");
}

void BeamModuleAssembler::emit_case_end(const ArgSource &) {
    emit_nyi("case_end");
}

void BeamModuleAssembler::emit_catch(const ArgYRegister &, const ArgCatch &) {
    emit_nyi("catch");
}

void BeamModuleAssembler::emit_catch_end(const ArgYRegister &) {
    emit_nyi("catch_end");
}

void BeamModuleAssembler::emit_continue_exit() {
    emit_nyi("continue_exit");
}

void BeamModuleAssembler::emit_deallocate(const ArgWord &) {
    emit_nyi("deallocate");
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

void BeamModuleAssembler::emit_get_hd(const ArgRegister &,
                                      const ArgRegister &) {
    emit_nyi("get_hd");
}

void BeamModuleAssembler::emit_get_list(const ArgRegister &,
                                        const ArgRegister &,
                                        const ArgRegister &) {
    emit_nyi("get_list");
}

void BeamModuleAssembler::emit_get_record_field(const ArgLabel &,
                                                const ArgRegister &,
                                                const ArgConstant &,
                                                const ArgAtom &,
                                                const ArgRegister &) {
    emit_nyi("get_record_field");
}

void BeamModuleAssembler::emit_get_tl(const ArgRegister &,
                                      const ArgRegister &) {
    emit_nyi("get_tl");
}

void BeamModuleAssembler::emit_get_tuple_element_swap(const ArgSource &,
                                                      const ArgWord &,
                                                      const ArgRegister &,
                                                      const ArgRegister &) {
    emit_nyi("get_tuple_element_swap");
}

void BeamModuleAssembler::emit_get_two_tuple_elements(const ArgSource &,
                                                      const ArgWord &,
                                                      const ArgRegister &,
                                                      const ArgRegister &) {
    emit_nyi("get_two_tuple_elements");
}

void BeamModuleAssembler::emit_i_apply() {
    emit_nyi("i_apply");
}

void BeamModuleAssembler::emit_i_apply_fun() {
    emit_nyi("i_apply_fun");
}

void BeamModuleAssembler::emit_i_apply_fun_last(const ArgWord &) {
    emit_nyi("i_apply_fun_last");
}

void BeamModuleAssembler::emit_i_apply_fun_only() {
    emit_nyi("i_apply_fun_only");
}

void BeamModuleAssembler::emit_i_apply_last(const ArgWord &) {
    emit_nyi("i_apply_last");
}

void BeamModuleAssembler::emit_i_apply_only() {
    emit_nyi("i_apply_only");
}

void BeamModuleAssembler::emit_i_band(const ArgLabel &,
                                      const ArgWord &,
                                      const ArgSource &,
                                      const ArgSource &,
                                      const ArgRegister &) {
    emit_nyi("i_band");
}

void BeamModuleAssembler::emit_i_bif1(const ArgSource &,
                                      const ArgLabel &,
                                      const ArgWord &,
                                      const ArgRegister &) {
    emit_nyi("i_bif1");
}

void BeamModuleAssembler::emit_i_bif2(const ArgSource &,
                                      const ArgSource &,
                                      const ArgLabel &,
                                      const ArgWord &,
                                      const ArgRegister &) {
    emit_nyi("i_bif2");
}

void BeamModuleAssembler::emit_i_bif3(const ArgSource &,
                                      const ArgSource &,
                                      const ArgSource &,
                                      const ArgLabel &,
                                      const ArgWord &,
                                      const ArgRegister &) {
    emit_nyi("i_bif3");
}

void BeamModuleAssembler::emit_i_bnot(const ArgLabel &,
                                      const ArgWord &,
                                      const ArgSource &,
                                      const ArgRegister &) {
    emit_nyi("i_bnot");
}

void BeamModuleAssembler::emit_i_bor(const ArgLabel &,
                                     const ArgWord &,
                                     const ArgSource &,
                                     const ArgSource &,
                                     const ArgRegister &) {
    emit_nyi("i_bor");
}

void BeamModuleAssembler::emit_i_bs_create_bin(const ArgLabel &,
                                               const ArgWord &,
                                               const ArgWord &,
                                               const ArgRegister &,
                                               const Span<const ArgVal> &) {
    emit_nyi("i_bs_create_bin");
}

void BeamModuleAssembler::emit_i_bs_get_bin_and_tail(const ArgRegister &,
                                                     const ArgLabel &,
                                                     const ArgWord &,
                                                     const ArgRegister &,
                                                     const ArgWord &,
                                                     const ArgRegister &,
                                                     const ArgRegister &) {
    emit_nyi("i_bs_get_bin_and_tail");
}

void BeamModuleAssembler::emit_i_bs_get_binary2(const ArgRegister &,
                                                const ArgLabel &,
                                                const ArgWord &,
                                                const ArgSource &,
                                                const ArgWord &,
                                                const ArgRegister &) {
    emit_nyi("i_bs_get_binary2");
}

void BeamModuleAssembler::emit_i_bs_get_binary_all2(const ArgRegister &,
                                                    const ArgLabel &,
                                                    const ArgWord &,
                                                    const ArgWord &,
                                                    const ArgRegister &) {
    emit_nyi("i_bs_get_binary_all2");
}

void BeamModuleAssembler::emit_i_bs_get_float2(const ArgRegister &,
                                               const ArgLabel &,
                                               const ArgWord &,
                                               const ArgSource &,
                                               const ArgWord &,
                                               const ArgRegister &) {
    emit_nyi("i_bs_get_float2");
}

void BeamModuleAssembler::emit_i_bs_get_position(const ArgRegister &,
                                                 const ArgRegister &) {
    emit_nyi("i_bs_get_position");
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

void BeamModuleAssembler::emit_i_bs_match(const ArgLabel &,
                                          const ArgRegister &,
                                          const Span<const ArgVal> &) {
    emit_nyi("i_bs_match");
}

void BeamModuleAssembler::emit_i_bs_match_string(const ArgRegister &,
                                                 const ArgLabel &,
                                                 const ArgWord &,
                                                 const ArgBytePtr &) {
    emit_nyi("i_bs_match_string");
}

void BeamModuleAssembler::emit_i_bs_match_test_heap(
        const ArgLabel &,
        const ArgRegister &,
        const ArgWord &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_bs_match_test_heap");
}

void BeamModuleAssembler::emit_i_bs_skip_bits2(const ArgRegister &,
                                               const ArgSource &,
                                               const ArgLabel &,
                                               const ArgWord &) {
    emit_nyi("i_bs_skip_bits2");
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

void BeamModuleAssembler::emit_i_bs_start_match3(const ArgRegister &,
                                                 const ArgWord &,
                                                 const ArgLabel &,
                                                 const ArgRegister &) {
    emit_nyi("i_bs_start_match3");
}

void BeamModuleAssembler::emit_i_bs_validate_unicode_retract(
        const ArgLabel &,
        const ArgSource &,
        const ArgRegister &) {
    emit_nyi("i_bs_validate_unicode_retract");
}

void BeamModuleAssembler::emit_i_bsl(const ArgLabel &,
                                     const ArgWord &,
                                     const ArgSource &,
                                     const ArgSource &,
                                     const ArgRegister &) {
    emit_nyi("i_bsl");
}

void BeamModuleAssembler::emit_i_bsr(const ArgLabel &,
                                     const ArgWord &,
                                     const ArgSource &,
                                     const ArgSource &,
                                     const ArgRegister &) {
    emit_nyi("i_bsr");
}

void BeamModuleAssembler::emit_i_bxor(const ArgLabel &,
                                      const ArgWord &,
                                      const ArgSource &,
                                      const ArgSource &,
                                      const ArgRegister &) {
    emit_nyi("i_bxor");
}

void BeamModuleAssembler::emit_i_call(const ArgLabel &) {
    emit_nyi("i_call");
}

void BeamModuleAssembler::emit_i_call_ext(const ArgExport &) {
    emit_nyi("i_call_ext");
}

void BeamModuleAssembler::emit_i_call_ext_last(const ArgExport &,
                                               const ArgWord &) {
    emit_nyi("i_call_ext_last");
}

void BeamModuleAssembler::emit_i_call_ext_only(const ArgExport &) {
    emit_nyi("i_call_ext_only");
}

void BeamModuleAssembler::emit_i_call_fun(const ArgWord &) {
    emit_nyi("i_call_fun");
}

void BeamModuleAssembler::emit_i_call_fun2(const ArgVal &,
                                           const ArgWord &,
                                           const ArgRegister &) {
    emit_nyi("i_call_fun2");
}

void BeamModuleAssembler::emit_i_call_fun2_last(const ArgVal &,
                                                const ArgWord &,
                                                const ArgRegister &,
                                                const ArgWord &) {
    emit_nyi("i_call_fun2_last");
}

void BeamModuleAssembler::emit_i_call_fun_last(const ArgWord &,
                                               const ArgWord &) {
    emit_nyi("i_call_fun_last");
}

void BeamModuleAssembler::emit_i_call_last(const ArgLabel &, const ArgWord &) {
    emit_nyi("i_call_last");
}

void BeamModuleAssembler::emit_i_call_on_load_function() {
    emit_nyi("i_call_on_load_function");
}

void BeamModuleAssembler::emit_i_call_only(const ArgLabel &) {
    emit_nyi("i_call_only");
}

void BeamModuleAssembler::emit_i_call_trace_return() {
    emit_nyi("i_call_trace_return");
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

void BeamModuleAssembler::emit_i_ctz(const ArgLabel &,
                                     const ArgSource &,
                                     const ArgRegister &) {
    emit_nyi("i_ctz");
}

void BeamModuleAssembler::emit_i_debug_line(const ArgWord &,
                                            const ArgWord &,
                                            const ArgWord &) {
    emit_nyi("i_debug_line");
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

void BeamModuleAssembler::emit_i_get(const ArgSource &, const ArgRegister &) {
    emit_nyi("i_get");
}

void BeamModuleAssembler::emit_i_get_hash(const ArgConstant &,
                                          const ArgWord &,
                                          const ArgRegister &) {
    emit_nyi("i_get_hash");
}

void BeamModuleAssembler::emit_i_get_local_record_elements(
        const ArgLiteral &,
        const ArgRegister &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_get_local_record_elements");
}

void BeamModuleAssembler::emit_i_get_map_element(const ArgLabel &,
                                                 const ArgRegister &,
                                                 const ArgRegister &,
                                                 const ArgRegister &) {
    emit_nyi("i_get_map_element");
}

void BeamModuleAssembler::emit_i_get_map_element_hash(const ArgLabel &,
                                                      const ArgRegister &,
                                                      const ArgConstant &,
                                                      const ArgWord &,
                                                      const ArgRegister &) {
    emit_nyi("i_get_map_element_hash");
}

void BeamModuleAssembler::emit_i_get_map_elements(const ArgLabel &,
                                                  const ArgSource &,
                                                  const ArgWord &,
                                                  const Span<const ArgVal> &) {
    emit_nyi("i_get_map_elements");
}

void BeamModuleAssembler::emit_i_get_record_elements(
        const ArgLabel &,
        const ArgRegister &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_get_record_elements");
}

void BeamModuleAssembler::emit_i_get_tuple_element(const ArgSource &,
                                                   const ArgWord &,
                                                   const ArgRegister &) {
    emit_nyi("i_get_tuple_element");
}

void BeamModuleAssembler::emit_i_hibernate() {
    emit_nyi("i_hibernate");
}

void BeamModuleAssembler::emit_i_int_div(const ArgLabel &,
                                         const ArgWord &,
                                         const ArgSource &,
                                         const ArgSource &,
                                         const ArgRegister &) {
    emit_nyi("i_int_div");
}

void BeamModuleAssembler::emit_i_is_tagged_tuple(const ArgLabel &,
                                                 const ArgSource &,
                                                 const ArgWord &,
                                                 const ArgAtom &) {
    emit_nyi("i_is_tagged_tuple");
}

void BeamModuleAssembler::emit_i_is_tagged_tuple_ff(const ArgLabel &,
                                                    const ArgLabel &,
                                                    const ArgSource &,
                                                    const ArgWord &,
                                                    const ArgAtom &) {
    emit_nyi("i_is_tagged_tuple_ff");
}

void BeamModuleAssembler::emit_i_is_tuple(const ArgLabel &, const ArgSource &) {
    emit_nyi("i_is_tuple");
}

void BeamModuleAssembler::emit_i_is_tuple_of_arity(const ArgLabel &,
                                                   const ArgSource &,
                                                   const ArgWord &) {
    emit_nyi("i_is_tuple_of_arity");
}

void BeamModuleAssembler::emit_i_is_tuple_of_arity_ff(const ArgLabel &,
                                                      const ArgLabel &,
                                                      const ArgSource &,
                                                      const ArgWord &) {
    emit_nyi("i_is_tuple_of_arity_ff");
}

void BeamModuleAssembler::emit_i_jump_on_val(const ArgSource &,
                                             const ArgVal &,
                                             const ArgWord &,
                                             const ArgWord &,
                                             const Span<const ArgVal> &) {
    emit_nyi("i_jump_on_val");
}

void BeamModuleAssembler::emit_i_lambda_error(const ArgWord &) {
    emit_nyi("i_lambda_error");
}

void BeamModuleAssembler::emit_i_lambda_trampoline(const ArgLambda &Lambda,
                                                   const ArgLabel &,
                                                   const ArgWord &,
                                                   const ArgWord &) {
    a.bind(lambdas[Lambda.get()].trampoline);
    emit_nyi("i_lambda_trampoline");
}

void BeamModuleAssembler::emit_i_length(const ArgLabel &,
                                        const ArgWord &,
                                        const ArgRegister &) {
    emit_nyi("i_length");
}

void BeamModuleAssembler::emit_i_length_setup(const ArgLabel &,
                                              const ArgWord &,
                                              const ArgSource &) {
    emit_nyi("i_length_setup");
}

void BeamModuleAssembler::emit_i_line_breakpoint_trampoline() {
    emit_nyi("i_line_breakpoint_trampoline");
}

void BeamModuleAssembler::emit_i_load_nif() {
    emit_nyi("i_load_nif");
}

void BeamModuleAssembler::emit_i_loop_rec(const ArgLabel &) {
    emit_nyi("i_loop_rec");
}

void BeamModuleAssembler::emit_i_m_div(const ArgLabel &,
                                       const ArgWord &,
                                       const ArgSource &,
                                       const ArgSource &,
                                       const ArgRegister &) {
    emit_nyi("i_m_div");
}

void BeamModuleAssembler::emit_i_make_fun3(const ArgLambda &,
                                           const ArgRegister &,
                                           const ArgWord &,
                                           const ArgWord &,
                                           const Span<const ArgVal> &) {
    emit_nyi("i_make_fun3");
}

void BeamModuleAssembler::emit_i_minus(const ArgLabel &,
                                       const ArgWord &,
                                       const ArgSource &,
                                       const ArgSource &,
                                       const ArgRegister &) {
    emit_nyi("i_minus");
}

void BeamModuleAssembler::emit_i_move(const ArgSource &, const ArgRegister &) {
    emit_nyi("i_move");
}

void BeamModuleAssembler::emit_i_mul_add(const ArgLabel &,
                                         const ArgSource &,
                                         const ArgSource &,
                                         const ArgSource &,
                                         const ArgVal &,
                                         const ArgRegister &) {
    emit_nyi("i_mul_add");
}

void BeamModuleAssembler::emit_i_new_small_map_lit(const ArgRegister &,
                                                   const ArgWord &,
                                                   const ArgLiteral &,
                                                   const ArgWord &,
                                                   const Span<const ArgVal> &) {
    emit_nyi("i_new_small_map_lit");
}

void BeamModuleAssembler::emit_i_perf_counter() {
    emit_nyi("i_perf_counter");
}

void BeamModuleAssembler::emit_i_plus(const ArgLabel &,
                                      const ArgWord &,
                                      const ArgSource &,
                                      const ArgSource &,
                                      const ArgRegister &) {
    emit_nyi("i_plus");
}

void BeamModuleAssembler::emit_i_popcount(const ArgLabel &,
                                          const ArgSource &,
                                          const ArgRegister &) {
    emit_nyi("i_popcount");
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

void BeamModuleAssembler::emit_i_return_to_trace() {
    emit_nyi("i_return_to_trace");
}

void BeamModuleAssembler::emit_i_select_tuple_arity(
        const ArgRegister &,
        const ArgLabel &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("i_select_tuple_arity");
}

void BeamModuleAssembler::emit_i_select_val_bins(const ArgSource &,
                                                 const ArgVal &,
                                                 const ArgWord &,
                                                 const Span<const ArgVal> &) {
    emit_nyi("i_select_val_bins");
}

void BeamModuleAssembler::emit_i_select_val_lins(const ArgSource &,
                                                 const ArgVal &,
                                                 const ArgWord &,
                                                 const Span<const ArgVal> &) {
    emit_nyi("i_select_val_lins");
}

void BeamModuleAssembler::emit_i_signed_width(const ArgLabel &,
                                              const ArgSource &,
                                              const ArgRegister &) {
    emit_nyi("i_signed_width");
}

void BeamModuleAssembler::emit_i_test_arity(const ArgLabel &,
                                            const ArgSource &,
                                            const ArgWord &) {
    emit_nyi("i_test_arity");
}

void BeamModuleAssembler::emit_i_test_yield() {
    emit_nyi("i_test_yield");
}

void BeamModuleAssembler::emit_i_unary_minus(const ArgLabel &,
                                             const ArgWord &,
                                             const ArgSource &,
                                             const ArgRegister &) {
    emit_nyi("i_unary_minus");
}

void BeamModuleAssembler::emit_i_unsigned_width(const ArgLabel &,
                                                const ArgSource &,
                                                const ArgRegister &) {
    emit_nyi("i_unsigned_width");
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

void BeamModuleAssembler::emit_i_validate(const ArgWord &) {
    emit_nyi("i_validate");
}

void BeamModuleAssembler::emit_i_yield() {
    emit_nyi("i_yield");
}

void BeamModuleAssembler::emit_if_end() {
    emit_nyi("if_end");
}

void BeamModuleAssembler::emit_init_yregs(const ArgWord &,
                                          const Span<const ArgVal> &) {
    emit_nyi("init_yregs");
}

void BeamModuleAssembler::emit_is_any_native_record(const ArgLabel &,
                                                    const ArgRegister &) {
    emit_nyi("is_any_native_record");
}

void BeamModuleAssembler::emit_is_atom(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_atom");
}

void BeamModuleAssembler::emit_is_binary(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_binary");
}

void BeamModuleAssembler::emit_is_bitstring(const ArgLabel &,
                                            const ArgSource &) {
    emit_nyi("is_bitstring");
}

void BeamModuleAssembler::emit_is_boolean(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_boolean");
}

void BeamModuleAssembler::emit_is_eq(const ArgLabel &,
                                     const ArgSource &,
                                     const ArgSource &) {
    emit_nyi("is_eq");
}

void BeamModuleAssembler::emit_is_eq_exact(const ArgLabel &,
                                           const ArgSource &,
                                           const ArgSource &) {
    emit_nyi("is_eq_exact");
}

void BeamModuleAssembler::emit_is_float(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_float");
}

void BeamModuleAssembler::emit_is_function(const ArgLabel &,
                                           const ArgRegister &) {
    emit_nyi("is_function");
}

void BeamModuleAssembler::emit_is_function2(const ArgLabel &,
                                            const ArgSource &,
                                            const ArgSource &) {
    emit_nyi("is_function2");
}

void BeamModuleAssembler::emit_is_ge(const ArgLabel &,
                                     const ArgSource &,
                                     const ArgSource &) {
    emit_nyi("is_ge");
}

void BeamModuleAssembler::emit_is_ge_ge(const ArgLabel &,
                                        const ArgLabel &,
                                        const ArgRegister &,
                                        const ArgConstant &,
                                        const ArgConstant &) {
    emit_nyi("is_ge_ge");
}

void BeamModuleAssembler::emit_is_ge_lt(const ArgLabel &,
                                        const ArgLabel &,
                                        const ArgRegister &,
                                        const ArgConstant &,
                                        const ArgConstant &) {
    emit_nyi("is_ge_lt");
}

void BeamModuleAssembler::emit_is_in_range(const ArgLabel &,
                                           const ArgLabel &,
                                           const ArgRegister &,
                                           const ArgConstant &,
                                           const ArgConstant &) {
    emit_nyi("is_in_range");
}

void BeamModuleAssembler::emit_is_int_ge(const ArgLabel &,
                                         const ArgRegister &,
                                         const ArgConstant &) {
    emit_nyi("is_int_ge");
}

void BeamModuleAssembler::emit_is_int_in_range(const ArgLabel &,
                                               const ArgRegister &,
                                               const ArgConstant &,
                                               const ArgConstant &) {
    emit_nyi("is_int_in_range");
}

void BeamModuleAssembler::emit_is_integer(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_integer");
}

void BeamModuleAssembler::emit_is_list(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_list");
}

void BeamModuleAssembler::emit_is_lt(const ArgLabel &,
                                     const ArgSource &,
                                     const ArgSource &) {
    emit_nyi("is_lt");
}

void BeamModuleAssembler::emit_is_map(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_map");
}

void BeamModuleAssembler::emit_is_native_record(const ArgLabel &,
                                                const ArgRegister &,
                                                const ArgAtom &,
                                                const ArgAtom &) {
    emit_nyi("is_native_record");
}

void BeamModuleAssembler::emit_is_ne(const ArgLabel &,
                                     const ArgSource &,
                                     const ArgSource &) {
    emit_nyi("is_ne");
}

void BeamModuleAssembler::emit_is_ne_exact(const ArgLabel &,
                                           const ArgSource &,
                                           const ArgSource &) {
    emit_nyi("is_ne_exact");
}

void BeamModuleAssembler::emit_is_nil(const ArgLabel &, const ArgRegister &) {
    emit_nyi("is_nil");
}

void BeamModuleAssembler::emit_is_nonempty_list(const ArgLabel &,
                                                const ArgRegister &) {
    emit_nyi("is_nonempty_list");
}

void BeamModuleAssembler::emit_is_number(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_number");
}

void BeamModuleAssembler::emit_is_pid(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_pid");
}

void BeamModuleAssembler::emit_is_port(const ArgLabel &, const ArgSource &) {
    emit_nyi("is_port");
}

void BeamModuleAssembler::emit_is_record_accessible(const ArgLabel &,
                                                    const ArgRegister &,
                                                    const ArgAtom &) {
    emit_nyi("is_record_accessible");
}

void BeamModuleAssembler::emit_is_reference(const ArgLabel &,
                                            const ArgSource &) {
    emit_nyi("is_reference");
}

void BeamModuleAssembler::emit_jump(const ArgLabel &) {
    emit_nyi("jump");
}

void BeamModuleAssembler::emit_load_tuple_ptr(const ArgSource &) {
    emit_nyi("load_tuple_ptr");
}

void BeamModuleAssembler::emit_load_two_xregs(const ArgRegister &,
                                              const ArgXRegister &,
                                              const ArgRegister &,
                                              const ArgXRegister &) {
    emit_nyi("load_two_xregs");
}

void BeamModuleAssembler::emit_loop_rec_end(const ArgLabel &) {
    emit_nyi("loop_rec_end");
}

void BeamModuleAssembler::emit_mark_unreachable() {
    emit_nyi("mark_unreachable");
}

void BeamModuleAssembler::emit_move_call_ext_last(const ArgYRegister &,
                                                  const ArgRegister &,
                                                  const ArgExport &,
                                                  const ArgWord &) {
    emit_nyi("move_call_ext_last");
}

void BeamModuleAssembler::emit_move_call_last(const ArgYRegister &,
                                              const ArgRegister &,
                                              const ArgLabel &,
                                              const ArgWord &) {
    emit_nyi("move_call_last");
}

void BeamModuleAssembler::emit_move_deallocate_return() {
    emit_nyi("move_deallocate_return");
}

void BeamModuleAssembler::emit_move_trim(const ArgSource &,
                                         const ArgRegister &,
                                         const ArgWord &) {
    emit_nyi("move_trim");
}

void BeamModuleAssembler::emit_move_two_trim(const ArgYRegister &,
                                             const ArgRegister &,
                                             const ArgYRegister &,
                                             const ArgRegister &,
                                             const ArgWord &) {
    emit_nyi("move_two_trim");
}

void BeamModuleAssembler::emit_new_map(const ArgRegister &,
                                       const ArgWord &,
                                       const ArgWord &,
                                       const Span<const ArgVal> &) {
    emit_nyi("new_map");
}

void BeamModuleAssembler::emit_nif_start() {
    emit_nyi("nif_start");
}

void BeamModuleAssembler::emit_node(const ArgRegister &) {
    emit_nyi("node");
}

void BeamModuleAssembler::emit_nofail_bif1(const ArgSource &,
                                           const ArgWord &,
                                           const ArgRegister &) {
    emit_nyi("nofail_bif1");
}

void BeamModuleAssembler::emit_nofail_bif2(const ArgSource &,
                                           const ArgSource &,
                                           const ArgWord &,
                                           const ArgRegister &) {
    emit_nyi("nofail_bif2");
}

void BeamModuleAssembler::emit_normal_exit() {
    emit_nyi("normal_exit");
}

void BeamModuleAssembler::emit_put_list(const ArgSource &,
                                        const ArgSource &,
                                        const ArgRegister &) {
    emit_nyi("put_list");
}

void BeamModuleAssembler::emit_put_list2(const ArgSource &,
                                         const ArgSource &,
                                         const ArgSource &,
                                         const ArgRegister &) {
    emit_nyi("put_list2");
}

void BeamModuleAssembler::emit_put_list_deallocate(const ArgSource &,
                                                   const ArgSource &,
                                                   const ArgRegister &,
                                                   const ArgWord &) {
    emit_nyi("put_list_deallocate");
}

void BeamModuleAssembler::emit_put_tuple2(const ArgRegister &,
                                          const ArgWord &,
                                          const Span<const ArgVal> &) {
    emit_nyi("put_tuple2");
}

void BeamModuleAssembler::emit_raise(const ArgSource &, const ArgSource &) {
    emit_nyi("raise");
}

void BeamModuleAssembler::emit_raw_raise() {
    emit_nyi("raw_raise");
}

void BeamModuleAssembler::emit_recv_marker_bind(const ArgRegister &,
                                                const ArgRegister &) {
    emit_nyi("recv_marker_bind");
}

void BeamModuleAssembler::emit_recv_marker_clear(const ArgRegister &) {
    emit_nyi("recv_marker_clear");
}

void BeamModuleAssembler::emit_recv_marker_reserve(const ArgRegister &) {
    emit_nyi("recv_marker_reserve");
}

void BeamModuleAssembler::emit_recv_marker_use(const ArgRegister &) {
    emit_nyi("recv_marker_use");
}

void BeamModuleAssembler::emit_remove_message() {
    emit_nyi("remove_message");
}

void BeamModuleAssembler::emit_return() {
    emit_nyi("return");
}

void BeamModuleAssembler::emit_return_trace() {
    emit_nyi("return_trace");
}

void BeamModuleAssembler::emit_self(const ArgRegister &) {
    emit_nyi("self");
}

void BeamModuleAssembler::emit_send() {
    emit_nyi("send");
}

void BeamModuleAssembler::emit_set_tuple_element(const ArgSource &,
                                                 const ArgRegister &,
                                                 const ArgWord &) {
    emit_nyi("set_tuple_element");
}

void BeamModuleAssembler::emit_store_two_values(const ArgSource &,
                                                const ArgRegister &,
                                                const ArgSource &,
                                                const ArgRegister &) {
    emit_nyi("store_two_values");
}

void BeamModuleAssembler::emit_swap(const ArgRegister &, const ArgRegister &) {
    emit_nyi("swap");
}

void BeamModuleAssembler::emit_swap2(const ArgRegister &,
                                     const ArgRegister &,
                                     const ArgRegister &) {
    emit_nyi("swap2");
}

void BeamModuleAssembler::emit_swap3(const ArgRegister &,
                                     const ArgRegister &,
                                     const ArgRegister &,
                                     const ArgRegister &) {
    emit_nyi("swap3");
}

void BeamModuleAssembler::emit_swap4(const ArgRegister &,
                                     const ArgRegister &,
                                     const ArgRegister &,
                                     const ArgRegister &,
                                     const ArgRegister &) {
    emit_nyi("swap4");
}

void BeamModuleAssembler::emit_system_limit_body() {
    emit_nyi("system_limit_body");
}

void BeamModuleAssembler::emit_test_heap(const ArgWord &, const ArgWord &) {
    emit_nyi("test_heap");
}

void BeamModuleAssembler::emit_timeout() {
    emit_nyi("timeout");
}

void BeamModuleAssembler::emit_timeout_locked() {
    emit_nyi("timeout_locked");
}

void BeamModuleAssembler::emit_trim(const ArgWord &, const ArgWord &) {
    emit_nyi("trim");
}

void BeamModuleAssembler::emit_try_case(const ArgYRegister &) {
    emit_nyi("try_case");
}

void BeamModuleAssembler::emit_try_case_end(const ArgSource &) {
    emit_nyi("try_case_end");
}

void BeamModuleAssembler::emit_try_end(const ArgYRegister &) {
    emit_nyi("try_end");
}

void BeamModuleAssembler::emit_try_end_deallocate(const ArgWord &) {
    emit_nyi("try_end_deallocate");
}

void BeamModuleAssembler::emit_try_end_move_deallocate(const ArgSource &,
                                                       const ArgRegister &,
                                                       const ArgWord &) {
    emit_nyi("try_end_move_deallocate");
}

void BeamModuleAssembler::emit_update_map_assoc(const ArgSource &,
                                                const ArgRegister &,
                                                const ArgWord &,
                                                const ArgWord &,
                                                const Span<const ArgVal> &) {
    emit_nyi("update_map_assoc");
}

void BeamModuleAssembler::emit_update_map_exact(const ArgSource &,
                                                const ArgLabel &,
                                                const ArgRegister &,
                                                const ArgWord &,
                                                const ArgWord &,
                                                const Span<const ArgVal> &) {
    emit_nyi("update_map_exact");
}

void BeamModuleAssembler::emit_update_record(const ArgAtom &,
                                             const ArgWord &,
                                             const ArgSource &,
                                             const ArgRegister &,
                                             const ArgWord &,
                                             const Span<const ArgVal> &) {
    emit_nyi("update_record");
}

void BeamModuleAssembler::emit_update_record_in_place(
        const ArgWord &,
        const ArgSource &,
        const ArgRegister &,
        const ArgWord &,
        const Span<const ArgVal> &) {
    emit_nyi("update_record_in_place");
}

void BeamModuleAssembler::emit_wait_locked(const ArgLabel &) {
    emit_nyi("wait_locked");
}

void BeamModuleAssembler::emit_wait_timeout_locked(const ArgSource &,
                                                   const ArgLabel &) {
    emit_nyi("wait_timeout_locked");
}

void BeamModuleAssembler::emit_wait_timeout_unlocked(const ArgSource &,
                                                     const ArgLabel &) {
    emit_nyi("wait_timeout_unlocked");
}

void BeamModuleAssembler::emit_wait_unlocked(const ArgLabel &) {
    emit_nyi("wait_unlocked");
}
