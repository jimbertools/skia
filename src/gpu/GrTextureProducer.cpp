/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/GrTextureProducer.h"

#include "include/private/GrRecordingContext.h"
#include "src/core/SkMipMap.h"
#include "src/core/SkRectPriv.h"
#include "src/gpu/GrClip.h"
#include "src/gpu/GrContextPriv.h"
#include "src/gpu/GrProxyProvider.h"
#include "src/gpu/GrRecordingContextPriv.h"
#include "src/gpu/GrRenderTargetContext.h"
#include "src/gpu/GrTextureProxy.h"
#include "src/gpu/SkGr.h"
#include "src/gpu/effects/GrBicubicEffect.h"
#include "src/gpu/effects/GrTextureDomain.h"
#include "src/gpu/effects/GrTextureEffect.h"

GrSurfaceProxyView GrTextureProducer::CopyOnGpu(GrRecordingContext* context,
                                                GrSurfaceProxyView inputView,
                                                GrColorType colorType,
                                                const CopyParams& copyParams,
                                                bool dstWillRequireMipMaps) {
    SkASSERT(context);
    SkASSERT(inputView.asTextureProxy());

    const SkRect dstRect = SkRect::Make(copyParams.fDimensions);
    GrMipMapped mipMapped = dstWillRequireMipMaps ? GrMipMapped::kYes : GrMipMapped::kNo;

    GrSurfaceProxy* proxy = inputView.proxy();
    SkRect localRect = proxy->getBoundsRect();

    bool resizing = false;
    if (copyParams.fFilter != GrSamplerState::Filter::kNearest) {
        resizing = localRect.width() != dstRect.width() || localRect.height() != dstRect.height();
    }

    if (copyParams.fFilter == GrSamplerState::Filter::kNearest && !resizing &&
        dstWillRequireMipMaps) {
        GrSurfaceProxyView view = GrCopyBaseMipMapToTextureProxy(context, proxy, inputView.origin(),
                                                                 colorType);
        if (view.proxy()) {
            return view;
        }
    }

    auto copyRTC = GrRenderTargetContext::MakeWithFallback(
            context, colorType, nullptr, SkBackingFit::kExact, copyParams.fDimensions, 1,
            mipMapped, proxy->isProtected(), inputView.origin());
    if (!copyRTC) {
        return {};
    }

    const auto& caps = *context->priv().caps();
    GrPaint paint;

    GrSamplerState sampler(GrSamplerState::WrapMode::kClamp, copyParams.fFilter);
    auto boundsRect = SkIRect::MakeSize(proxy->dimensions());
    auto fp = GrTextureEffect::MakeTexelSubset(std::move(inputView), kUnknown_SkAlphaType,
                                               SkMatrix::I(), sampler, boundsRect, localRect, caps);
    paint.addColorFragmentProcessor(std::move(fp));
    paint.setPorterDuffXPFactory(SkBlendMode::kSrc);

    copyRTC->fillRectToRect(GrNoClip(), std::move(paint), GrAA::kNo, SkMatrix::I(), dstRect,
                            localRect);
    return copyRTC->readSurfaceView();
}

/** Determines whether a texture domain is necessary and if so what domain to use. There are two
 *  rectangles to consider:
 *  - The first is the content area specified by the texture adjuster (i.e., textureContentArea).
 *    We can *never* allow filtering to cause bleed of pixels outside this rectangle.
 *  - The second rectangle is the constraint rectangle (i.e., constraintRect), which is known to
 *    be contained by the content area. The filterConstraint specifies whether we are allowed to
 *    bleed across this rect.
 *
 *  We want to avoid using a domain if possible. We consider the above rectangles, the filter type,
 *  and whether the coords generated by the draw would all fall within the constraint rect. If the
 *  latter is true we only need to consider whether the filter would extend beyond the rects.
 */
GrTextureProducer::DomainMode GrTextureProducer::DetermineDomainMode(
        const SkRect& constraintRect,
        FilterConstraint filterConstraint,
        bool coordsLimitedToConstraintRect,
        GrSurfaceProxy* proxy,
        const GrSamplerState::Filter* filterModeOrNullForBicubic,
        SkRect* domainRect) {
    const SkIRect proxyBounds = SkIRect::MakeSize(proxy->dimensions());

    SkASSERT(proxyBounds.contains(constraintRect));

    const bool proxyIsExact = proxy->isFunctionallyExact();

    // If the constraint rectangle contains the whole proxy then no need for a domain.
    if (constraintRect.contains(proxyBounds) && proxyIsExact) {
        return kNoDomain_DomainMode;
    }

    bool restrictFilterToRect = (filterConstraint == GrTextureProducer::kYes_FilterConstraint);

    // If we can filter outside the constraint rect, and there is no non-content area of the
    // proxy, and we aren't going to generate sample coords outside the constraint rect then we
    // don't need a domain.
    if (!restrictFilterToRect && proxyIsExact && coordsLimitedToConstraintRect) {
        return kNoDomain_DomainMode;
    }

    // Get the domain inset based on sampling mode (or bail if mipped). This is used
    // to evaluate whether we will read outside a non-exact proxy's dimensions.
    // TODO: Let GrTextureEffect handle this.
    SkScalar filterHalfWidth = 0.f;
    if (filterModeOrNullForBicubic) {
        switch (*filterModeOrNullForBicubic) {
            case GrSamplerState::Filter::kNearest:
                if (coordsLimitedToConstraintRect) {
                    return kNoDomain_DomainMode;
                } else {
                    filterHalfWidth = 0.f;
                }
                break;
            case GrSamplerState::Filter::kBilerp:
                filterHalfWidth = .5f;
                break;
            case GrSamplerState::Filter::kMipMap:
                if (restrictFilterToRect || !proxyIsExact) {
                    // No domain can save us here.
                    return kTightCopy_DomainMode;
                }
                return kNoDomain_DomainMode;
        }
    } else {
        // bicubic does nearest filtering internally.
        filterHalfWidth = 1.5f;
    }

    if (restrictFilterToRect) {
        *domainRect = constraintRect;
    } else if (!proxyIsExact) {
        // If we got here then: proxy is not exact, the coords are limited to the
        // constraint rect, and we're allowed to filter across the constraint rect boundary. So
        // we check whether the filter would reach across the edge of the proxy.
        // We will only set the sides that are required.

        *domainRect = SkRectPriv::MakeLargest();
        if (coordsLimitedToConstraintRect) {
            // We may be able to use the fact that the texture coords are limited to the constraint
            // rect in order to avoid having to add a domain.
            bool needContentAreaConstraint = false;
            if (proxyBounds.fRight - filterHalfWidth < constraintRect.fRight) {
                domainRect->fRight = proxyBounds.fRight;
                needContentAreaConstraint = true;
            }
            if (proxyBounds.fBottom - filterHalfWidth < constraintRect.fBottom) {
                domainRect->fBottom = proxyBounds.fBottom;
                needContentAreaConstraint = true;
            }
            if (!needContentAreaConstraint) {
                return kNoDomain_DomainMode;
            }
        }
    } else {
        return kNoDomain_DomainMode;
    }

    if (!filterModeOrNullForBicubic) {
        // Bicubic doesn't yet rely on GrTextureEffect to do this insetting.
        domainRect->inset(0.5f, 0.5f);
        if (domainRect->fLeft > domainRect->fRight) {
            domainRect->fLeft = domainRect->fRight =
                    SkScalarAve(domainRect->fLeft, domainRect->fRight);
        }
        if (domainRect->fTop > domainRect->fBottom) {
            domainRect->fTop = domainRect->fBottom =
                    SkScalarAve(domainRect->fTop, domainRect->fBottom);
        }
    }

    return kDomain_DomainMode;
}

std::unique_ptr<GrFragmentProcessor> GrTextureProducer::createFragmentProcessorForDomainAndFilter(
        GrSurfaceProxyView view,
        const SkMatrix& textureMatrix,
        DomainMode domainMode,
        const SkRect& domain,
        const GrSamplerState::Filter* filterOrNullForBicubic) {
    SkASSERT(kTightCopy_DomainMode != domainMode);
    SkASSERT(view.asTextureProxy());
    const auto& caps = *fContext->priv().caps();
    SkAlphaType srcAlphaType = this->alphaType();
    if (filterOrNullForBicubic) {
        GrSamplerState::WrapMode wrapMode = fDomainNeedsDecal
                                                    ? GrSamplerState::WrapMode::kClampToBorder
                                                    : GrSamplerState::WrapMode::kClamp;
        GrSamplerState samplerState(wrapMode, *filterOrNullForBicubic);
        if (kNoDomain_DomainMode == domainMode) {
            return GrTextureEffect::Make(std::move(view), srcAlphaType, textureMatrix, samplerState,
                                         caps);
        }
        return GrTextureEffect::MakeSubset(std::move(view), srcAlphaType, textureMatrix,
                                           samplerState, domain, caps);
    } else {
        static const GrSamplerState::WrapMode kClampClamp[] = {
                GrSamplerState::WrapMode::kClamp, GrSamplerState::WrapMode::kClamp};
        static const GrSamplerState::WrapMode kDecalDecal[] = {
                GrSamplerState::WrapMode::kClampToBorder, GrSamplerState::WrapMode::kClampToBorder};

        static constexpr auto kDir = GrBicubicEffect::Direction::kXY;
        bool clampToBorderSupport = caps.clampToBorderSupport();
        if (kDomain_DomainMode == domainMode || (fDomainNeedsDecal && !clampToBorderSupport)) {
            GrTextureDomain::Mode wrapMode = fDomainNeedsDecal ? GrTextureDomain::kDecal_Mode
                                         : GrTextureDomain::kClamp_Mode;
            return GrBicubicEffect::Make(view.detachProxy(), textureMatrix, kClampClamp, wrapMode,
                                         wrapMode, kDir, srcAlphaType,
                                         kDomain_DomainMode == domainMode ? &domain : nullptr);
        } else {
            return GrBicubicEffect::Make(view.detachProxy(), textureMatrix,
                                         fDomainNeedsDecal ? kDecalDecal : kClampClamp, kDir,
                                         srcAlphaType);
        }
    }
}

GrSurfaceProxyView GrTextureProducer::viewForParams(
        const GrSamplerState::Filter* filterOrNullForBicubic, SkScalar scaleAdjust[2]) {
    GrSamplerState sampler; // Default is nearest + clamp
    if (filterOrNullForBicubic) {
        sampler.setFilterMode(*filterOrNullForBicubic);
    }
    if (fDomainNeedsDecal) {
        // Assuming hardware support, switch to clamp-to-border instead of clamp
        if (fContext->priv().caps()->clampToBorderSupport()) {
            sampler.setWrapModeX(GrSamplerState::WrapMode::kClampToBorder);
            sampler.setWrapModeY(GrSamplerState::WrapMode::kClampToBorder);
        }
    }
    return this->viewForParams(sampler, scaleAdjust);
}

GrSurfaceProxyView GrTextureProducer::viewForParams(GrSamplerState sampler,
                                                    SkScalar scaleAdjust[2]) {
    // Check that the caller pre-initialized scaleAdjust
    SkASSERT(!scaleAdjust || (scaleAdjust[0] == 1 && scaleAdjust[1] == 1));

    const GrCaps* caps = this->context()->priv().caps();

    int mipCount = SkMipMap::ComputeLevelCount(this->width(), this->height());
    bool willBeMipped = GrSamplerState::Filter::kMipMap == sampler.filter() && mipCount &&
                        caps->mipMapSupport();

    auto result = this->onRefTextureProxyViewForParams(sampler, willBeMipped, scaleAdjust);

    // Check to make sure that if we say the texture willBeMipped that the returned texture has mip
    // maps, unless the config is not copyable.
    SkASSERT(!result.proxy() || !willBeMipped ||
             result.asTextureProxy()->mipMapped() == GrMipMapped::kYes ||
             !caps->isFormatCopyable(result.proxy()->backendFormat()));

    SkASSERT(!result.proxy() || result.asTextureProxy());

    SkDEBUGCODE(bool expectNoScale = (sampler.filter() != GrSamplerState::Filter::kMipMap &&
                                      !sampler.isRepeated()));
    // Check that the "no scaling expected" case always returns a proxy of the same size as the
    // producer.
    SkASSERT(!result.proxy() || !expectNoScale ||
             result.proxy()->dimensions() == this->dimensions());

    return result;
}

std::pair<GrSurfaceProxyView, GrColorType> GrTextureProducer::view(GrMipMapped willNeedMips) {
    GrSamplerState::Filter filter =
            GrMipMapped::kNo == willNeedMips ? GrSamplerState::Filter::kNearest
                                             : GrSamplerState::Filter::kMipMap;
    GrSamplerState sampler(GrSamplerState::WrapMode::kClamp, filter);

    auto result = this->viewForParams(sampler, nullptr);

#ifdef SK_DEBUG
    const GrCaps* caps = this->context()->priv().caps();
    // Check that the resulting proxy format is compatible with the GrColorType of this producer
    SkASSERT(!result.proxy() ||
             result.proxy()->isFormatCompressed(caps) ||
             caps->areColorTypeAndFormatCompatible(this->colorType(),
                                                   result.proxy()->backendFormat()));
#endif

    return {result, this->colorType()};
}
