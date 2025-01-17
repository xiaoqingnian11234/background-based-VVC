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

/** \file     TAppDecLib.h
    \brief    Decoder application class (header)
*/

#ifndef __DECAPP__
#define __DECAPP__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Utilities/VideoIOYuv.h"
#include "Utilities/ColourRemapping.h"
#include "CommonLib/Picture.h"
#include "DecoderLib/DecLib.h"
#include "DecAppCfg.h"

//! \ingroup DecoderApp
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// decoder application class
class DecApp : public DecAppCfg
{
private:
  // class interface
  DecLib          m_cDecLib;                     ///< decoder class
  VideoIOYuv      m_cVideoIOYuvReconFile;        ///< reconstruction YUV class

#if PRINT_PRE_FLAG
  TVideoIOYuv                     m_cTVideoIOYuvPrediFile;		  ///<prediction yuv class
#endif

#if PRINT_BAC_PIC_FLAG
  TVideoIOYuv                    m_cTVideoIOYuvBapicFile;
#endif

#if PRINT_UPDATE_RESI_PIC
  TVideoIOYuv                    m_cTVideoIOYuvRepicFile;
#endif

#if GENERATE_BG_PIC
  Picture* bg_NewPicYuvRec;
#endif
#if BLOCK_GEN
  Picture* bg_NewBlocksRec;
#endif

#if GENERATE_RESI_PIC
  //Picture* bg_NewPicYuvResi;
#endif

#if GENERATE_UPDATE_RESI_PIC
  Picture* bg_NewPicYuvUpdateResi;
#endif

#if GENERATE_TEMPRECO_PIC
  Picture* bg_NewPicYuvReco;
#endif

  // for output control
  Int             m_iPOCLastDisplay;              ///< last POC in display order
  std::ofstream   m_seiMessageFileStream;         ///< Used for outputing SEI messages.
  ColourRemapping m_cColourRemapping;             ///< colour remapping handler


public:
  DecApp();
  virtual ~DecApp         ()  {}

  UInt  decode            (); ///< main decoding function

private:
  Void  xCreateDecLib     (); ///< create internal classes
  Void  xDestroyDecLib    (); ///< destroy internal classes
  Void  xWriteOutput      ( PicList* pcListPic , UInt tId); ///< write YUV to file
  Void  xFlushOutput      ( PicList* pcListPic ); ///< flush all remaining decoded pictures to file
  Bool  isNaluWithinTargetDecLayerIdSet ( InputNALUnit* nalu ); ///< check whether given Nalu is within targetDecLayerIdSet
};

//! \}

#endif // __DECAPP__

