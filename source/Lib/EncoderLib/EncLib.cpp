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

/** \file     EncLib.cpp
    \brief    encoder class
*/

#include "EncLib.h"

#include "EncModeCtrl.h"
#include "AQp.h"
#include "EncCu.h"

#include "CommonLib/Picture.h"
#include "CommonLib/CommonDef.h"
#include "CommonLib/ChromaFormat.h"
#if ENABLE_SPLIT_PARALLELISM
#include <omp.h>
#endif

//! \ingroup EncoderLib
//! \{

// ====================================================================================================================
// Constructor / destructor / create / destroy
// ====================================================================================================================



EncLib::EncLib()
  : m_spsMap( MAX_NUM_SPS )
  , m_ppsMap( MAX_NUM_PPS )
  , m_AUWriterIf( nullptr )
{
  m_iPOCLast          = -1;
  m_iNumPicRcvd       =  0;
  m_uiNumAllPicCoded  =  0;

  m_iMaxRefPicNum     = 0;

#if ENABLE_SIMD_OPT_BUFFER
  g_pelBufOP.initPelBufOpsX86();
#endif
}

EncLib::~EncLib()
{
}

Void EncLib::create ()
{
  // initialize global variables
  initROM();




  // create processing unit classes
  m_cGOPEncoder.        create( );
  m_cSliceEncoder.      create( getSourceWidth(), getSourceHeight(), m_chromaFormatIDC, m_maxCUWidth, m_maxCUHeight, m_maxTotalCUDepth );
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
#if ENABLE_SPLIT_PARALLELISM
  m_numCuEncStacks  = m_numSplitThreads == 1 ? 1 : NUM_RESERVERD_SPLIT_JOBS;
#else
  m_numCuEncStacks  = 1;
#endif
#if ENABLE_WPP_PARALLELISM
  m_numCuEncStacks *= ( m_numWppThreads + m_numWppExtraLines );
#endif

  m_cCuEncoder      = new EncCu              [m_numCuEncStacks];
  m_cInterSearch    = new InterSearch        [m_numCuEncStacks];
  m_cIntraSearch    = new IntraSearch        [m_numCuEncStacks];
  m_cTrQuant        = new TrQuant            [m_numCuEncStacks];
  m_CABACEncoder    = new CABACEncoder       [m_numCuEncStacks];
  m_cRdCost         = new RdCost             [m_numCuEncStacks];
  m_CtxCache        = new CtxCache           [m_numCuEncStacks];

  for( int jId = 0; jId < m_numCuEncStacks; jId++ )
  {
    m_cCuEncoder[jId].         create( this );
  }
#else
  m_cCuEncoder.         create( this );
#endif
  const UInt widthInCtus   = (getSourceWidth()  + m_maxCUWidth  - 1)  / m_maxCUWidth;
  const UInt heightInCtus  = (getSourceHeight() + m_maxCUHeight - 1) / m_maxCUHeight;
  const UInt numCtuInFrame = widthInCtus * heightInCtus;

  if (m_bUseSAO)
  {
    m_cEncSAO.create( getSourceWidth(), getSourceHeight(), m_chromaFormatIDC, m_maxCUWidth, m_maxCUHeight, m_maxTotalCUDepth, m_log2SaoOffsetScale[CHANNEL_TYPE_LUMA], m_log2SaoOffsetScale[CHANNEL_TYPE_CHROMA] );
    m_cEncSAO.createEncData(getSaoCtuBoundary(), numCtuInFrame);
  }

  m_cLoopFilter.create( m_maxTotalCUDepth );

  if ( m_RCEnableRateControl )
  {
    m_cRateCtrl.init( m_framesToBeEncoded, m_RCTargetBitrate, (Int)( (Double)m_iFrameRate/m_temporalSubsampleRatio + 0.5), m_iGOPSize, m_iSourceWidth, m_iSourceHeight,
                      m_maxCUWidth, m_maxCUHeight,m_RCKeepHierarchicalBit, m_RCUseLCUSeparateModel, m_GOPList );
  }

}

Void EncLib::destroy ()
{
  // destroy processing unit classes
  m_cGOPEncoder.        destroy();
  m_cSliceEncoder.      destroy();
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
  for( int jId = 0; jId < m_numCuEncStacks; jId++ )
  {
    m_cCuEncoder[jId].destroy();
  }
#else
  m_cCuEncoder.         destroy();
#endif
  m_cEncSAO.            destroyEncData();
  m_cEncSAO.            destroy();
  m_cLoopFilter.        destroy();
  m_cRateCtrl.          destroy();
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
  for( int jId = 0; jId < m_numCuEncStacks; jId++ )
  {
    m_cInterSearch[jId].   destroy();
    m_cIntraSearch[jId].   destroy();
  }
#else
  m_cInterSearch.       destroy();
  m_cIntraSearch.       destroy();
#endif

#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
  delete[] m_cCuEncoder;
  delete[] m_cInterSearch;
  delete[] m_cIntraSearch;
  delete[] m_cTrQuant;
  delete[] m_CABACEncoder;
  delete[] m_cRdCost;
  delete[] m_CtxCache;
#endif




  // destroy ROM
  destroyROM();
  return;
}

Void EncLib::init( Bool isFieldCoding, AUWriterIf* auWriterIf )
{
  m_AUWriterIf = auWriterIf;

  SPS &sps0=*(m_spsMap.allocatePS(0)); // NOTE: implementations that use more than 1 SPS need to be aware of activation issues.
  PPS &pps0=*(m_ppsMap.allocatePS(0));

  // initialize SPS
  xInitSPS(sps0);
#if HEVC_VPS
  xInitVPS(m_cVPS, sps0);
#endif

#if ENABLE_SPLIT_PARALLELISM
  if( omp_get_dynamic() )
  {
    omp_set_dynamic( false );
  }
  omp_set_nested( true );
#endif


#if U0132_TARGET_BITS_SATURATION
  if (m_RCCpbSaturationEnabled)
  {
    m_cRateCtrl.initHrdParam(sps0.getVuiParameters()->getHrdParameters(), m_iFrameRate, m_RCInitialCpbFullness);
  }
#endif
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
  for( int jId = 0; jId < m_numCuEncStacks; jId++ )
  {
    m_cRdCost[jId].setCostMode ( m_costMode );
    m_cRdCost[jId].setUseQtbt  ( m_QTBT );
  }
#else
  m_cRdCost.setCostMode ( m_costMode );
  m_cRdCost.setUseQtbt  ( m_QTBT );
#endif

  // initialize PPS
  xInitPPS(pps0, sps0);
  xInitRPS(sps0, isFieldCoding);

#if ER_CHROMA_QP_WCG_PPS
  if (m_wcgChromaQpControl.isEnabled())
  {
    PPS &pps1=*(m_ppsMap.allocatePS(1));
    xInitPPS(pps1, sps0);
  }
#endif

  // initialize processing unit classes
  m_cGOPEncoder.  init( this );
  m_cSliceEncoder.init( this, sps0 );
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
  for( int jId = 0; jId < m_numCuEncStacks; jId++ )
  {
    // precache a few objects
    for( int i = 0; i < 10; i++ )
    {
      auto x = m_CtxCache[jId].get();
      m_CtxCache[jId].cache( x );
    }

    m_cCuEncoder[jId].init( this, sps0, jId );

    // initialize transform & quantization class
    m_cTrQuant[jId].init( jId == 0 ? nullptr : m_cTrQuant[0].getQuant(),
                          1 << m_uiQuadtreeTULog2MaxSize,
                          m_useRDOQ,
                          m_useRDOQTS,
#if T0196_SELECTIVE_RDOQ
                          m_useSelectiveRDOQ,
#endif
                          true,
                          m_useTransformSkipFast
                          , m_QTBT
    );

    // initialize encoder search class
    CABACWriter* cabacEstimator = m_CABACEncoder[jId].getCABACEstimator( &sps0 );
    m_cIntraSearch[jId].init( this,
                              &m_cTrQuant[jId],
                              &m_cRdCost[jId],
                              cabacEstimator,
                              getCtxCache( jId ), m_maxCUWidth, m_maxCUHeight, m_maxTotalCUDepth );
    m_cInterSearch[jId].init( this,
                              &m_cTrQuant[jId],
                              m_iSearchRange,
                              m_bipredSearchRange,
                              m_motionEstimationSearchMethod,
                              m_maxCUWidth, m_maxCUHeight, m_maxTotalCUDepth, &m_cRdCost[jId], cabacEstimator, getCtxCache( jId ) );

    // link temporary buffets from intra search with inter search to avoid unnecessary memory overhead
    m_cInterSearch[jId].setTempBuffers( m_cIntraSearch[jId].getSplitCSBuf(), m_cIntraSearch[jId].getFullCSBuf(), m_cIntraSearch[jId].getSaveCSBuf() );
  }
#else  // ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
  m_cCuEncoder.   init( this, sps0 );

  // initialize transform & quantization class
  m_cTrQuant.init( nullptr,
                   1 << m_uiQuadtreeTULog2MaxSize,
                   m_useRDOQ,
                   m_useRDOQTS,
#if T0196_SELECTIVE_RDOQ
                   m_useSelectiveRDOQ,
#endif
                   true,
                   m_useTransformSkipFast
                   , m_QTBT
  );

  // initialize encoder search class
  CABACWriter* cabacEstimator = m_CABACEncoder.getCABACEstimator(&sps0);
  m_cIntraSearch.init( this,
                       &m_cTrQuant,
                       &m_cRdCost,
                       cabacEstimator,
                       getCtxCache(), m_maxCUWidth, m_maxCUHeight, m_maxTotalCUDepth );
  m_cInterSearch.init( this,
                       &m_cTrQuant,
                       m_iSearchRange,
                       m_bipredSearchRange,
                       m_motionEstimationSearchMethod,
                       m_maxCUWidth, m_maxCUHeight, m_maxTotalCUDepth, &m_cRdCost, cabacEstimator, getCtxCache() );

  // link temporary buffets from intra search with inter search to avoid unneccessary memory overhead
  m_cInterSearch.setTempBuffers( m_cIntraSearch.getSplitCSBuf(), m_cIntraSearch.getFullCSBuf(), m_cIntraSearch.getSaveCSBuf() );
#endif // ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM

  m_iMaxRefPicNum = 0;

#if HEVC_USE_SCALING_LISTS
#if ER_CHROMA_QP_WCG_PPS
  if( m_wcgChromaQpControl.isEnabled() )
  {
    xInitScalingLists( sps0, *m_ppsMap.getPS(1) );
    xInitScalingLists( sps0, pps0 );
  }
  else
#endif
  {
    xInitScalingLists( sps0, pps0 );
  }
#endif
#if ENABLE_WPP_PARALLELISM
  m_entropyCodingSyncContextStateVec.resize( pps0.pcv->heightInCtus );
#endif
}

#if HEVC_USE_SCALING_LISTS
Void EncLib::xInitScalingLists(SPS &sps, PPS &pps)
{
  // Initialise scaling lists
  // The encoder will only use the SPS scaling lists. The PPS will never be marked present.
  const Int maxLog2TrDynamicRange[MAX_NUM_CHANNEL_TYPE] =
  {
      sps.getMaxLog2TrDynamicRange(CHANNEL_TYPE_LUMA),
      sps.getMaxLog2TrDynamicRange(CHANNEL_TYPE_CHROMA)
  };

  Quant* quant = getTrQuant()->getQuant();

  if(getUseScalingListId() == SCALING_LIST_OFF)
  {
    quant->setFlatScalingList(maxLog2TrDynamicRange, sps.getBitDepths());
    quant->setUseScalingList(false);
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
    for( int jId = 1; jId < m_numCuEncStacks; jId++ )
    {
      getTrQuant( jId )->getQuant()->setFlatScalingList( maxLog2TrDynamicRange, sps.getBitDepths() );
      getTrQuant( jId )->getQuant()->setUseScalingList( false );
    }
#endif
    sps.setScalingListPresentFlag(false);
    pps.setScalingListPresentFlag(false);
  }
  else if(getUseScalingListId() == SCALING_LIST_DEFAULT)
  {
    sps.getScalingList().setDefaultScalingList ();
    sps.setScalingListPresentFlag(false);
    pps.setScalingListPresentFlag(false);

    quant->setScalingList(&(sps.getScalingList()), maxLog2TrDynamicRange, sps.getBitDepths());
    quant->setUseScalingList(true);
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
    for( int jId = 1; jId < m_numCuEncStacks; jId++ )
    {
      getTrQuant( jId )->getQuant()->setUseScalingList( true );
    }
#endif
  }
  else if(getUseScalingListId() == SCALING_LIST_FILE_READ)
  {
    sps.getScalingList().setDefaultScalingList ();
    if(sps.getScalingList().xParseScalingList(getScalingListFileName()))
    {
      THROW( "parse scaling list");
    }
    sps.getScalingList().checkDcOfMatrix();
    sps.setScalingListPresentFlag(sps.getScalingList().checkDefaultScalingList());
    pps.setScalingListPresentFlag(false);

    quant->setScalingList(&(sps.getScalingList()), maxLog2TrDynamicRange, sps.getBitDepths());
    quant->setUseScalingList(true);
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
    for( int jId = 1; jId < m_numCuEncStacks; jId++ )
    {
      getTrQuant( jId )->getQuant()->setUseScalingList( true );
    }
#endif
  }
  else
  {
    THROW("error : ScalingList == " << getUseScalingListId() << " not supported\n");
  }

  if (getUseScalingListId() != SCALING_LIST_OFF)
  {
    // Prepare delta's:
    for(UInt sizeId = 0; sizeId < SCALING_LIST_SIZE_NUM; sizeId++)
    {
      const Int predListStep = (sizeId == SCALING_LIST_32x32? (SCALING_LIST_NUM/NUMBER_OF_PREDICTION_MODES) : 1); // if 32x32, skip over chroma entries.

      for(UInt listId = 0; listId < SCALING_LIST_NUM; listId+=predListStep)
      {
        sps.getScalingList().checkPredMode( sizeId, listId );
      }
    }
  }
}
#endif

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

Void EncLib::deletePicBuffer()
{
  PicList::iterator iterPic = m_cListPic.begin();
  Int iSize = Int( m_cListPic.size() );

  for ( Int i = 0; i < iSize; i++ )
  {
    Picture* pcPic = *(iterPic++);

    pcPic->destroy();

    // get rid of the qpadaption layer
    while( pcPic->aqlayer.size() )
    {
      delete pcPic->aqlayer.back(); pcPic->aqlayer.pop_back();
    }

    delete pcPic;
    pcPic = NULL;
  }
}

/**
 - Application has picture buffer list with size of GOP + 1
 - Picture buffer list acts like as ring buffer
 - End of the list has the latest picture
 .
 \param   flush               cause encoder to encode a partial GOP
 \param   pcPicYuvOrg         original YUV picture
 \param   pcPicYuvTrueOrg
 \param   snrCSC
 \retval  rcListPicYuvRecOut  list of reconstruction YUV pictures
 \retval  accessUnitsOut      list of output access units
 \retval  iNumEncoded         number of encoded pictures
 */
Void EncLib::encode( Bool flush, PelStorage* pcPicYuvOrg, PelStorage* cPicYuvTrueOrg, const InputColourSpaceConversion snrCSC, std::list<PelUnitBuf*>& rcListPicYuvRecOut,
                     Int& iNumEncoded )
{
  //PROF_ACCUM_AND_START_NEW_SET( getProfilerPic(), P_GOP_LEVEL );
  if (pcPicYuvOrg != NULL)
  {
    // get original YUV
    Picture* pcPicCurr = NULL;

#if ER_CHROMA_QP_WCG_PPS
    Int ppsID=-1; // Use default PPS ID
    if (getWCGChromaQPControl().isEnabled())
    {
      ppsID=getdQPs()[ m_iPOCLast+1 ];
      ppsID+=(getSwitchPOC() != -1 && (m_iPOCLast+1 >= getSwitchPOC())?1:0);
    }
    xGetNewPicBuffer( rcListPicYuvRecOut,
                      pcPicCurr, ppsID );
#else
    xGetNewPicBuffer( rcListPicYuvRecOut,
                      pcPicCurr, -1 ); // Uses default PPS ID. However, could be modified, for example, to use a PPS ID as a function of POC (m_iPOCLast+1)
#endif

    {
      const PPS *pPPS=(ppsID<0) ? m_ppsMap.getFirstPS() : m_ppsMap.getPS(ppsID);
      const SPS *pSPS=m_spsMap.getPS(pPPS->getSPSId());

      pcPicCurr->M_BUFS( 0, PIC_ORIGINAL ).swap( *pcPicYuvOrg );

      pcPicCurr->finalInit( *pSPS, *pPPS );
    }

    pcPicCurr->poc = m_iPOCLast;

    // compute image characteristics
    if ( getUseAdaptiveQP() )
    {
      AQpPreanalyzer::preanalyze( pcPicCurr );
    }
  }

  if ((m_iNumPicRcvd == 0) || (!flush && (m_iPOCLast != 0) && (m_iNumPicRcvd != m_iGOPSize) && (m_iGOPSize != 0)))
  {
    iNumEncoded = 0;
    return;
  }

  if ( m_RCEnableRateControl )
  {
    m_cRateCtrl.initRCGOP( m_iNumPicRcvd );
  }


#if BLOCK_GEN
  Picture* bg_NewBlocksOrgGop = getbgNewBlocksOrg();
  m_cGOPEncoder.setbgNewBlocksOrgGop(bg_NewBlocksOrgGop);

  Picture* bg_NewBlocksRecGop = getbgNewBlocksRec();
  m_cGOPEncoder.setbgNewBlocksRecGop(bg_NewBlocksRecGop);

  Picture* PrePicRecoGop = getPrePicReco();
  m_cGOPEncoder.setPrePicRecoGop(PrePicRecoGop);

  Picture* bg_NewBlockOrgGop = getbgNewBlockOrg();
  m_cGOPEncoder.setbgNewBlockOrgGop(bg_NewBlockOrgGop);

  Picture* bg_NewBlockRecGop = getbgNewBlockRec();
  m_cGOPEncoder.setbgNewBlockRecGop(bg_NewBlockRecGop);

  Picture* bg_NewBlockRecoGop = getbgNewBlockReco();
  m_cGOPEncoder.setbgNewBlockRecoGop(bg_NewBlockRecoGop);
#endif

#if GENERATE_OrgBG_PIC
  Picture* bg_NewPicYuvOrgGop = getbgNewPicYuvOrg();
  m_cGOPEncoder.setbgNewPicYuvOrgGop(bg_NewPicYuvOrgGop);
#endif

#if GENERATE_BG_PIC
  Picture* bg_NewPicYuvRecGop = getbgNewPicYuvRec();
  m_cGOPEncoder.setbgNewPicYuvRecGop(bg_NewPicYuvRecGop);
#endif

#if GENERATE_RESI_PIC
  Picture* bg_NewPicYuvResiGop = getbgNewPicYuvResi();
  m_cGOPEncoder.setbgNewPicYuvResiGop(bg_NewPicYuvResiGop);
#endif

#if GENERATE_UPDATE_RESI_PIC
  Picture* bg_NewPicYuvUpdateResiGop = getbgNewPicYuvUpdateResi();
  m_cGOPEncoder.setbgNewPicYuvUpdateResiGop(bg_NewPicYuvUpdateResiGop);
#endif

#if GENERATE_TEMPRECO_PIC
  Picture* bg_NewPicYuvRecoGop = getbgNewPicYuvReco();
  m_cGOPEncoder.setbgNewPicYuvRecoGop(bg_NewPicYuvRecoGop);
#endif

#if GENERATE_RECO_PIC
  Picture* bg_NewPicYuvTempUpdateRecoGop = getbgNewPicYuvTempUpdateReco();
  m_cGOPEncoder.setbgNewPicYuvTempUpdateRecoGop(bg_NewPicYuvTempUpdateRecoGop);
#endif

#if BG_REFERENCE_SUBSTITUTION
  Picture* rcPicYuvTempGop = getPicYuvTemp();
  m_cGOPEncoder.setrcPicYuvTempGop(rcPicYuvTempGop);
#endif

  // compress GOP
  
  m_cGOPEncoder.compressGOP( m_iPOCLast, m_iNumPicRcvd, m_cListPic, rcListPicYuvRecOut,
                             false, false, snrCSC, m_printFrameMSE );
	  

  if ( m_RCEnableRateControl )
  {
    m_cRateCtrl.destroyRCGOP();
  }

  iNumEncoded         = m_iNumPicRcvd;
  m_iNumPicRcvd       = 0;
  m_uiNumAllPicCoded += iNumEncoded;
}

/**------------------------------------------------
 Separate interlaced frame into two fields
 -------------------------------------------------**/
Void separateFields(Pel* org, Pel* dstField, UInt stride, UInt width, UInt height, Bool isTop)
{
  if (!isTop)
  {
    org += stride;
  }
  for (Int y = 0; y < height>>1; y++)
  {
    for (Int x = 0; x < width; x++)
    {
      dstField[x] = org[x];
    }

    dstField += stride;
    org += stride*2;
  }

}

Void EncLib::encode( Bool flush, PelStorage* pcPicYuvOrg, PelStorage* pcPicYuvTrueOrg, const InputColourSpaceConversion snrCSC, std::list<PelUnitBuf*>& rcListPicYuvRecOut,
                     Int& iNumEncoded, Bool isTff )
{
  iNumEncoded = 0;

  for (Int fieldNum=0; fieldNum<2; fieldNum++)
  {
    if (pcPicYuvOrg)
    {
      /* -- field initialization -- */
      const Bool isTopField=isTff==(fieldNum==0);

      Picture *pcField;
      xGetNewPicBuffer( rcListPicYuvRecOut, pcField, -1 );

      for (UInt comp = 0; comp < ::getNumberValidComponents(pcPicYuvOrg->chromaFormat); comp++)
      {
        const ComponentID compID = ComponentID(comp);
        {
          PelBuf compBuf = pcPicYuvOrg->get( compID );
          separateFields( compBuf.buf,
                         pcField->getOrigBuf().get(compID).buf,
                         compBuf.stride,
                         compBuf.width,
                         compBuf.height,
                         isTopField);
        }
      }

      {
        Int ppsID=-1; // Use default PPS ID
        const PPS *pPPS=(ppsID<0) ? m_ppsMap.getFirstPS() : m_ppsMap.getPS(ppsID);
        const SPS *pSPS=m_spsMap.getPS(pPPS->getSPSId());

        pcField->finalInit( *pSPS, *pPPS );
      }

      pcField->poc = m_iPOCLast;
      pcField->reconstructed = false;

      pcField->setBorderExtension(false);// where is this normally?

      pcField->topField = isTopField;                  // interlaced requirement

      // compute image characteristics
      if ( getUseAdaptiveQP() )
      {
        AQpPreanalyzer::preanalyze( pcField );
      }
    }

    if ( m_iNumPicRcvd && ((flush&&fieldNum==1) || (m_iPOCLast/2)==0 || m_iNumPicRcvd==m_iGOPSize ) )
    {
      // compress GOP
		
	  Bool a = false;
      m_cGOPEncoder.compressGOP( m_iPOCLast, m_iNumPicRcvd, m_cListPic, rcListPicYuvRecOut,
                                 true, isTff, snrCSC, m_printFrameMSE );

      iNumEncoded += m_iNumPicRcvd;
      m_uiNumAllPicCoded += m_iNumPicRcvd;
      m_iNumPicRcvd = 0;
    }
  }
}


// ====================================================================================================================
// Protected member functions
// ====================================================================================================================

/**
 - Application has picture buffer list with size of GOP + 1
 - Picture buffer list acts like as ring buffer
 - End of the list has the latest picture
 .
 \retval rpcPic obtained picture buffer
 */
Void EncLib::xGetNewPicBuffer ( std::list<PelUnitBuf*>& rcListPicYuvRecOut, Picture*& rpcPic, Int ppsId )
{
  // rotate he output buffer
  rcListPicYuvRecOut.push_back( rcListPicYuvRecOut.front() ); rcListPicYuvRecOut.pop_front();

  rpcPic=0;

  // At this point, the SPS and PPS can be considered activated - they are copied to the new Pic.
  const PPS *pPPS=(ppsId<0) ? m_ppsMap.getFirstPS() : m_ppsMap.getPS(ppsId);
  CHECK(!(pPPS!=0), "Unspecified error");
  const PPS &pps=*pPPS;

  const SPS *pSPS=m_spsMap.getPS(pps.getSPSId());
  CHECK(!(pSPS!=0), "Unspecified error");
  const SPS &sps=*pSPS;

  Slice::sortPicList(m_cListPic);

  // use an entry in the buffered list if the maximum number that need buffering has been reached:
  if (m_cListPic.size() >= (UInt)(m_iGOPSize + getMaxDecPicBuffering(MAX_TLAYER-1) + 2) )
  {
    PicList::iterator iterPic  = m_cListPic.begin();
    Int iSize = Int( m_cListPic.size() );
    for ( Int i = 0; i < iSize; i++ )
    {
      rpcPic = *iterPic;
      if( ! rpcPic->referenced )
      {
        break;
      }
      iterPic++;
    }

    // If PPS ID is the same, we will assume that it has not changed since it was last used
    // and return the old object.
    if (pps.getPPSId() != rpcPic->cs->pps->getPPSId())
    {
      // the IDs differ - free up an entry in the list, and then create a new one, as with the case where the max buffering state has not been reached.
      rpcPic->destroy();
      delete rpcPic;
      m_cListPic.erase(iterPic);
      rpcPic=0;
    }
  }

  if (rpcPic==0)
  {
    rpcPic = new Picture;

    rpcPic->create( sps.getChromaFormatIdc(), Size( sps.getPicWidthInLumaSamples(), sps.getPicHeightInLumaSamples()), sps.getMaxCUWidth(), sps.getMaxCUWidth()+16, false );
    if ( getUseAdaptiveQP() )
    {
      const UInt iMaxDQPLayer = pps.getMaxCuDQPDepth()+1;
      rpcPic->aqlayer.resize( iMaxDQPLayer );
      for (UInt d = 0; d < iMaxDQPLayer; d++)
      {
        rpcPic->aqlayer[d] = new AQpLayer( sps.getPicWidthInLumaSamples(), sps.getPicHeightInLumaSamples(), sps.getMaxCUWidth()>>d, sps.getMaxCUHeight()>>d );
      }
    }

    m_cListPic.push_back( rpcPic );
  }

  rpcPic->setBorderExtension( false );
  rpcPic->reconstructed = false;
  rpcPic->referenced = true;


  m_iPOCLast++;
  m_iNumPicRcvd++;
}


#if HEVC_VPS
Void EncLib::xInitVPS(VPS &vps, const SPS &sps)
{
  // The SPS must have already been set up.
  // set the VPS profile information.
  *vps.getPTL() = *sps.getPTL();
  vps.setMaxOpSets(1);
  vps.getTimingInfo()->setTimingInfoPresentFlag       ( false );
  vps.setNumHrdParameters( 0 );

  vps.createHrdParamBuffer();
  for( UInt i = 0; i < vps.getNumHrdParameters(); i ++ )
  {
    vps.setHrdOpSetIdx( 0, i );
    vps.setCprmsPresentFlag( false, i );
    // Set up HrdParameters here.
  }
}
#endif

Void EncLib::xInitSPS(SPS &sps)
{
  ProfileTierLevel& profileTierLevel = *sps.getPTL()->getGeneralPTL();
  profileTierLevel.setLevelIdc                    (m_level);
  profileTierLevel.setTierFlag                    (m_levelTier);
  profileTierLevel.setProfileIdc                  (m_profile);
  profileTierLevel.setProfileCompatibilityFlag    (m_profile, 1);
  profileTierLevel.setProgressiveSourceFlag       (m_progressiveSourceFlag);
  profileTierLevel.setInterlacedSourceFlag        (m_interlacedSourceFlag);
  profileTierLevel.setNonPackedConstraintFlag     (m_nonPackedConstraintFlag);
  profileTierLevel.setFrameOnlyConstraintFlag     (m_frameOnlyConstraintFlag);
  profileTierLevel.setBitDepthConstraint          (m_bitDepthConstraintValue);
  profileTierLevel.setChromaFormatConstraint      (m_chromaFormatConstraintValue);
  profileTierLevel.setIntraConstraintFlag         (m_intraConstraintFlag);
  profileTierLevel.setOnePictureOnlyConstraintFlag(m_onePictureOnlyConstraintFlag);
  profileTierLevel.setLowerBitRateConstraintFlag  (m_lowerBitRateConstraintFlag);

  if ((m_profile == Profile::MAIN10) && (m_bitDepth[CHANNEL_TYPE_LUMA] == 8) && (m_bitDepth[CHANNEL_TYPE_CHROMA] == 8))
  {
    /* The above constraint is equal to Profile::MAIN */
    profileTierLevel.setProfileCompatibilityFlag(Profile::MAIN, 1);
  }
  if (m_profile == Profile::MAIN)
  {
    /* A Profile::MAIN10 decoder can always decode Profile::MAIN */
    profileTierLevel.setProfileCompatibilityFlag( Profile::MAIN10, 1 );
  }

  /* XXX: should Main be marked as compatible with still picture? */
  /* XXX: may be a good idea to refactor the above into a function
   * that chooses the actual compatibility based upon options */

  sps.setPicWidthInLumaSamples  ( m_iSourceWidth      );
  sps.setPicHeightInLumaSamples ( m_iSourceHeight     );
  sps.setConformanceWindow      ( m_conformanceWindow );
  sps.setMaxCUWidth             ( m_maxCUWidth        );
  sps.setMaxCUHeight            ( m_maxCUHeight       );
  sps.setMaxCodingDepth         ( m_maxTotalCUDepth   );
  sps.setChromaFormatIdc        ( m_chromaFormatIDC   );
  sps.setLog2DiffMaxMinCodingBlockSize(m_log2DiffMaxMinCodingBlockSize);

  sps.getSpsNext().setNextToolsEnabled      ( m_profile == Profile::NEXT );
  sps.getSpsNext().setUseQTBT               ( m_QTBT );
  sps.getSpsNext().setCTUSize               ( m_CTUSize );
  sps.getSpsNext().setMinQTSizes            ( m_uiMinQT );
  sps.getSpsNext().setUseLargeCTU           ( m_LargeCTU );
  sps.getSpsNext().setMaxBTDepth            ( m_uiMaxBTDepth, m_uiMaxBTDepthI, m_uiMaxBTDepthIChroma );
  sps.getSpsNext().setUseDualITree          ( m_dualITree );
  sps.getSpsNext().setDisableMotCompress    ( m_DisableMotionCompression );
  sps.getSpsNext().setMTTMode               ( m_MTTMode );
#if ENABLE_WPP_PARALLELISM
  sps.getSpsNext().setUseNextDQP            ( m_AltDQPCoding );
#endif

  // ADD_NEW_TOOL : (encoder lib) set tool enabling flags and associated parameters here

  Int minCUSize = ( /*sps.getSpsNext().getUseQTBT() ? 1 << MIN_CU_LOG2 :*/ sps.getMaxCUWidth() >> sps.getLog2DiffMaxMinCodingBlockSize() );
  Int log2MinCUSize = 0;
  while(minCUSize > 1)
  {
    minCUSize >>= 1;
    log2MinCUSize++;
  }

  sps.setLog2MinCodingBlockSize(log2MinCUSize);

  sps.setPCMLog2MinSize (m_uiPCMLog2MinSize);
  sps.setUsePCM        ( m_usePCM           );
  sps.setPCMLog2MaxSize( m_pcmLog2MaxSize  );

  sps.setQuadtreeTULog2MaxSize( m_uiQuadtreeTULog2MaxSize );
  sps.setQuadtreeTULog2MinSize( m_uiQuadtreeTULog2MinSize );
  sps.setQuadtreeTUMaxDepthInter( m_uiQuadtreeTUMaxDepthInter    );
  sps.setQuadtreeTUMaxDepthIntra( m_uiQuadtreeTUMaxDepthIntra    );

  sps.setSPSTemporalMVPEnabledFlag((getTMVPModeId() == 2 || getTMVPModeId() == 1));

  sps.setMaxTrSize   ( 1 << m_uiQuadtreeTULog2MaxSize );

  sps.setUseAMP ( m_useAMP );

  for (UInt channelType = 0; channelType < MAX_NUM_CHANNEL_TYPE; channelType++)
  {
    sps.setBitDepth      (ChannelType(channelType), m_bitDepth[channelType] );
    sps.setQpBDOffset  (ChannelType(channelType), (6 * (m_bitDepth[channelType] - 8)));
    sps.setPCMBitDepth (ChannelType(channelType), m_PCMBitDepth[channelType]         );
  }

  sps.setUseSAO( m_bUseSAO );

  sps.setMaxTLayers( m_maxTempLayer );
  sps.setTemporalIdNestingFlag( ( m_maxTempLayer == 1 ) ? true : false );

  for (Int i = 0; i < min(sps.getMaxTLayers(),(UInt) MAX_TLAYER); i++ )
  {
    sps.setMaxDecPicBuffering(m_maxDecPicBuffering[i], i);
    sps.setNumReorderPics(m_numReorderPics[i], i);
  }

  sps.setPCMFilterDisableFlag  ( m_bPCMFilterDisableFlag );
#if HEVC_USE_SCALING_LISTS
  sps.setScalingListFlag ( (m_useScalingListId == SCALING_LIST_OFF) ? 0 : 1 );
#endif
#if HEVC_USE_INTRA_SMOOTHING_T32 || HEVC_USE_INTRA_SMOOTHING_T64
  sps.setUseStrongIntraSmoothing( m_useStrongIntraSmoothing );
#endif
  sps.setVuiParametersPresentFlag(getVuiParametersPresentFlag());

  if (sps.getVuiParametersPresentFlag())
  {
    VUI* pcVUI = sps.getVuiParameters();
    pcVUI->setAspectRatioInfoPresentFlag(getAspectRatioInfoPresentFlag());
    pcVUI->setAspectRatioIdc(getAspectRatioIdc());
    pcVUI->setSarWidth(getSarWidth());
    pcVUI->setSarHeight(getSarHeight());
    pcVUI->setOverscanInfoPresentFlag(getOverscanInfoPresentFlag());
    pcVUI->setOverscanAppropriateFlag(getOverscanAppropriateFlag());
    pcVUI->setVideoSignalTypePresentFlag(getVideoSignalTypePresentFlag());
    pcVUI->setVideoFormat(getVideoFormat());
    pcVUI->setVideoFullRangeFlag(getVideoFullRangeFlag());
    pcVUI->setColourDescriptionPresentFlag(getColourDescriptionPresentFlag());
    pcVUI->setColourPrimaries(getColourPrimaries());
    pcVUI->setTransferCharacteristics(getTransferCharacteristics());
    pcVUI->setMatrixCoefficients(getMatrixCoefficients());
    pcVUI->setChromaLocInfoPresentFlag(getChromaLocInfoPresentFlag());
    pcVUI->setChromaSampleLocTypeTopField(getChromaSampleLocTypeTopField());
    pcVUI->setChromaSampleLocTypeBottomField(getChromaSampleLocTypeBottomField());
    pcVUI->setNeutralChromaIndicationFlag(getNeutralChromaIndicationFlag());
    pcVUI->setDefaultDisplayWindow(getDefaultDisplayWindow());
    pcVUI->setFrameFieldInfoPresentFlag(getFrameFieldInfoPresentFlag());
    pcVUI->setFieldSeqFlag(false);
    pcVUI->setHrdParametersPresentFlag(false);
    pcVUI->getTimingInfo()->setPocProportionalToTimingFlag(getPocProportionalToTimingFlag());
    pcVUI->getTimingInfo()->setNumTicksPocDiffOneMinus1   (getNumTicksPocDiffOneMinus1()   );
    pcVUI->setBitstreamRestrictionFlag(getBitstreamRestrictionFlag());
#if HEVC_TILES_WPP
    pcVUI->setTilesFixedStructureFlag(getTilesFixedStructureFlag());
#endif
    pcVUI->setMotionVectorsOverPicBoundariesFlag(getMotionVectorsOverPicBoundariesFlag());
    pcVUI->setMinSpatialSegmentationIdc(getMinSpatialSegmentationIdc());
    pcVUI->setMaxBytesPerPicDenom(getMaxBytesPerPicDenom());
    pcVUI->setMaxBitsPerMinCuDenom(getMaxBitsPerMinCuDenom());
    pcVUI->setLog2MaxMvLengthHorizontal(getLog2MaxMvLengthHorizontal());
    pcVUI->setLog2MaxMvLengthVertical(getLog2MaxMvLengthVertical());
  }

  sps.setNumLongTermRefPicSPS(NUM_LONG_TERM_REF_PIC_SPS);
  CHECK(!(NUM_LONG_TERM_REF_PIC_SPS <= MAX_NUM_LONG_TERM_REF_PICS), "Unspecified error");
  for (Int k = 0; k < NUM_LONG_TERM_REF_PIC_SPS; k++)
  {
    sps.setLtRefPicPocLsbSps(k, 0);
    sps.setUsedByCurrPicLtSPSFlag(k, 0);
  }

#if U0132_TARGET_BITS_SATURATION
  if( getPictureTimingSEIEnabled() || getDecodingUnitInfoSEIEnabled() || getCpbSaturationEnabled() )
#else
  if( getPictureTimingSEIEnabled() || getDecodingUnitInfoSEIEnabled() )
#endif
  {
    xInitHrdParameters(sps);
  }
  if( getBufferingPeriodSEIEnabled() || getPictureTimingSEIEnabled() || getDecodingUnitInfoSEIEnabled() )
  {
    sps.getVuiParameters()->setHrdParametersPresentFlag( true );
  }

  // Set up SPS range extension settings
  sps.getSpsRangeExtension().setTransformSkipRotationEnabledFlag(m_transformSkipRotationEnabledFlag);
  sps.getSpsRangeExtension().setTransformSkipContextEnabledFlag(m_transformSkipContextEnabledFlag);
  for (UInt signallingModeIndex = 0; signallingModeIndex < NUMBER_OF_RDPCM_SIGNALLING_MODES; signallingModeIndex++)
  {
    sps.getSpsRangeExtension().setRdpcmEnabledFlag(RDPCMSignallingMode(signallingModeIndex), m_rdpcmEnabledFlag[signallingModeIndex]);
  }
  sps.getSpsRangeExtension().setExtendedPrecisionProcessingFlag(m_extendedPrecisionProcessingFlag);
  sps.getSpsRangeExtension().setIntraSmoothingDisabledFlag( m_intraSmoothingDisabledFlag );
  sps.getSpsRangeExtension().setHighPrecisionOffsetsEnabledFlag(m_highPrecisionOffsetsEnabledFlag);
  sps.getSpsRangeExtension().setPersistentRiceAdaptationEnabledFlag(m_persistentRiceAdaptationEnabledFlag);
  sps.getSpsRangeExtension().setCabacBypassAlignmentEnabledFlag(m_cabacBypassAlignmentEnabledFlag);
}

#if U0132_TARGET_BITS_SATURATION
// calculate scale value of bitrate and initial delay
Int calcScale(Int x)
{
  if (x==0)
  {
    return 0;
  }
  UInt iMask = 0xffffffff;
  Int ScaleValue = 32;

  while ((x&iMask) != 0)
  {
    ScaleValue--;
    iMask = (iMask >> 1);
  }

  return ScaleValue;
}
#endif
Void EncLib::xInitHrdParameters(SPS &sps)
{
  Bool useSubCpbParams = (getSliceMode() > 0) || (getSliceSegmentMode() > 0);
  Int  bitRate         = getTargetBitrate();
  Bool isRandomAccess  = getIntraPeriod() > 0;
# if U0132_TARGET_BITS_SATURATION
  Int cpbSize          = getCpbSize();
  CHECK(!(cpbSize!=0), "Unspecified error");  // CPB size may not be equal to zero. ToDo: have a better default and check for level constraints
  if( !getVuiParametersPresentFlag() && !getCpbSaturationEnabled() )
#else
  if( !getVuiParametersPresentFlag() )
#endif
  {
    return;
  }

  VUI *vui = sps.getVuiParameters();
  HRD *hrd = vui->getHrdParameters();

  TimingInfo *timingInfo = vui->getTimingInfo();
  timingInfo->setTimingInfoPresentFlag( true );
  switch( getFrameRate() )
  {
  case 24:
    timingInfo->setNumUnitsInTick( 1125000 );    timingInfo->setTimeScale    ( 27000000 );
    break;
  case 25:
    timingInfo->setNumUnitsInTick( 1080000 );    timingInfo->setTimeScale    ( 27000000 );
    break;
  case 30:
    timingInfo->setNumUnitsInTick( 900900 );     timingInfo->setTimeScale    ( 27000000 );
    break;
  case 50:
    timingInfo->setNumUnitsInTick( 540000 );     timingInfo->setTimeScale    ( 27000000 );
    break;
  case 60:
    timingInfo->setNumUnitsInTick( 450450 );     timingInfo->setTimeScale    ( 27000000 );
    break;
  default:
    timingInfo->setNumUnitsInTick( 1001 );       timingInfo->setTimeScale    ( 60000 );
    break;
  }

  if (getTemporalSubsampleRatio()>1)
  {
    UInt temporalSubsampleRatio = getTemporalSubsampleRatio();
    if ( Double(timingInfo->getNumUnitsInTick()) * temporalSubsampleRatio > std::numeric_limits<UInt>::max() )
    {
      timingInfo->setTimeScale( timingInfo->getTimeScale() / temporalSubsampleRatio );
    }
    else
    {
      timingInfo->setNumUnitsInTick( timingInfo->getNumUnitsInTick() * temporalSubsampleRatio );
    }
  }

  Bool rateCnt = ( bitRate > 0 );
  hrd->setNalHrdParametersPresentFlag( rateCnt );
  hrd->setVclHrdParametersPresentFlag( rateCnt );
  hrd->setSubPicCpbParamsPresentFlag( useSubCpbParams );

  if( hrd->getSubPicCpbParamsPresentFlag() )
  {
    hrd->setTickDivisorMinus2( 100 - 2 );                          //
    hrd->setDuCpbRemovalDelayLengthMinus1( 7 );                    // 8-bit precision ( plus 1 for last DU in AU )
    hrd->setSubPicCpbParamsInPicTimingSEIFlag( true );
    hrd->setDpbOutputDelayDuLengthMinus1( 5 + 7 );                 // With sub-clock tick factor of 100, at least 7 bits to have the same value as AU dpb delay
  }
  else
  {
    hrd->setSubPicCpbParamsInPicTimingSEIFlag( false );
  }

#if U0132_TARGET_BITS_SATURATION
  if (calcScale(bitRate) <= 6)
  {
    hrd->setBitRateScale(0);
  }
  else
  {
    hrd->setBitRateScale(calcScale(bitRate) - 6);
  }

  if (calcScale(cpbSize) <= 4)
  {
    hrd->setCpbSizeScale(0);
  }
  else
  {
    hrd->setCpbSizeScale(calcScale(cpbSize) - 4);
  }
#else
  hrd->setBitRateScale( 4 );                                       // in units of 2^( 6 + 4 ) = 1,024 bps
  hrd->setCpbSizeScale( 6 );                                       // in units of 2^( 4 + 6 ) = 1,024 bit
#endif

  hrd->setDuCpbSizeScale( 6 );                                     // in units of 2^( 4 + 6 ) = 1,024 bit

  hrd->setInitialCpbRemovalDelayLengthMinus1(15);                  // assuming 0.5 sec, log2( 90,000 * 0.5 ) = 16-bit
  if( isRandomAccess )
  {
    hrd->setCpbRemovalDelayLengthMinus1(5);                        // 32 = 2^5 (plus 1)
    hrd->setDpbOutputDelayLengthMinus1 (5);                        // 32 + 3 = 2^6
  }
  else
  {
    hrd->setCpbRemovalDelayLengthMinus1(9);                        // max. 2^10
    hrd->setDpbOutputDelayLengthMinus1 (9);                        // max. 2^10
  }

  // Note: parameters for all temporal layers are initialized with the same values
  Int i, j;
  UInt bitrateValue, cpbSizeValue;
  UInt duCpbSizeValue;
  UInt duBitRateValue = 0;

  for( i = 0; i < MAX_TLAYER; i ++ )
  {
    hrd->setFixedPicRateFlag( i, 1 );
    hrd->setPicDurationInTcMinus1( i, 0 );
    hrd->setLowDelayHrdFlag( i, 0 );
    hrd->setCpbCntMinus1( i, 0 );

    //! \todo check for possible PTL violations
    // BitRate[ i ] = ( bit_rate_value_minus1[ i ] + 1 ) * 2^( 6 + bit_rate_scale )
    bitrateValue = bitRate / (1 << (6 + hrd->getBitRateScale()) );      // bitRate is in bits, so it needs to be scaled down
    // CpbSize[ i ] = ( cpb_size_value_minus1[ i ] + 1 ) * 2^( 4 + cpb_size_scale )
#if U0132_TARGET_BITS_SATURATION
    cpbSizeValue = cpbSize / (1 << (4 + hrd->getCpbSizeScale()) );      // using bitRate results in 1 second CPB size
#else
    cpbSizeValue = bitRate / (1 << (4 + hrd->getCpbSizeScale()) );      // using bitRate results in 1 second CPB size
#endif


    // DU CPB size could be smaller (i.e. bitrateValue / number of DUs), but we don't know
    // in how many DUs the slice segment settings will result
    duCpbSizeValue = bitrateValue;
    duBitRateValue = cpbSizeValue;

    for( j = 0; j < ( hrd->getCpbCntMinus1( i ) + 1 ); j ++ )
    {
      hrd->setBitRateValueMinus1( i, j, 0, ( bitrateValue - 1 ) );
      hrd->setCpbSizeValueMinus1( i, j, 0, ( cpbSizeValue - 1 ) );
      hrd->setDuCpbSizeValueMinus1( i, j, 0, ( duCpbSizeValue - 1 ) );
      hrd->setDuBitRateValueMinus1( i, j, 0, ( duBitRateValue - 1 ) );
      hrd->setCbrFlag( i, j, 0, false );

      hrd->setBitRateValueMinus1( i, j, 1, ( bitrateValue - 1) );
      hrd->setCpbSizeValueMinus1( i, j, 1, ( cpbSizeValue - 1 ) );
      hrd->setDuCpbSizeValueMinus1( i, j, 1, ( duCpbSizeValue - 1 ) );
      hrd->setDuBitRateValueMinus1( i, j, 1, ( duBitRateValue - 1 ) );
      hrd->setCbrFlag( i, j, 1, false );
    }
  }
}

Void EncLib::xInitPPS(PPS &pps, const SPS &sps)
{
  // pps ID already initialised.
  pps.setSPSId(sps.getSPSId());

  pps.setConstrainedIntraPred( m_bUseConstrainedIntraPred );
  Bool bUseDQP = (getMaxCuDQPDepth() > 0)? true : false;

  if((getMaxDeltaQP() != 0 )|| getUseAdaptiveQP())
  {
    bUseDQP = true;
  }

#if SHARP_LUMA_DELTA_QP
  if ( getLumaLevelToDeltaQPMapping().isEnabled() )
  {
    bUseDQP = true;
  }
#endif
#if ENABLE_QPA
  if (getUsePerceptQPA() && !bUseDQP)
  {
    CHECK( m_iMaxCuDQPDepth != 0, "max. delta-QP depth must be zero!" );
    bUseDQP = true;
  }
#endif

  if (m_costMode==COST_SEQUENCE_LEVEL_LOSSLESS || m_costMode==COST_LOSSLESS_CODING)
  {
    bUseDQP=false;
  }


  if ( m_RCEnableRateControl )
  {
    pps.setUseDQP(true);
    pps.setMaxCuDQPDepth( 0 );
  }
  else if(bUseDQP)
  {
    pps.setUseDQP(true);
    pps.setMaxCuDQPDepth( m_iMaxCuDQPDepth );
  }
  else
  {
    pps.setUseDQP(false);
    pps.setMaxCuDQPDepth( 0 );
  }

  if ( m_diffCuChromaQpOffsetDepth >= 0 )
  {
    pps.getPpsRangeExtension().setDiffCuChromaQpOffsetDepth(m_diffCuChromaQpOffsetDepth);
    pps.getPpsRangeExtension().clearChromaQpOffsetList();
    pps.getPpsRangeExtension().setChromaQpOffsetListEntry(1, 6, 6);
    /* todo, insert table entries from command line (NB, 0 should not be touched) */
  }
  else
  {
    pps.getPpsRangeExtension().setDiffCuChromaQpOffsetDepth(0);
    pps.getPpsRangeExtension().clearChromaQpOffsetList();
  }
  pps.getPpsRangeExtension().setCrossComponentPredictionEnabledFlag(m_crossComponentPredictionEnabledFlag);
  pps.getPpsRangeExtension().setLog2SaoOffsetScale(CHANNEL_TYPE_LUMA,   m_log2SaoOffsetScale[CHANNEL_TYPE_LUMA  ]);
  pps.getPpsRangeExtension().setLog2SaoOffsetScale(CHANNEL_TYPE_CHROMA, m_log2SaoOffsetScale[CHANNEL_TYPE_CHROMA]);

#if ! HM_NO_ADAPT_PPS_QP
  {
    int baseQp = 26;
    if( 16 == getGOPSize() )
    {
      baseQp = getBaseQP()-24;
    }
    else
    {
      baseQp = getBaseQP()-26;
    }
    const int maxDQP = 25;
    const int minDQP = -26 + sps.getQpBDOffset(CHANNEL_TYPE_LUMA);

    pps.setPicInitQPMinus26( std::min( maxDQP, std::max( minDQP, baseQp ) ));
  }
#endif

#if ER_CHROMA_QP_WCG_PPS
  if (getWCGChromaQPControl().isEnabled())
  {
    const Int baseQp=m_iQP+pps.getPPSId();
    const Double chromaQp = m_wcgChromaQpControl.chromaQpScale * baseQp + m_wcgChromaQpControl.chromaQpOffset;
    const Double dcbQP = m_wcgChromaQpControl.chromaCbQpScale * chromaQp;
    const Double dcrQP = m_wcgChromaQpControl.chromaCrQpScale * chromaQp;
    const Int cbQP =(Int)(dcbQP + ( dcbQP < 0 ? -0.5 : 0.5) );
    const Int crQP =(Int)(dcrQP + ( dcrQP < 0 ? -0.5 : 0.5) );
    pps.setQpOffset(COMPONENT_Cb, Clip3( -12, 12, min(0, cbQP) + m_chromaCbQpOffset ));
    pps.setQpOffset(COMPONENT_Cr, Clip3( -12, 12, min(0, crQP) + m_chromaCrQpOffset));
  }
  else
  {
#endif
  pps.setQpOffset(COMPONENT_Cb, m_chromaCbQpOffset );
  pps.setQpOffset(COMPONENT_Cr, m_chromaCrQpOffset );
#if ER_CHROMA_QP_WCG_PPS
  }
#endif
#if W0038_CQP_ADJ
  Bool bChromaDeltaQPEnabled = false;
  {
    bChromaDeltaQPEnabled = ( m_sliceChromaQpOffsetIntraOrPeriodic[0] || m_sliceChromaQpOffsetIntraOrPeriodic[1] );
    if( !bChromaDeltaQPEnabled )
    {
      for( Int i=0; i<m_iGOPSize; i++ )
      {
        if( m_GOPList[i].m_CbQPoffset || m_GOPList[i].m_CrQPoffset )
        {
          bChromaDeltaQPEnabled = true;
          break;
        }
      }
    }
  }
  pps.setSliceChromaQpFlag(bChromaDeltaQPEnabled);
#endif

#if HEVC_TILES_WPP
  pps.setEntropyCodingSyncEnabledFlag( m_entropyCodingSyncEnabledFlag );
  pps.setTilesEnabledFlag( (m_iNumColumnsMinus1 > 0 || m_iNumRowsMinus1 > 0) );
#endif
  pps.setUseWP( m_useWeightedPred );
  pps.setWPBiPred( m_useWeightedBiPred );
  pps.setOutputFlagPresentFlag( false );
#if HEVC_USE_SIGN_HIDING
  pps.setSignDataHidingEnabledFlag(getSignDataHidingEnabledFlag());
#endif

  if ( getDeblockingFilterMetric() )
  {
    pps.setDeblockingFilterOverrideEnabledFlag(true);
    pps.setPPSDeblockingFilterDisabledFlag(false);
  }
  else
  {
    pps.setDeblockingFilterOverrideEnabledFlag( !getLoopFilterOffsetInPPS() );
    pps.setPPSDeblockingFilterDisabledFlag( getLoopFilterDisable() );
  }

  if (! pps.getPPSDeblockingFilterDisabledFlag())
  {
    pps.setDeblockingFilterBetaOffsetDiv2( getLoopFilterBetaOffset() );
    pps.setDeblockingFilterTcOffsetDiv2( getLoopFilterTcOffset() );
  }
  else
  {
    pps.setDeblockingFilterBetaOffsetDiv2(0);
    pps.setDeblockingFilterTcOffsetDiv2(0);
  }

  // deblockingFilterControlPresentFlag is true if any of the settings differ from the inferred values:
  const Bool deblockingFilterControlPresentFlag = pps.getDeblockingFilterOverrideEnabledFlag() ||
                                                  pps.getPPSDeblockingFilterDisabledFlag()     ||
                                                  pps.getDeblockingFilterBetaOffsetDiv2() != 0 ||
                                                  pps.getDeblockingFilterTcOffsetDiv2() != 0;

  pps.setDeblockingFilterControlPresentFlag(deblockingFilterControlPresentFlag);

  pps.setLog2ParallelMergeLevelMinus2   (m_log2ParallelMergeLevelMinus2 );
  pps.setCabacInitPresentFlag(CABAC_INIT_PRESENT_FLAG);
  pps.setLoopFilterAcrossSlicesEnabledFlag( m_bLFCrossSliceBoundaryFlag );


  Int histogram[MAX_NUM_REF + 1];
  for( Int i = 0; i <= MAX_NUM_REF; i++ )
  {
    histogram[i]=0;
  }
  for( Int i = 0; i < getGOPSize(); i++)
  {
    CHECK(!(getGOPEntry(i).m_numRefPicsActive >= 0 && getGOPEntry(i).m_numRefPicsActive <= MAX_NUM_REF), "Unspecified error");
    histogram[getGOPEntry(i).m_numRefPicsActive]++;
  }

  Int maxHist=-1;
  Int bestPos=0;
  for( Int i = 0; i <= MAX_NUM_REF; i++ )
  {
    if(histogram[i]>maxHist)
    {
      maxHist=histogram[i];
      bestPos=i;
    }
  }
  CHECK(!(bestPos <= 15), "Unspecified error");
  pps.setNumRefIdxL0DefaultActive(bestPos);
  pps.setNumRefIdxL1DefaultActive(bestPos);
  pps.setTransquantBypassEnabledFlag(getTransquantBypassEnabledFlag());
  pps.setUseTransformSkip( m_useTransformSkip );
  pps.getPpsRangeExtension().setLog2MaxTransformSkipBlockSize( m_log2MaxTransformSkipBlockSize  );

#if HEVC_DEPENDENT_SLICES
  if (m_sliceSegmentMode != NO_SLICES)
  {
    pps.setDependentSliceSegmentsEnabledFlag( true );
  }
#endif

#if HEVC_TILES_WPP
  xInitPPSforTiles(pps);
#endif

  pps.pcv = new PreCalcValues( sps, pps, true );
}

//Function for initializing m_RPSList, a list of ReferencePictureSet, based on the GOPEntry objects read from the config file.
Void EncLib::xInitRPS(SPS &sps, Bool isFieldCoding)
{
  ReferencePictureSet*      rps;

  sps.createRPSList(getGOPSize() + m_extraRPSs + 1);
  RPSList* rpsList = sps.getRPSList();

  for( Int i = 0; i < getGOPSize()+m_extraRPSs; i++)
  {
    const GOPEntry &ge = getGOPEntry(i);
    rps = rpsList->getReferencePictureSet(i);
    rps->setNumberOfPictures(ge.m_numRefPics);
    rps->setNumRefIdc(ge.m_numRefIdc);
    Int numNeg = 0;
    Int numPos = 0;
    for( Int j = 0; j < ge.m_numRefPics; j++)
    {
      rps->setDeltaPOC(j,ge.m_referencePics[j]);
      rps->setUsed(j,ge.m_usedByCurrPic[j]);
      if(ge.m_referencePics[j]>0)
      {
        numPos++;
      }
      else
      {
        numNeg++;
      }
    }
    rps->setNumberOfNegativePictures(numNeg);
    rps->setNumberOfPositivePictures(numPos);

    // handle inter RPS intialization from the config file.
    rps->setInterRPSPrediction(ge.m_interRPSPrediction > 0);  // not very clean, converting anything > 0 to true.
    rps->setDeltaRIdxMinus1(0);                               // index to the Reference RPS is always the previous one.
    ReferencePictureSet*     RPSRef = i>0 ? rpsList->getReferencePictureSet(i-1): NULL;  // get the reference RPS

    if (ge.m_interRPSPrediction == 2)  // Automatic generation of the inter RPS idc based on the RIdx provided.
    {
      CHECK(!(RPSRef!=NULL), "Unspecified error");
      Int deltaRPS = getGOPEntry(i-1).m_POC - ge.m_POC;  // the ref POC - current POC
      Int numRefDeltaPOC = RPSRef->getNumberOfPictures();

      rps->setDeltaRPS(deltaRPS);           // set delta RPS
      rps->setNumRefIdc(numRefDeltaPOC+1);  // set the numRefIdc to the number of pictures in the reference RPS + 1.
      Int count=0;
      for (Int j = 0; j <= numRefDeltaPOC; j++ ) // cycle through pics in reference RPS.
      {
        Int RefDeltaPOC = (j<numRefDeltaPOC)? RPSRef->getDeltaPOC(j): 0;  // if it is the last decoded picture, set RefDeltaPOC = 0
        rps->setRefIdc(j, 0);
        for (Int k = 0; k < rps->getNumberOfPictures(); k++ )  // cycle through pics in current RPS.
        {
          if (rps->getDeltaPOC(k) == ( RefDeltaPOC + deltaRPS))  // if the current RPS has a same picture as the reference RPS.
          {
              rps->setRefIdc(j, (rps->getUsed(k)?1:2));
              count++;
              break;
          }
        }
      }
      if (count != rps->getNumberOfPictures())
      {
        msg( WARNING, "Warning: Unable fully predict all delta POCs using the reference RPS index given in the config file.  Setting Inter RPS to false for this RPS.\n");
        rps->setInterRPSPrediction(0);
      }
    }
    else if (ge.m_interRPSPrediction == 1)  // inter RPS idc based on the RefIdc values provided in config file.
    {
      CHECK(!(RPSRef!=NULL), "Unspecified error");
      rps->setDeltaRPS(ge.m_deltaRPS);
      rps->setNumRefIdc(ge.m_numRefIdc);
      for (Int j = 0; j < ge.m_numRefIdc; j++ )
      {
        rps->setRefIdc(j, ge.m_refIdc[j]);
      }
      // the following code overwrite the deltaPOC and Used by current values read from the config file with the ones
      // computed from the RefIdc.  A warning is printed if they are not identical.
      numNeg = 0;
      numPos = 0;
      ReferencePictureSet      RPSTemp;  // temporary variable

      for (Int j = 0; j < ge.m_numRefIdc; j++ )
      {
        if (ge.m_refIdc[j])
        {
          Int deltaPOC = ge.m_deltaRPS + ((j < RPSRef->getNumberOfPictures())? RPSRef->getDeltaPOC(j) : 0);
          RPSTemp.setDeltaPOC((numNeg+numPos),deltaPOC);
          RPSTemp.setUsed((numNeg+numPos),ge.m_refIdc[j]==1?1:0);
          if (deltaPOC<0)
          {
            numNeg++;
          }
          else
          {
            numPos++;
          }
        }
      }
      if (numNeg != rps->getNumberOfNegativePictures())
      {
        msg( WARNING, "Warning: number of negative pictures in RPS is different between intra and inter RPS specified in the config file.\n");
        rps->setNumberOfNegativePictures(numNeg);
        rps->setNumberOfPictures(numNeg+numPos);
      }
      if (numPos != rps->getNumberOfPositivePictures())
      {
        msg( WARNING, "Warning: number of positive pictures in RPS is different between intra and inter RPS specified in the config file.\n");
        rps->setNumberOfPositivePictures(numPos);
        rps->setNumberOfPictures(numNeg+numPos);
      }
      RPSTemp.setNumberOfPictures(numNeg+numPos);
      RPSTemp.setNumberOfNegativePictures(numNeg);
      RPSTemp.sortDeltaPOC();     // sort the created delta POC before comparing
      // check if Delta POC and Used are the same
      // print warning if they are not.
      for (Int j = 0; j < ge.m_numRefIdc; j++ )
      {
        if (RPSTemp.getDeltaPOC(j) != rps->getDeltaPOC(j))
        {
          msg( WARNING, "Warning: delta POC is different between intra RPS and inter RPS specified in the config file.\n");
          rps->setDeltaPOC(j,RPSTemp.getDeltaPOC(j));
        }
        if (RPSTemp.getUsed(j) != rps->getUsed(j))
        {
          msg( WARNING, "Warning: Used by Current in RPS is different between intra and inter RPS specified in the config file.\n");
          rps->setUsed(j,RPSTemp.getUsed(j));
        }
      }
    }
  }
  //In case of field coding, we need to set special parameters for the first bottom field of the sequence, since it is not specified in the cfg file.
  //The position = GOPSize + extraRPSs which is (a priori) unused is reserved for this field in the RPS.
  if (isFieldCoding)
  {
    rps = rpsList->getReferencePictureSet(getGOPSize()+m_extraRPSs);
    rps->setNumberOfPictures(1);
    rps->setNumberOfNegativePictures(1);
    rps->setNumberOfPositivePictures(0);
    rps->setNumberOfLongtermPictures(0);
    rps->setDeltaPOC(0,-1);
    rps->setPOC(0,0);
    rps->setUsed(0,true);
    rps->setInterRPSPrediction(false);
    rps->setDeltaRIdxMinus1(0);
    rps->setDeltaRPS(0);
    rps->setNumRefIdc(0);
  }
}

   // This is a function that
   // determines what Reference Picture Set to use
   // for a specific slice (with POC = POCCurr)
Void EncLib::selectReferencePictureSet(Slice* slice, Int POCCurr, Int GOPid )
{
  slice->setRPSidx(GOPid);

  for(Int extraNum=m_iGOPSize; extraNum<m_extraRPSs+m_iGOPSize; extraNum++)
  {
    if(m_uiIntraPeriod > 0 && getDecodingRefreshType() > 0)
    {
      Int POCIndex = POCCurr%m_uiIntraPeriod;
      if(POCIndex == 0)
      {
        POCIndex = m_uiIntraPeriod;
      }
      if(POCIndex == m_GOPList[extraNum].m_POC)
      {
        slice->setRPSidx(extraNum);
      }
    }
    else
    {
      if(POCCurr==m_GOPList[extraNum].m_POC)
      {
        slice->setRPSidx(extraNum);
      }
    }
  }

  if(POCCurr == 1 && slice->getPic()->fieldPic)
  {
    slice->setRPSidx(m_iGOPSize+m_extraRPSs);
  }

  const ReferencePictureSet *rps = (slice->getSPS()->getRPSList()->getReferencePictureSet(slice->getRPSidx()));
  slice->setRPS(rps);
}

Int EncLib::getReferencePictureSetIdxForSOP(Int POCCurr, Int GOPid )
{
  Int rpsIdx = GOPid;

  for(Int extraNum=m_iGOPSize; extraNum<m_extraRPSs+m_iGOPSize; extraNum++)
  {
    if(m_uiIntraPeriod > 0 && getDecodingRefreshType() > 0)
    {
      Int POCIndex = POCCurr%m_uiIntraPeriod;
      if(POCIndex == 0)
      {
        POCIndex = m_uiIntraPeriod;
      }
      if(POCIndex == m_GOPList[extraNum].m_POC)
      {
        rpsIdx = extraNum;
      }
    }
    else
    {
      if(POCCurr==m_GOPList[extraNum].m_POC)
      {
        rpsIdx = extraNum;
      }
    }
  }

  return rpsIdx;
}

#if HEVC_TILES_WPP
Void  EncLib::xInitPPSforTiles(PPS &pps)
{
  pps.setTileUniformSpacingFlag( m_tileUniformSpacingFlag );
  pps.setNumTileColumnsMinus1( m_iNumColumnsMinus1 );
  pps.setNumTileRowsMinus1( m_iNumRowsMinus1 );
  if( !m_tileUniformSpacingFlag )
  {
    pps.setTileColumnWidth( m_tileColumnWidth );
    pps.setTileRowHeight( m_tileRowHeight );
  }
  pps.setLoopFilterAcrossTilesEnabledFlag( m_loopFilterAcrossTilesEnabledFlag );

  // # substreams is "per tile" when tiles are independent.
}
#endif

Void  EncCfg::xCheckGSParameters()
{
#if HEVC_TILES_WPP
  Int   iWidthInCU = ( m_iSourceWidth%m_maxCUWidth ) ? m_iSourceWidth/m_maxCUWidth + 1 : m_iSourceWidth/m_maxCUWidth;
  Int   iHeightInCU = ( m_iSourceHeight%m_maxCUHeight ) ? m_iSourceHeight/m_maxCUHeight + 1 : m_iSourceHeight/m_maxCUHeight;
  UInt  uiCummulativeColumnWidth = 0;
  UInt  uiCummulativeRowHeight = 0;

  //check the column relative parameters
  if( m_iNumColumnsMinus1 >= (1<<(LOG2_MAX_NUM_COLUMNS_MINUS1+1)) )
  {
    EXIT( "The number of columns is larger than the maximum allowed number of columns." );
  }

  if( m_iNumColumnsMinus1 >= iWidthInCU )
  {
    EXIT( "The current picture can not have so many columns." );
  }

  if( m_iNumColumnsMinus1 && !m_tileUniformSpacingFlag )
  {
    for(Int i=0; i<m_iNumColumnsMinus1; i++)
    {
      uiCummulativeColumnWidth += m_tileColumnWidth[i];
    }

    if( uiCummulativeColumnWidth >= iWidthInCU )
    {
      EXIT( "The width of the column is too large." );
    }
  }

  //check the row relative parameters
  if( m_iNumRowsMinus1 >= (1<<(LOG2_MAX_NUM_ROWS_MINUS1+1)) )
  {
    EXIT( "The number of rows is larger than the maximum allowed number of rows." );
  }

  if( m_iNumRowsMinus1 >= iHeightInCU )
  {
    EXIT( "The current picture can not have so many rows." );
  }

  if( m_iNumRowsMinus1 && !m_tileUniformSpacingFlag )
  {
    for(Int i=0; i<m_iNumRowsMinus1; i++)
    {
      uiCummulativeRowHeight += m_tileRowHeight[i];
    }

    if( uiCummulativeRowHeight >= iHeightInCU )
    {
      EXIT( "The height of the row is too large." );
    }
  }
#endif
}

Bool EncLib::PPSNeedsWriting(Int ppsId)
{
  Bool bChanged=m_ppsMap.getChangedFlag(ppsId);
  m_ppsMap.clearChangedFlag(ppsId);
  return bChanged;
}

Bool EncLib::SPSNeedsWriting(Int spsId)
{
  Bool bChanged=m_spsMap.getChangedFlag(spsId);
  m_spsMap.clearChangedFlag(spsId);
  return bChanged;
}

#if X0038_LAMBDA_FROM_QP_CAPABILITY
Int EncCfg::getQPForPicture(const UInt gopIndex, const Slice *pSlice
#if ADJUST_QP
	,bool qpflag
#endif // ADJUST_QP
) const
{
  const Int lumaQpBDOffset = pSlice->getSPS()->getQpBDOffset(CHANNEL_TYPE_LUMA);
  Int qp;

  if (getCostMode()==COST_LOSSLESS_CODING)
  {
    qp=LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_TEST_QP;
  }
  else
  {
    const SliceType sliceType=pSlice->getSliceType();

    qp = getBaseQP();

    // switch at specific qp and keep this qp offset
    static int appliedSwitchDQQ = 0; /* TODO: MT */
    if( pSlice->getPOC() == getSwitchPOC() )
    {
      appliedSwitchDQQ = getSwitchDQP();
    }
    qp += appliedSwitchDQQ;

#if QP_SWITCHING_FOR_PARALLEL
    const Int* pdQPs = getdQPs();
    if ( pdQPs )
    {
      qp += pdQPs[ pSlice->getPOC() ];
    }
#endif

    if(sliceType==I_SLICE)
    {
      qp += getIntraQPOffset();
    }
    else
    {
#if SHARP_LUMA_DELTA_QP
      // Only adjust QP when not lossless
      if (!(( getMaxDeltaQP() == 0 ) && (!getLumaLevelToDeltaQPMapping().isEnabled()) && (qp == -lumaQpBDOffset ) && (pSlice->getPPS()->getTransquantBypassEnabledFlag())))
#else
      if (!(( getMaxDeltaQP() == 0 ) && (qp == -lumaQpBDOffset ) && (pSlice->getPPS()->getTransquantBypassEnabledFlag())))
#endif

      {
        const GOPEntry &gopEntry=getGOPEntry(gopIndex);
        // adjust QP according to the QP offset for the GOP entry.
#if ADJUST_QP
		if (qpflag)
		{
			if (pSlice->getPOC() % 4 == 0)
			{
				qp = qp + 2;
			}
			else
				qp = qp + 4;
	  }
		else
			qp += gopEntry.m_QPOffset;
#else
		qp += gopEntry.m_QPOffset;
#endif // ADJUST_QP

        

        // adjust QP according to QPOffsetModel for the GOP entry.
        Double dqpOffset=qp*gopEntry.m_QPOffsetModelScale+gopEntry.m_QPOffsetModelOffset+0.5;
        Int qpOffset = (Int)floor(Clip3<Double>(0.0, 3.0, dqpOffset));
        qp += qpOffset ;
      }
    }

#if !QP_SWITCHING_FOR_PARALLEL
    // modify QP if a fractional QP was originally specified, cause dQPs to be 0 or 1.
    const Int* pdQPs = getdQPs();
    if ( pdQPs )
    {
      qp += pdQPs[ pSlice->getPOC() ];
    }
#endif
  }
  qp = Clip3( -lumaQpBDOffset, MAX_QP, qp );
  return qp;
}
#endif

//! \}
