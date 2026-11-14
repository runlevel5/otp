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

void BeamGlobalAssembler::emit_create_native_record_shared() {
    emit_nyi("create_native_record_shared");
}

void BeamGlobalAssembler::emit_i_func_info_shared() {
    emit_nyi("i_func_info_shared");
}

/*
 * Instructions.
 */

void BeamModuleAssembler::emit_get_record_field(const ArgLabel &,
                                                const ArgRegister &,
                                                const ArgConstant &,
                                                const ArgAtom &,
                                                const ArgRegister &) {
    emit_nyi("get_record_field");
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
