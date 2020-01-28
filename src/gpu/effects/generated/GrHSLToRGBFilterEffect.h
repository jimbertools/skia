/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrHSLToRGBFilterEffect.fp; do not modify.
 **************************************************************************************************/
#ifndef GrHSLToRGBFilterEffect_DEFINED
#define GrHSLToRGBFilterEffect_DEFINED
#include "include/core/SkTypes.h"
#include "include/core/SkMatrix44.h"

#include "src/gpu/GrCoordTransform.h"
#include "src/gpu/GrFragmentProcessor.h"
class GrHSLToRGBFilterEffect : public GrFragmentProcessor {
public:
#include "include/private/SkColorData.h"
#include "include/private/SkNx.h"

    SkPMColor4f constantOutputForConstantInput(const SkPMColor4f& c) const override {
        const auto H = c[0], S = c[1], L = c[2], C = (1 - std::abs(2 * L - 1)) * S;

        const auto p = H + Sk4f(0, 2 / 3.f, 1 / 3.f, 0),
                   q = Sk4f::Min(Sk4f::Max(((p - p.floor()) * 6 - 3).abs() - 1, 0), 1),
                   rgb = (q - 0.5f) * C + L,
                   rgba = Sk4f::Min(Sk4f::Max(Sk4f(rgb[0], rgb[1], rgb[2], c.fA), 0), 1);

        return SkColor4f{rgba[0], rgba[1], rgba[2], rgba[3]}.premul();
    }
    static std::unique_ptr<GrFragmentProcessor> Make() {
        return std::unique_ptr<GrFragmentProcessor>(new GrHSLToRGBFilterEffect());
    }
    GrHSLToRGBFilterEffect(const GrHSLToRGBFilterEffect& src);
    std::unique_ptr<GrFragmentProcessor> clone() const override;
    const char* name() const override { return "HSLToRGBFilterEffect"; }

private:
    GrHSLToRGBFilterEffect()
            : INHERITED(kGrHSLToRGBFilterEffect_ClassID,
                        (OptimizationFlags)(kConstantOutputForConstantInput_OptimizationFlag |
                                            kPreservesOpaqueInput_OptimizationFlag)) {}
    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override;
    void onGetGLSLProcessorKey(const GrShaderCaps&, GrProcessorKeyBuilder*) const override;
    bool onIsEqual(const GrFragmentProcessor&) const override;
    GR_DECLARE_FRAGMENT_PROCESSOR_TEST
    typedef GrFragmentProcessor INHERITED;
};
#endif
