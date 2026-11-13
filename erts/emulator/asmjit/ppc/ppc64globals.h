// This file is part of AsmJit project <https://asmjit.com>
//
// See <asmjit/core.h> or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef ASMJIT_PPC_PPC64GLOBALS_H_INCLUDED
#define ASMJIT_PPC_PPC64GLOBALS_H_INCLUDED

#include <asmjit/core/globals.h>
#include <asmjit/core/inst.h>

//! \namespace asmjit::ppc
//! \ingroup asmjit_ppc64
//!
//! PPC64 backend.

ASMJIT_BEGIN_SUB_NAMESPACE(ppc)

//! \addtogroup asmjit_ppc64
//! \{

//! PPC64 instruction.
//!
//! \note Only used to hold PPC-specific enumerations and static functions.
namespace Inst {
  //! Instruction id.
  //!
  //! \note The order must match the instruction list in `ppc64instdb.cpp`.
  enum Id : uint32_t {
    kIdNone = 0,                         //!< Instruction ''.
    kIdAdd,                              //!< Instruction 'add'.
    kIdAdd_rc,                           //!< Instruction 'add.'.
    kIdAddc,                             //!< Instruction 'addc'.
    kIdAddc_rc,                          //!< Instruction 'addc.'.
    kIdAddco,                            //!< Instruction 'addco'.
    kIdAddco_rc,                         //!< Instruction 'addco.'.
    kIdAdde,                             //!< Instruction 'adde'.
    kIdAdde_rc,                          //!< Instruction 'adde.'.
    kIdAddeo,                            //!< Instruction 'addeo'.
    kIdAddeo_rc,                         //!< Instruction 'addeo.'.
    kIdAddi,                             //!< Instruction 'addi'.
    kIdAddic,                            //!< Instruction 'addic'.
    kIdAddic_rc,                         //!< Instruction 'addic.'.
    kIdAddis,                            //!< Instruction 'addis'.
    kIdAddme,                            //!< Instruction 'addme'.
    kIdAddme_rc,                         //!< Instruction 'addme.'.
    kIdAddmeo,                           //!< Instruction 'addmeo'.
    kIdAddmeo_rc,                        //!< Instruction 'addmeo.'.
    kIdAddo,                             //!< Instruction 'addo'.
    kIdAddo_rc,                          //!< Instruction 'addo.'.
    kIdAddpcis,                          //!< Instruction 'addpcis'.
    kIdAddze,                            //!< Instruction 'addze'.
    kIdAddze_rc,                         //!< Instruction 'addze.'.
    kIdAddzeo,                           //!< Instruction 'addzeo'.
    kIdAddzeo_rc,                        //!< Instruction 'addzeo.'.
    kIdAnd,                              //!< Instruction 'and'.
    kIdAnd_rc,                           //!< Instruction 'and.'.
    kIdAndc,                             //!< Instruction 'andc'.
    kIdAndc_rc,                          //!< Instruction 'andc.'.
    kIdAndi_rc,                          //!< Instruction 'andi.'.
    kIdAndis_rc,                         //!< Instruction 'andis.'.
    kIdB,                                //!< Instruction 'b'.
    kIdBc,                               //!< Instruction 'bc'.
    kIdBcctr,                            //!< Instruction 'bcctr'.
    kIdBcctrl,                           //!< Instruction 'bcctrl'.
    kIdBcl,                              //!< Instruction 'bcl'.
    kIdBclr,                             //!< Instruction 'bclr'.
    kIdBclrl,                            //!< Instruction 'bclrl'.
    kIdBl,                               //!< Instruction 'bl'.
    kIdCmp,                              //!< Instruction 'cmp'.
    kIdCmpi,                             //!< Instruction 'cmpi'.
    kIdCmpl,                             //!< Instruction 'cmpl'.
    kIdCmpli,                            //!< Instruction 'cmpli'.
    kIdCntlzd,                           //!< Instruction 'cntlzd'.
    kIdCntlzd_rc,                        //!< Instruction 'cntlzd.'.
    kIdCntlzw,                           //!< Instruction 'cntlzw'.
    kIdCntlzw_rc,                        //!< Instruction 'cntlzw.'.
    kIdCnttzd,                           //!< Instruction 'cnttzd'.
    kIdCnttzd_rc,                        //!< Instruction 'cnttzd.'.
    kIdCnttzw,                           //!< Instruction 'cnttzw'.
    kIdCnttzw_rc,                        //!< Instruction 'cnttzw.'.
    kIdCrand,                            //!< Instruction 'crand'.
    kIdCrandc,                           //!< Instruction 'crandc'.
    kIdCreqv,                            //!< Instruction 'creqv'.
    kIdCrnand,                           //!< Instruction 'crnand'.
    kIdCrnor,                            //!< Instruction 'crnor'.
    kIdCror,                             //!< Instruction 'cror'.
    kIdCrorc,                            //!< Instruction 'crorc'.
    kIdCrxor,                            //!< Instruction 'crxor'.
    kIdDivd,                             //!< Instruction 'divd'.
    kIdDivd_rc,                          //!< Instruction 'divd.'.
    kIdDivdo,                            //!< Instruction 'divdo'.
    kIdDivdo_rc,                         //!< Instruction 'divdo.'.
    kIdDivdu,                            //!< Instruction 'divdu'.
    kIdDivdu_rc,                         //!< Instruction 'divdu.'.
    kIdDivduo,                           //!< Instruction 'divduo'.
    kIdDivduo_rc,                        //!< Instruction 'divduo.'.
    kIdDivw,                             //!< Instruction 'divw'.
    kIdDivw_rc,                          //!< Instruction 'divw.'.
    kIdDivwo,                            //!< Instruction 'divwo'.
    kIdDivwo_rc,                         //!< Instruction 'divwo.'.
    kIdDivwu,                            //!< Instruction 'divwu'.
    kIdDivwu_rc,                         //!< Instruction 'divwu.'.
    kIdDivwuo,                           //!< Instruction 'divwuo'.
    kIdDivwuo_rc,                        //!< Instruction 'divwuo.'.
    kIdEqv,                              //!< Instruction 'eqv'.
    kIdEqv_rc,                           //!< Instruction 'eqv.'.
    kIdExtsb,                            //!< Instruction 'extsb'.
    kIdExtsb_rc,                         //!< Instruction 'extsb.'.
    kIdExtsh,                            //!< Instruction 'extsh'.
    kIdExtsh_rc,                         //!< Instruction 'extsh.'.
    kIdExtsw,                            //!< Instruction 'extsw'.
    kIdExtsw_rc,                         //!< Instruction 'extsw.'.
    kIdIsel,                             //!< Instruction 'isel'.
    kIdLbarx,                            //!< Instruction 'lbarx'.
    kIdLbz,                              //!< Instruction 'lbz'.
    kIdLbzu,                             //!< Instruction 'lbzu'.
    kIdLbzux,                            //!< Instruction 'lbzux'.
    kIdLbzx,                             //!< Instruction 'lbzx'.
    kIdLd,                               //!< Instruction 'ld'.
    kIdLdarx,                            //!< Instruction 'ldarx'.
    kIdLdbrx,                            //!< Instruction 'ldbrx'.
    kIdLdu,                              //!< Instruction 'ldu'.
    kIdLdux,                             //!< Instruction 'ldux'.
    kIdLdx,                              //!< Instruction 'ldx'.
    kIdLfd,                              //!< Instruction 'lfd'.
    kIdLfdx,                             //!< Instruction 'lfdx'.
    kIdLfs,                              //!< Instruction 'lfs'.
    kIdLfsx,                             //!< Instruction 'lfsx'.
    kIdLha,                              //!< Instruction 'lha'.
    kIdLharx,                            //!< Instruction 'lharx'.
    kIdLhau,                             //!< Instruction 'lhau'.
    kIdLhaux,                            //!< Instruction 'lhaux'.
    kIdLhax,                             //!< Instruction 'lhax'.
    kIdLhbrx,                            //!< Instruction 'lhbrx'.
    kIdLhz,                              //!< Instruction 'lhz'.
    kIdLhzu,                             //!< Instruction 'lhzu'.
    kIdLhzux,                            //!< Instruction 'lhzux'.
    kIdLhzx,                             //!< Instruction 'lhzx'.
    kIdLi,                               //!< Instruction 'li'.
    kIdLis,                              //!< Instruction 'lis'.
    kIdLwa,                              //!< Instruction 'lwa'.
    kIdLwarx,                            //!< Instruction 'lwarx'.
    kIdLwaux,                            //!< Instruction 'lwaux'.
    kIdLwax,                             //!< Instruction 'lwax'.
    kIdLwbrx,                            //!< Instruction 'lwbrx'.
    kIdLwz,                              //!< Instruction 'lwz'.
    kIdLwzu,                             //!< Instruction 'lwzu'.
    kIdLwzux,                            //!< Instruction 'lwzux'.
    kIdLwzx,                             //!< Instruction 'lwzx'.
    kIdMcrf,                             //!< Instruction 'mcrf'.
    kIdMcrxrx,                           //!< Instruction 'mcrxrx'.
    kIdMfcr,                             //!< Instruction 'mfcr'.
    kIdMfspr,                            //!< Instruction 'mfspr'.
    kIdModsd,                            //!< Instruction 'modsd'.
    kIdModsw,                            //!< Instruction 'modsw'.
    kIdModud,                            //!< Instruction 'modud'.
    kIdModuw,                            //!< Instruction 'moduw'.
    kIdMtspr,                            //!< Instruction 'mtspr'.
    kIdMulhd,                            //!< Instruction 'mulhd'.
    kIdMulhd_rc,                         //!< Instruction 'mulhd.'.
    kIdMulhdu,                           //!< Instruction 'mulhdu'.
    kIdMulhdu_rc,                        //!< Instruction 'mulhdu.'.
    kIdMulhw,                            //!< Instruction 'mulhw'.
    kIdMulhw_rc,                         //!< Instruction 'mulhw.'.
    kIdMulhwu,                           //!< Instruction 'mulhwu'.
    kIdMulhwu_rc,                        //!< Instruction 'mulhwu.'.
    kIdMulld,                            //!< Instruction 'mulld'.
    kIdMulld_rc,                         //!< Instruction 'mulld.'.
    kIdMulldo,                           //!< Instruction 'mulldo'.
    kIdMulldo_rc,                        //!< Instruction 'mulldo.'.
    kIdMulli,                            //!< Instruction 'mulli'.
    kIdMullw,                            //!< Instruction 'mullw'.
    kIdMullw_rc,                         //!< Instruction 'mullw.'.
    kIdMullwo,                           //!< Instruction 'mullwo'.
    kIdMullwo_rc,                        //!< Instruction 'mullwo.'.
    kIdNand,                             //!< Instruction 'nand'.
    kIdNand_rc,                          //!< Instruction 'nand.'.
    kIdNeg,                              //!< Instruction 'neg'.
    kIdNeg_rc,                           //!< Instruction 'neg.'.
    kIdNego,                             //!< Instruction 'nego'.
    kIdNego_rc,                          //!< Instruction 'nego.'.
    kIdNor,                              //!< Instruction 'nor'.
    kIdNor_rc,                           //!< Instruction 'nor.'.
    kIdOr,                               //!< Instruction 'or'.
    kIdOr_rc,                            //!< Instruction 'or.'.
    kIdOrc,                              //!< Instruction 'orc'.
    kIdOrc_rc,                           //!< Instruction 'orc.'.
    kIdOri,                              //!< Instruction 'ori'.
    kIdOris,                             //!< Instruction 'oris'.
    kIdPopcntb,                          //!< Instruction 'popcntb'.
    kIdPopcntd,                          //!< Instruction 'popcntd'.
    kIdPopcntw,                          //!< Instruction 'popcntw'.
    kIdRldcl,                            //!< Instruction 'rldcl'.
    kIdRldcl_rc,                         //!< Instruction 'rldcl.'.
    kIdRldcr,                            //!< Instruction 'rldcr'.
    kIdRldcr_rc,                         //!< Instruction 'rldcr.'.
    kIdRldic,                            //!< Instruction 'rldic'.
    kIdRldic_rc,                         //!< Instruction 'rldic.'.
    kIdRldicl,                           //!< Instruction 'rldicl'.
    kIdRldicl_rc,                        //!< Instruction 'rldicl.'.
    kIdRldicr,                           //!< Instruction 'rldicr'.
    kIdRldicr_rc,                        //!< Instruction 'rldicr.'.
    kIdRldimi,                           //!< Instruction 'rldimi'.
    kIdRldimi_rc,                        //!< Instruction 'rldimi.'.
    kIdRlwimi,                           //!< Instruction 'rlwimi'.
    kIdRlwimi_rc,                        //!< Instruction 'rlwimi.'.
    kIdRlwinm,                           //!< Instruction 'rlwinm'.
    kIdRlwinm_rc,                        //!< Instruction 'rlwinm.'.
    kIdRlwnm,                            //!< Instruction 'rlwnm'.
    kIdRlwnm_rc,                         //!< Instruction 'rlwnm.'.
    kIdSetb,                             //!< Instruction 'setb'.
    kIdSld,                              //!< Instruction 'sld'.
    kIdSld_rc,                           //!< Instruction 'sld.'.
    kIdSlw,                              //!< Instruction 'slw'.
    kIdSlw_rc,                           //!< Instruction 'slw.'.
    kIdSrad,                             //!< Instruction 'srad'.
    kIdSrad_rc,                          //!< Instruction 'srad.'.
    kIdSradi,                            //!< Instruction 'sradi'.
    kIdSradi_rc,                         //!< Instruction 'sradi.'.
    kIdSraw,                             //!< Instruction 'sraw'.
    kIdSraw_rc,                          //!< Instruction 'sraw.'.
    kIdSrawi,                            //!< Instruction 'srawi'.
    kIdSrawi_rc,                         //!< Instruction 'srawi.'.
    kIdSrd,                              //!< Instruction 'srd'.
    kIdSrd_rc,                           //!< Instruction 'srd.'.
    kIdSrw,                              //!< Instruction 'srw'.
    kIdSrw_rc,                           //!< Instruction 'srw.'.
    kIdStb,                              //!< Instruction 'stb'.
    kIdStbcx_rc,                         //!< Instruction 'stbcx.'.
    kIdStbu,                             //!< Instruction 'stbu'.
    kIdStbux,                            //!< Instruction 'stbux'.
    kIdStbx,                             //!< Instruction 'stbx'.
    kIdStd,                              //!< Instruction 'std'.
    kIdStdbrx,                           //!< Instruction 'stdbrx'.
    kIdStdcx_rc,                         //!< Instruction 'stdcx.'.
    kIdStdu,                             //!< Instruction 'stdu'.
    kIdStdux,                            //!< Instruction 'stdux'.
    kIdStdx,                             //!< Instruction 'stdx'.
    kIdStfd,                             //!< Instruction 'stfd'.
    kIdStfdx,                            //!< Instruction 'stfdx'.
    kIdStfs,                             //!< Instruction 'stfs'.
    kIdStfsx,                            //!< Instruction 'stfsx'.
    kIdSth,                              //!< Instruction 'sth'.
    kIdSthbrx,                           //!< Instruction 'sthbrx'.
    kIdSthcx_rc,                         //!< Instruction 'sthcx.'.
    kIdSthu,                             //!< Instruction 'sthu'.
    kIdSthux,                            //!< Instruction 'sthux'.
    kIdSthx,                             //!< Instruction 'sthx'.
    kIdStw,                              //!< Instruction 'stw'.
    kIdStwbrx,                           //!< Instruction 'stwbrx'.
    kIdStwcx_rc,                         //!< Instruction 'stwcx.'.
    kIdStwu,                             //!< Instruction 'stwu'.
    kIdStwux,                            //!< Instruction 'stwux'.
    kIdStwx,                             //!< Instruction 'stwx'.
    kIdSubf,                             //!< Instruction 'subf'.
    kIdSubf_rc,                          //!< Instruction 'subf.'.
    kIdSubfc,                            //!< Instruction 'subfc'.
    kIdSubfc_rc,                         //!< Instruction 'subfc.'.
    kIdSubfco,                           //!< Instruction 'subfco'.
    kIdSubfco_rc,                        //!< Instruction 'subfco.'.
    kIdSubfe,                            //!< Instruction 'subfe'.
    kIdSubfe_rc,                         //!< Instruction 'subfe.'.
    kIdSubfeo,                           //!< Instruction 'subfeo'.
    kIdSubfeo_rc,                        //!< Instruction 'subfeo.'.
    kIdSubfic,                           //!< Instruction 'subfic'.
    kIdSubfme,                           //!< Instruction 'subfme'.
    kIdSubfme_rc,                        //!< Instruction 'subfme.'.
    kIdSubfmeo,                          //!< Instruction 'subfmeo'.
    kIdSubfmeo_rc,                       //!< Instruction 'subfmeo.'.
    kIdSubfo,                            //!< Instruction 'subfo'.
    kIdSubfo_rc,                         //!< Instruction 'subfo.'.
    kIdSubfze,                           //!< Instruction 'subfze'.
    kIdSubfze_rc,                        //!< Instruction 'subfze.'.
    kIdSubfzeo,                          //!< Instruction 'subfzeo'.
    kIdSubfzeo_rc,                       //!< Instruction 'subfzeo.'.
    kIdXor,                              //!< Instruction 'xor'.
    kIdXor_rc,                           //!< Instruction 'xor.'.
    kIdXori,                             //!< Instruction 'xori'.
    kIdXoris,                            //!< Instruction 'xoris'.
    _kIdCount
  };

  //! Tests whether the `inst_id` is defined (counts also Inst::kIdNone, which must be zero).
  [[nodiscard]]
  static ASMJIT_INLINE_NODEBUG bool is_defined_id(InstId inst_id) noexcept { return inst_id < _kIdCount; }
}

//! Special purpose register ids used by `mfspr` and `mtspr` instructions.
namespace SPR {
  //! Special purpose register id.
  enum Id : uint32_t {
    //! Fixed-point exception register.
    kXER = 1,
    //! Link register.
    kLR = 8,
    //! Count register.
    kCTR = 9
  };
}

//! \}

ASMJIT_END_SUB_NAMESPACE

#endif // ASMJIT_PPC_PPC64GLOBALS_H_INCLUDED
