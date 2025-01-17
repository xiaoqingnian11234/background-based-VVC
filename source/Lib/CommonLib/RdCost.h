/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     RdCost.h
    \brief    RD cost computation classes (header)
*/

#ifndef __RDCOST__
#define __RDCOST__

#include "CommonDef.h"
#include "Mv.h"
#include "Unit.h"
#include "Buffer.h"
#include "Slice.h"
#include "RdCostWeightPrediction.h"
#include <math.h>

//! \ingroup CommonLib
//! \{

class DistParam;
class EncCfg;

// ====================================================================================================================
// Type definition
// ====================================================================================================================

// for function pointer
typedef Distortion (*FpDistFunc) (const DistParam&);

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// distortion parameter class
class DistParam
{
public:
  CPelBuf               org;
  CPelBuf               cur;
#if WCG_EXT
  CPelBuf               orgLuma;
#endif
  int                   step;
  FpDistFunc            distFunc;
  int                   bitDepth;

  bool                  useMR;
  bool                  applyWeight;     // whether weighted prediction is used or not
  bool                  isBiPred;
  bool                  isQtbt;

  const WPScalingParam *wpCur;           // weighted prediction scaling parameters for current ref
  ComponentID           compID;
  Distortion            maximumDistortionForEarlyExit; /// During cost calculations, if distortion exceeds this value, cost calculations may early-terminate.

  // (vertical) subsampling shift (for reducing complexity)
  // - 0 = no subsampling, 1 = even rows, 2 = every 4th, etc.
  Int                   subShift;

  DistParam() : org(), cur(), step( 1 ), bitDepth( 0 ), useMR( false ), applyWeight( false ), isBiPred( false ), wpCur( nullptr ), compID( MAX_NUM_COMPONENT ), maximumDistortionForEarlyExit( std::numeric_limits<Distortion>::max() ), subShift( 0 ) { }
};

/// RD cost computation class
class RdCost
{
private:
  // for distortion

  static FpDistFunc       m_afpDistortFunc[DF_TOTAL_FUNCTIONS]; // [eDFunc]
  CostMode                m_costMode;
  double                  m_distortionWeight[MAX_NUM_COMPONENT]; // only chroma values are used.
  double                  m_dLambda;
#if WCG_EXT
  double                  m_dLambda_unadjusted; // TODO: check is necessary
  double                  m_DistScaleUnadjusted;
  static double           m_lumaLevelToWeightPLUT[LUMA_LEVEL_TO_DQP_LUT_MAXSIZE];
#endif
  double                  m_DistScale;
  double                  m_dLambdaMotionSAD[2 /* 0=standard, 1=for transquant bypass when mixed-lossless cost evaluation enabled*/];

  // for motion cost
  Mv                      m_mvPredictor;
  double                  m_motionLambda;
  int                     m_iCostScale;

  bool                    m_useQtbt;

public:
  RdCost();
  virtual ~RdCost();

#if WCG_EXT
  Double        calcRdCost            ( uint64_t fracBits, Distortion distortion, bool useUnadjustedLambda = true );
#else
  Double        calcRdCost            ( uint64_t fracBits, Distortion distortion );
#endif

  Void          setDistortionWeight   ( const ComponentID compID, const Double distortionWeight ) { m_distortionWeight[compID] = distortionWeight; }
  Void          setLambda             ( Double dLambda, const BitDepths &bitDepths );
#if BLOCK_RDO
  Void          setLambdaRDO          (Double dLambda, const BitDepths &bitDepths, Double M);
#endif

#if WCG_EXT
  Double        getLambda( bool unadj = false )
                                      { return unadj ? m_dLambda_unadjusted : m_dLambda; }
#else
  Double        getLambda()           { return m_dLambda; }
#endif
  Double        getChromaWeight()     { return ((m_distortionWeight[COMPONENT_Cb] + m_distortionWeight[COMPONENT_Cr]) / 2.0); }

  Void          setCostMode(CostMode m) { m_costMode = m; }

  Void          setUseQtbt(bool b)    { m_useQtbt = b; }

  // Distortion Functions
  Void          init();
#ifdef TARGET_SIMD_X86
  Void          initRdCostX86();
  template <X86_VEXT vext>
  Void          _initRdCostX86();
#endif

  Void           setDistParam( DistParam &rcDP, const CPelBuf &org, const Pel* piRefY , Int iRefStride, Int bitDepth, ComponentID compID, Int subShiftMode = 0, Int step = 1, Bool useHadamard = false );
  Void           setDistParam( DistParam &rcDP, const CPelBuf &org, const CPelBuf &cur, Int bitDepth, ComponentID compID, Bool useHadamard = false );
  Void           setDistParam( DistParam &rcDP, const Pel* pOrg, const Pel* piRefY, Int iOrgStride, Int iRefStride, Int bitDepth, ComponentID compID, Int width, Int height, Int subShiftMode = 0, Int step = 1, Bool useHadamard = false );

  double         getMotionLambda          ( bool bIsTransquantBypass ) { return m_dLambdaMotionSAD[(bIsTransquantBypass && m_costMode==COST_MIXED_LOSSLESS_LOSSY_CODING)?1:0]; }
  Void           selectMotionLambda       ( bool bIsTransquantBypass ) { m_motionLambda = getMotionLambda( bIsTransquantBypass ); }
  Void           setPredictor             ( const Mv& rcMv )
  {
    m_mvPredictor = rcMv;
  }
  Void           setCostScale             ( Int iCostScale )           { m_iCostScale = iCostScale; }
#if HM_EQ_MOTION_COST
  Distortion     getCost                  ( UInt b )                   { return Distortion( (((UInt)floor(65536.0 * m_motionLambda)) * b) >> 16 ); }
#else
  Distortion     getCost                  ( UInt b )                   { return Distortion( m_motionLambda * b ); }
#endif

#if ENABLE_SPLIT_PARALLELISM
  void copyState( const RdCost& other );
#endif

  // for motion cost
  static UInt    xGetExpGolombNumberOfBits( Int iVal )
  {
    CHECKD( iVal == std::numeric_limits<int>::min(), "Wrong value" );
    unsigned uiLength2 = 1, uiTemp2 = ( iVal <= 0 ) ? ( unsigned( -iVal ) << 1 ) + 1 : unsigned( iVal << 1 );

    while( uiTemp2 > MAX_CU_SIZE )
    {
      uiLength2 += ( MAX_CU_DEPTH << 1 );
      uiTemp2  >>=   MAX_CU_DEPTH;
    }

    return uiLength2 + ( g_aucPrevLog2[uiTemp2] << 1 );
  }
  Distortion     getCostOfVectorWithPredictor( const Int x, const Int y )  { return Distortion( m_motionLambda * getBitsOfVectorWithPredictor(x, y )); }
  UInt           getBitsOfVectorWithPredictor( const Int x, const Int y )  { return xGetExpGolombNumberOfBits(((x << m_iCostScale) - m_mvPredictor.getHor())) + xGetExpGolombNumberOfBits(((y << m_iCostScale) - m_mvPredictor.getVer())); }
#if WCG_EXT
         Void    saveUnadjustedLambda       ();
         Void    initLumaLevelToWeightTable ();
  inline Double  getWPSNRLumaLevelWeight    (Int val) { return m_lumaLevelToWeightPLUT[val]; }
#endif

private:

  static Distortion xGetSSE           ( const DistParam& pcDtParam );
  static Distortion xGetSSE4          ( const DistParam& pcDtParam );
  static Distortion xGetSSE8          ( const DistParam& pcDtParam );
  static Distortion xGetSSE16         ( const DistParam& pcDtParam );
  static Distortion xGetSSE32         ( const DistParam& pcDtParam );
  static Distortion xGetSSE64         ( const DistParam& pcDtParam );
  static Distortion xGetSSE16N        ( const DistParam& pcDtParam );

#if WCG_EXT
  static Distortion getWeightedMSE    (Int compIdx, const Pel org, const Pel cur, const UInt uiShift, const Pel orgLuma);
  static Distortion xGetSSE_WTD       ( const DistParam& pcDtParam );
  static Distortion xGetSSE2_WTD      ( const DistParam& pcDtParam );
  static Distortion xGetSSE4_WTD      ( const DistParam& pcDtParam );
  static Distortion xGetSSE8_WTD      ( const DistParam& pcDtParam );
  static Distortion xGetSSE16_WTD     ( const DistParam& pcDtParam );
  static Distortion xGetSSE32_WTD     ( const DistParam& pcDtParam );
  static Distortion xGetSSE64_WTD     ( const DistParam& pcDtParam );
  static Distortion xGetSSE16N_WTD    ( const DistParam& pcDtParam );
#endif

  static Distortion xGetSAD           ( const DistParam& pcDtParam );
  static Distortion xGetSAD4          ( const DistParam& pcDtParam );
  static Distortion xGetSAD8          ( const DistParam& pcDtParam );
  static Distortion xGetSAD16         ( const DistParam& pcDtParam );
  static Distortion xGetSAD32         ( const DistParam& pcDtParam );
  static Distortion xGetSAD64         ( const DistParam& pcDtParam );
  static Distortion xGetSAD16N        ( const DistParam& pcDtParam );

  static Distortion xGetSAD12         ( const DistParam& pcDtParam );
  static Distortion xGetSAD24         ( const DistParam& pcDtParam );
  static Distortion xGetSAD48         ( const DistParam& pcDtParam );

  static Distortion xGetSAD_full      ( const DistParam& pcDtParam );

  static Distortion xGetMRSAD         ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD4        ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD8        ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD16       ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD32       ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD64       ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD16N      ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD12       ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD24       ( const DistParam& pcDtParam );
  static Distortion xGetMRSAD48       ( const DistParam& pcDtParam );
  static Distortion xGetMRHADs        ( const DistParam& pcDtParam );

  static Distortion xGetHADs          ( const DistParam& pcDtParam );
  static Distortion xCalcHADs2x2      ( const Pel *piOrg, const Pel *piCurr, Int iStrideOrg, Int iStrideCur, Int iStep );
  static Distortion xCalcHADs4x4      ( const Pel *piOrg, const Pel *piCurr, Int iStrideOrg, Int iStrideCur, Int iStep );
  static Distortion xCalcHADs8x8      ( const Pel *piOrg, const Pel *piCurr, Int iStrideOrg, Int iStrideCur, Int iStep );

  static Distortion xCalcHADs16x8     ( const Pel *piOrg, const Pel *piCur, Int iStrideOrg, Int iStrideCur );
  static Distortion xCalcHADs8x16     ( const Pel *piOrg, const Pel *piCur, Int iStrideOrg, Int iStrideCur );
  static Distortion xCalcHADs4x8      ( const Pel *piOrg, const Pel *piCur, Int iStrideOrg, Int iStrideCur );
  static Distortion xCalcHADs8x4      ( const Pel *piOrg, const Pel *piCur, Int iStrideOrg, Int iStrideCur );

#ifdef TARGET_SIMD_X86
  template< typename Torg, typename Tcur, X86_VEXT vext >
  static Distortion xGetSSE_SIMD    ( const DistParam& pcDtParam );
  template< typename Torg, typename Tcur, Int iWidth, X86_VEXT vext >
  static Distortion xGetSSE_NxN_SIMD( const DistParam& pcDtParam );

  template< X86_VEXT vext >
  static Distortion xGetSAD_SIMD    ( const DistParam& pcDtParam );
  template< Int iWidth, X86_VEXT vext >
  static Distortion xGetSAD_NxN_SIMD( const DistParam& pcDtParam );

  template< typename Torg, typename Tcur, X86_VEXT vext >
  static Distortion xGetHADs_SIMD   ( const DistParam& pcDtParam );
#endif

public:

#if WCG_EXT
  Distortion   getDistPart( const CPelBuf &org, const CPelBuf &cur, Int bitDepth, const ComponentID compID, DFunc eDFunc, const CPelBuf *orgLuma = NULL );
#else
  Distortion   getDistPart( const CPelBuf &org, const CPelBuf &cur, Int bitDepth, const ComponentID compID, DFunc eDFunc );
#endif

};// END CLASS DEFINITION RdCost

//! \}

#endif // __RDCOST__
