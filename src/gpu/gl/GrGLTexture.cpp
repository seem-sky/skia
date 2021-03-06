/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrGLTexture.h"
#include "GrGLGpu.h"
#include "SkTraceMemoryDump.h"

#define GPUGL static_cast<GrGLGpu*>(this->getGpu())
#define GL_CALL(X) GR_GL_CALL(GPUGL->glInterface(), X)

static inline GrSLType sampler_type(const GrGLTexture::IDDesc& idDesc, const GrGLGpu* gpu) {
    if (idDesc.fInfo.fTarget == GR_GL_TEXTURE_EXTERNAL) {
        SkASSERT(gpu->glCaps().glslCaps()->externalTextureSupport());
        return kTextureExternalSampler_GrSLType;
    } else if (idDesc.fInfo.fTarget == GR_GL_TEXTURE_RECTANGLE) {
        SkASSERT(gpu->glCaps().rectangleTextureSupport());
        return kTexture2DRectSampler_GrSLType;
    } else {
        SkASSERT(idDesc.fInfo.fTarget == GR_GL_TEXTURE_2D);
        return kTexture2DSampler_GrSLType;
    }
}

static inline GrTextureParams::FilterMode highest_filter_mode(const GrGLTexture::IDDesc& idDesc) {
    if (idDesc.fInfo.fTarget == GR_GL_TEXTURE_RECTANGLE ||
        idDesc.fInfo.fTarget == GR_GL_TEXTURE_EXTERNAL) {
        return GrTextureParams::kBilerp_FilterMode;
    }
    return GrTextureParams::kMipMap_FilterMode;
}

// Because this class is virtually derived from GrSurface we must explicitly call its constructor.
GrGLTexture::GrGLTexture(GrGLGpu* gpu, SkBudgeted budgeted, const GrSurfaceDesc& desc,
                         const IDDesc& idDesc)
    : GrSurface(gpu, desc)
    , INHERITED(gpu, desc, sampler_type(idDesc, gpu), highest_filter_mode(idDesc), false) {
    this->init(desc, idDesc);
    this->registerWithCache(budgeted);
}

GrGLTexture::GrGLTexture(GrGLGpu* gpu, SkBudgeted budgeted, const GrSurfaceDesc& desc,
                         const IDDesc& idDesc,
                         bool wasMipMapDataProvided)
    : GrSurface(gpu, desc)
    , INHERITED(gpu, desc, sampler_type(idDesc, gpu), highest_filter_mode(idDesc),
                wasMipMapDataProvided) {
    this->init(desc, idDesc);
    this->registerWithCache(budgeted);
}

GrGLTexture::GrGLTexture(GrGLGpu* gpu, Wrapped, const GrSurfaceDesc& desc, const IDDesc& idDesc)
    : GrSurface(gpu, desc)
    , INHERITED(gpu, desc, sampler_type(idDesc, gpu), highest_filter_mode(idDesc), false) {
    this->init(desc, idDesc);
    this->registerWithCacheWrapped();
}

GrGLTexture::GrGLTexture(GrGLGpu* gpu, const GrSurfaceDesc& desc, const IDDesc& idDesc,
                         bool wasMipMapDataProvided)
    : GrSurface(gpu, desc)
    , INHERITED(gpu, desc, sampler_type(idDesc, gpu), highest_filter_mode(idDesc),
                wasMipMapDataProvided) {
    this->init(desc, idDesc);
}

void GrGLTexture::init(const GrSurfaceDesc& desc, const IDDesc& idDesc) {
    SkASSERT(0 != idDesc.fInfo.fID);
    fTexParams.invalidate();
    fTexParamsTimestamp = GrGpu::kExpiredTimestamp;
    fInfo = idDesc.fInfo;
    fTextureIDOwnership = idDesc.fOwnership;
}

void GrGLTexture::onRelease() {
    if (fInfo.fID) {
        if (GrBackendObjectOwnership::kBorrowed != fTextureIDOwnership) {
            GL_CALL(DeleteTextures(1, &fInfo.fID));
        }
        fInfo.fID = 0;
    }
    INHERITED::onRelease();
}

void GrGLTexture::onAbandon() {
    fInfo.fTarget = 0;
    fInfo.fID = 0;
    INHERITED::onAbandon();
}

GrBackendObject GrGLTexture::getTextureHandle() const {
    return reinterpret_cast<GrBackendObject>(&fInfo);
}

void GrGLTexture::setMemoryBacking(SkTraceMemoryDump* traceMemoryDump,
                                   const SkString& dumpName) const {
    SkString texture_id;
    texture_id.appendU32(this->textureID());
    traceMemoryDump->setMemoryBacking(dumpName.c_str(), "gl_texture",
                                      texture_id.c_str());
}

sk_sp<GrGLTexture> GrGLTexture::MakeWrapped(GrGLGpu* gpu, const GrSurfaceDesc& desc,
                                            const IDDesc& idDesc) {
    return sk_sp<GrGLTexture>(new GrGLTexture(gpu, kWrapped, desc, idDesc));
}

