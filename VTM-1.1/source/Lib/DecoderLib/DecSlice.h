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

/** \file     DecSlice.h
    \brief    slice decoder class (header)
*/

#ifndef __DECSLICE__
#define __DECSLICE__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "CommonLib/CommonDef.h"
#include "CommonLib/BitStream.h"
#include "DecCu.h"
#include "CABACReader.h"

//! \ingroup DecoderLib
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// slice decoder class
class DecSlice
{
private:
#if GENERATE_BG_PIC
	Picture* m_bgNewPicYuvRecGOP;
#endif

#if GENERATE_RESI_PIC
	Picture* m_bgNewPicYuvResiGOP;
#endif

#if GENERATE_UPDATE_RESI_PIC
	Picture* m_bgNewPicYuvUpdateResiGOP;
#endif

#if GENERATE_TEMPRECO_PIC
	Picture* m_bgNewPicYuvRecoGOP;
#endif

  // access channel
  CABACDecoder*   m_CABACDecoder;
  DecCu*          m_pcCuDecoder;

#if HEVC_DEPENDENT_SLICES
  Ctx             m_lastSliceSegmentEndContextState;    ///< context storage for state at the end of the previous slice-segment (used for dependent slices only).
#endif
#if HEVC_TILES_WPP
  Ctx             m_entropyCodingSyncContextState;      ///< context storage for state of contexts at the wavefront/WPP/entropy-coding-sync second CTU of tile-row
#endif

public:
  DecSlice();
  virtual ~DecSlice();

  Void  init              ( CABACDecoder* cabacDecoder, DecCu* pcMbDecoder );
  Void  create            ();
  Void  destroy           ();

  Void  decompressSlice   ( Slice* slice, InputBitstream* bitstream );

#if GENERATE_BG_PIC
  Void setbgNewPicYuvRecGOP(Picture* m) { m_bgNewPicYuvRecGOP = m; }
#endif

#if GENERATE_RESI_PIC
  Void setbgNewPicYuvResiGOP(Picture* m) { m_bgNewPicYuvResiGOP = m; }
#endif

#if GENERATE_UPDATE_RESI_PIC
  Void setbgNewPicYuvUpdateResiGOP(Picture* m) { m_bgNewPicYuvUpdateResiGOP = m; }
#endif

#if GENERATE_TEMPRECO_PIC
  Void setbgNewPicYuvRecoGOP(Picture* m) { m_bgNewPicYuvRecoGOP = m; }
#endif
};

//! \}

#endif
