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

#pragma once

#ifndef __SEIWRITE__
#define __SEIWRITE__

#include "VLCWriter.h"
#include "CommonLib/SEI.h"

class OutputBitstream;

//! \ingroup EncoderLib
//! \{
class SEIWriter : public VLCWriter
{
public:
  SEIWriter() {};
  virtual ~SEIWriter() {};

  Void writeSEImessages(OutputBitstream& bs, const SEIMessages &seiList, const SPS *sps, Bool isNested);

protected:
  Void xWriteSEIuserDataUnregistered(const SEIuserDataUnregistered &sei);
  Void xWriteSEIActiveParameterSets(const SEIActiveParameterSets& sei);
  Void xWriteSEIDecodingUnitInfo(const SEIDecodingUnitInfo& sei, const SPS *sps);
  Void xWriteSEIDecodedPictureHash(const SEIDecodedPictureHash& sei);
  Void xWriteSEIBufferingPeriod(const SEIBufferingPeriod& sei, const SPS *sps);
  Void xWriteSEIPictureTiming(const SEIPictureTiming& sei, const SPS *sps);
  Void xWriteSEIRecoveryPoint(const SEIRecoveryPoint& sei);
  Void xWriteSEIFramePacking(const SEIFramePacking& sei);
  Void xWriteSEISegmentedRectFramePacking(const SEISegmentedRectFramePacking& sei);
  Void xWriteSEIDisplayOrientation(const SEIDisplayOrientation &sei);
  Void xWriteSEITemporalLevel0Index(const SEITemporalLevel0Index &sei);
  Void xWriteSEIGradualDecodingRefreshInfo(const SEIGradualDecodingRefreshInfo &sei);
  Void xWriteSEINoDisplay(const SEINoDisplay &sei);
  Void xWriteSEIToneMappingInfo(const SEIToneMappingInfo& sei);
  Void xWriteSEISOPDescription(const SEISOPDescription& sei);
  Void xWriteSEIScalableNesting(OutputBitstream& bs, const SEIScalableNesting& sei, const SPS *sps);
#if HEVC_TILES_WPP
  Void xWriteSEITempMotionConstrainedTileSets(const SEITempMotionConstrainedTileSets& sei);
#endif
  Void xWriteSEITimeCode(const SEITimeCode& sei);
  Void xWriteSEIChromaResamplingFilterHint(const SEIChromaResamplingFilterHint& sei);
  Void xWriteSEIKneeFunctionInfo(const SEIKneeFunctionInfo &sei);
  Void xWriteSEIColourRemappingInfo(const SEIColourRemappingInfo& sei);
  Void xWriteSEIMasteringDisplayColourVolume( const SEIMasteringDisplayColourVolume& sei);
#if U0033_ALTERNATIVE_TRANSFER_CHARACTERISTICS_SEI
  Void xWriteSEIAlternativeTransferCharacteristics(const SEIAlternativeTransferCharacteristics& sei);
#endif
  Void xWriteSEIGreenMetadataInfo(const SEIGreenMetadataInfo &sei);

  Void xWriteSEIpayloadData(OutputBitstream& bs, const SEI& sei, const SPS *sps);
  Void xWriteByteAlign();
};

//! \}

#endif