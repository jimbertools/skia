/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/GrPrimitiveProcessor.h"

#include "src/gpu/GrCoordTransform.h"
#include "src/gpu/GrFragmentProcessor.h"

/**
 * We specialize the vertex code for each of these matrix types.
 */
enum MatrixType {
    kNone_MatrixType     = 0,
    kNoPersp_MatrixType  = 1,
    kGeneral_MatrixType  = 2,
};

GrPrimitiveProcessor::GrPrimitiveProcessor(ClassID classID) : GrProcessor(classID) {}

const GrPrimitiveProcessor::TextureSampler& GrPrimitiveProcessor::textureSampler(int i) const {
    SkASSERT(i >= 0 && i < this->numTextureSamplers());
    return this->onTextureSampler(i);
}

uint32_t GrPrimitiveProcessor::computeCoordTransformsKey(const GrFragmentProcessor& fp) const {
    // This is highly coupled with the code in GrGLSLGeometryProcessor::emitTransforms().
    SkASSERT(fp.numCoordTransforms() * 2 <= 32);
    uint32_t totalKey = 0;
    for (int t = 0; t < fp.numCoordTransforms(); ++t) {
        uint32_t key = 0;
        const GrCoordTransform& coordTransform = fp.coordTransform(t);
        if (!fp.coordTransformsApplyToLocalCoords() && coordTransform.isNoOp()) {
            key = kNone_MatrixType;
        } else if (coordTransform.matrix().hasPerspective()) {
            // Note that we can also have homogeneous varyings as a result of a GP local matrix or
            // homogeneous local coords generated by GP. We're relying on the GP to include any
            // variability in those in its key.
            key = kGeneral_MatrixType;
        } else {
            key = kNoPersp_MatrixType;
        }
        key <<= 2*t;
        SkASSERT(0 == (totalKey & key)); // keys for each transform ought not to overlap
        totalKey |= key;
    }
    return totalKey;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static inline GrSamplerState::Filter clamp_filter(GrTextureType type,
                                                  GrSamplerState::Filter requestedFilter) {
    if (GrTextureTypeHasRestrictedSampling(type)) {
        return SkTMin(requestedFilter, GrSamplerState::Filter::kBilerp);
    }
    return requestedFilter;
}

GrPrimitiveProcessor::TextureSampler::TextureSampler(GrSamplerState samplerState,
                                                     const GrBackendFormat& backendFormat,
                                                     const GrSwizzle& swizzle) {
    this->reset(samplerState, backendFormat, swizzle);
}

void GrPrimitiveProcessor::TextureSampler::reset(GrSamplerState samplerState,
                                                 const GrBackendFormat& backendFormat,
                                                 const GrSwizzle& swizzle) {
    fSamplerState = samplerState;
    fSamplerState.setFilterMode(clamp_filter(backendFormat.textureType(), samplerState.filter()));
    fBackendFormat = backendFormat;
    fSwizzle = swizzle;
    fIsInitialized = true;
}
