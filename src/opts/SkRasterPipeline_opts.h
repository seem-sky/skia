/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRasterPipeline_opts_DEFINED
#define SkRasterPipeline_opts_DEFINED

#include "SkColorPriv.h"
#include "SkHalf.h"
#include "SkPM4f.h"
#include "SkPM4fPriv.h"
#include "SkRasterPipeline.h"
#include "SkSRGB.h"
#include "SkUtils.h"
#include <utility>

namespace {

#if SK_CPU_SSE_LEVEL >= SK_CPU_SSE_LEVEL_AVX2
    static constexpr int N = 8;
#else
    static constexpr int N = 4;
#endif

    using SkNf = SkNx<N, float>;
    using SkNi = SkNx<N, int>;
    using SkNh = SkNx<N, uint16_t>;

    struct BodyStage;
    struct TailStage;

    using Body = void(SK_VECTORCALL *)(BodyStage*, size_t,         SkNf,SkNf,SkNf,SkNf,
                                                                   SkNf,SkNf,SkNf,SkNf);
    using Tail = void(SK_VECTORCALL *)(TailStage*, size_t, size_t, SkNf,SkNf,SkNf,SkNf,
                                                                   SkNf,SkNf,SkNf,SkNf);
    struct BodyStage { Body next; void* ctx; };
    struct TailStage { Tail next; void* ctx; };

}  // namespace

#define SI static inline

// Stages are logically a pipeline, and physically are contiguous in an array.
// To get to the next stage, we just increment our pointer to the next array element.
SI void SK_VECTORCALL next(BodyStage* st, size_t x,
                           SkNf  r, SkNf  g, SkNf  b, SkNf  a,
                           SkNf dr, SkNf dg, SkNf db, SkNf da) {
    st->next(st+1, x, r,g,b,a, dr,dg,db,da);
}
SI void SK_VECTORCALL next(TailStage* st, size_t x, size_t tail,
                           SkNf  r, SkNf  g, SkNf  b, SkNf  a,
                           SkNf dr, SkNf dg, SkNf db, SkNf da) {
    st->next(st+1, x,tail, r,g,b,a, dr,dg,db,da);
}


#define STAGE(name, kCallNext)                                                           \
    template <bool kIsTail>                                                              \
    static SK_ALWAYS_INLINE void name##_kernel(void* ctx, size_t x, size_t tail,         \
                                               SkNf&  r, SkNf&  g, SkNf&  b, SkNf&  a,   \
                                               SkNf& dr, SkNf& dg, SkNf& db, SkNf& da);  \
    SI void SK_VECTORCALL name(BodyStage* st, size_t x,                                  \
                               SkNf  r, SkNf  g, SkNf  b, SkNf  a,                       \
                               SkNf dr, SkNf dg, SkNf db, SkNf da) {                     \
        name##_kernel<false>(st->ctx, x,0, r,g,b,a, dr,dg,db,da);                        \
        if (kCallNext) {                                                                 \
            next(st, x, r,g,b,a, dr,dg,db,da);                                           \
        }                                                                                \
    }                                                                                    \
    SI void SK_VECTORCALL name(TailStage* st, size_t x, size_t tail,                     \
                               SkNf  r, SkNf  g, SkNf  b, SkNf  a,                       \
                               SkNf dr, SkNf dg, SkNf db, SkNf da) {                     \
        name##_kernel<true>(st->ctx, x,tail, r,g,b,a, dr,dg,db,da);                      \
        if (kCallNext) {                                                                 \
            next(st, x,tail, r,g,b,a, dr,dg,db,da);                                      \
        }                                                                                \
    }                                                                                    \
    template <bool kIsTail>                                                              \
    static SK_ALWAYS_INLINE void name##_kernel(void* ctx, size_t x, size_t tail,         \
                                               SkNf&  r, SkNf&  g, SkNf&  b, SkNf&  a,   \
                                               SkNf& dr, SkNf& dg, SkNf& db, SkNf& da)


// Many xfermodes apply the same logic to each channel.
#define RGBA_XFERMODE(name)                                                     \
    static SK_ALWAYS_INLINE SkNf name##_kernel(const SkNf& s, const SkNf& sa,   \
                                               const SkNf& d, const SkNf& da);  \
    SI void SK_VECTORCALL name(BodyStage* st, size_t x,                         \
                               SkNf  r, SkNf  g, SkNf  b, SkNf  a,              \
                               SkNf dr, SkNf dg, SkNf db, SkNf da) {            \
        r = name##_kernel(r,a,dr,da);                                           \
        g = name##_kernel(g,a,dg,da);                                           \
        b = name##_kernel(b,a,db,da);                                           \
        a = name##_kernel(a,a,da,da);                                           \
        next(st, x, r,g,b,a, dr,dg,db,da);                                      \
    }                                                                           \
    SI void SK_VECTORCALL name(TailStage* st, size_t x, size_t tail,            \
                               SkNf  r, SkNf  g, SkNf  b, SkNf  a,              \
                               SkNf dr, SkNf dg, SkNf db, SkNf da) {            \
        r = name##_kernel(r,a,dr,da);                                           \
        g = name##_kernel(g,a,dg,da);                                           \
        b = name##_kernel(b,a,db,da);                                           \
        a = name##_kernel(a,a,da,da);                                           \
        next(st, x,tail, r,g,b,a, dr,dg,db,da);                                 \
    }                                                                           \
    static SK_ALWAYS_INLINE SkNf name##_kernel(const SkNf& s, const SkNf& sa,   \
                                               const SkNf& d, const SkNf& da)

// Most of the rest apply the same logic to color channels and use srcover's alpha logic.
#define RGB_XFERMODE(name)                                                      \
    static SK_ALWAYS_INLINE SkNf name##_kernel(const SkNf& s, const SkNf& sa,   \
                                               const SkNf& d, const SkNf& da);  \
    SI void SK_VECTORCALL name(BodyStage* st, size_t x,                         \
                               SkNf  r, SkNf  g, SkNf  b, SkNf  a,              \
                               SkNf dr, SkNf dg, SkNf db, SkNf da) {            \
        r = name##_kernel(r,a,dr,da);                                           \
        g = name##_kernel(g,a,dg,da);                                           \
        b = name##_kernel(b,a,db,da);                                           \
        a = a + (da * (1.0f-a));                                                \
        next(st, x, r,g,b,a, dr,dg,db,da);                                      \
    }                                                                           \
    SI void SK_VECTORCALL name(TailStage* st, size_t x, size_t tail,            \
                               SkNf  r, SkNf  g, SkNf  b, SkNf  a,              \
                               SkNf dr, SkNf dg, SkNf db, SkNf da) {            \
        r = name##_kernel(r,a,dr,da);                                           \
        g = name##_kernel(g,a,dg,da);                                           \
        b = name##_kernel(b,a,db,da);                                           \
        a = a + (da * (1.0f-a));                                                \
        next(st, x,tail, r,g,b,a, dr,dg,db,da);                                 \
    }                                                                           \
    static SK_ALWAYS_INLINE SkNf name##_kernel(const SkNf& s, const SkNf& sa,   \
                                               const SkNf& d, const SkNf& da)

SI SkNf inv(const SkNf& x) { return 1.0f - x; }

SI SkNf lerp(const SkNf& from, const SkNf& to, const SkNf& cov) {
    return SkNx_fma(to-from, cov, from);
}

template <bool kIsTail, typename T>
SI SkNx<N,T> load(size_t tail, const T* src) {
    SkASSERT(kIsTail == (tail > 0));
    // TODO: maskload for 32- and 64-bit T
    if (kIsTail) {
        T buf[8] = {0};
        switch (tail & (N-1)) {
            case 7: buf[6] = src[6];
            case 6: buf[5] = src[5];
            case 5: buf[4] = src[4];
            case 4: buf[3] = src[3];
            case 3: buf[2] = src[2];
            case 2: buf[1] = src[1];
        }
        buf[0] = src[0];
        return SkNx<N,T>::Load(buf);
    }
    return SkNx<N,T>::Load(src);
}

template <bool kIsTail, typename T>
SI void store(size_t tail, const SkNx<N,T>& v, T* dst) {
    SkASSERT(kIsTail == (tail > 0));
    // TODO: maskstore for 32- and 64-bit T
    if (kIsTail) {
        switch (tail & (N-1)) {
            case 7: dst[6] = v[6];
            case 6: dst[5] = v[5];
            case 5: dst[4] = v[4];
            case 4: dst[3] = v[3];
            case 3: dst[2] = v[2];
            case 2: dst[1] = v[1];
        }
        dst[0] = v[0];
        return;
    }
    v.store(dst);
}

SI void from_565(const SkNh& _565, SkNf* r, SkNf* g, SkNf* b) {
    auto _32_bit = SkNx_cast<int>(_565);

    *r = SkNx_cast<float>(_32_bit & SK_R16_MASK_IN_PLACE) * (1.0f / SK_R16_MASK_IN_PLACE);
    *g = SkNx_cast<float>(_32_bit & SK_G16_MASK_IN_PLACE) * (1.0f / SK_G16_MASK_IN_PLACE);
    *b = SkNx_cast<float>(_32_bit & SK_B16_MASK_IN_PLACE) * (1.0f / SK_B16_MASK_IN_PLACE);
}

SI SkNh to_565(const SkNf& r, const SkNf& g, const SkNf& b) {
    return SkNx_cast<uint16_t>( SkNx_cast<int>(r * SK_R16_MASK + 0.5f) << SK_R16_SHIFT
                              | SkNx_cast<int>(g * SK_G16_MASK + 0.5f) << SK_G16_SHIFT
                              | SkNx_cast<int>(b * SK_B16_MASK + 0.5f) << SK_B16_SHIFT);
}

STAGE(just_return, false) { }

STAGE(clamp_0, true) {
    a = SkNf::Max(a, 0.0f);
    r = SkNf::Max(r, 0.0f);
    g = SkNf::Max(g, 0.0f);
    b = SkNf::Max(b, 0.0f);
}

STAGE(clamp_a, true) {
    a = SkNf::Min(a, 1.0f);
    r = SkNf::Min(r, a);
    g = SkNf::Min(g, a);
    b = SkNf::Min(b, a);
}

STAGE(unpremul, true) {
    r *= a.invert();
    g *= a.invert();
    b *= a.invert();
}

STAGE(premul, true) {
    r *= a;
    g *= a;
    b *= a;
}

STAGE(move_src_dst, true) {
    dr = r;
    dg = g;
    db = b;
    da = a;
}

// The default shader produces a constant color (from the SkPaint).
STAGE(constant_color, true) {
    auto color = (const SkPM4f*)ctx;
    r = color->r();
    g = color->g();
    b = color->b();
    a = color->a();
}

// s' = sc for a constant c.
STAGE(scale_constant_float, true) {
    SkNf c = *(const float*)ctx;

    r *= c;
    g *= c;
    b *= c;
    a *= c;
}

// s' = d(1-c) + sc, for a constant c.
STAGE(lerp_constant_float, true) {
    SkNf c = *(const float*)ctx;

    r = lerp(dr, r, c);
    g = lerp(dg, g, c);
    b = lerp(db, b, c);
    a = lerp(da, a, c);
}

// s' = sc for 8-bit c.
STAGE(scale_u8, true) {
    auto ptr = *(const uint8_t**)ctx + x;

    SkNf c = SkNx_cast<float>(load<kIsTail>(tail, ptr)) * (1/255.0f);
    r = r*c;
    g = g*c;
    b = b*c;
    a = a*c;
}

// s' = d(1-c) + sc for 8-bit c.
STAGE(lerp_u8, true) {
    auto ptr = *(const uint8_t**)ctx + x;

    SkNf c = SkNx_cast<float>(load<kIsTail>(tail, ptr)) * (1/255.0f);
    r = lerp(dr, r, c);
    g = lerp(dg, g, c);
    b = lerp(db, b, c);
    a = lerp(da, a, c);
}

// s' = d(1-c) + sc for 565 c.
STAGE(lerp_565, true) {
    auto ptr = *(const uint16_t**)ctx + x;
    SkNf cr, cg, cb;
    from_565(load<kIsTail>(tail, ptr), &cr, &cg, &cb);

    r = lerp(dr, r, cr);
    g = lerp(dg, g, cg);
    b = lerp(db, b, cb);
    a = 1.0f;
}

STAGE(load_d_565, true) {
    auto ptr = *(const uint16_t**)ctx + x;
    from_565(load<kIsTail>(tail, ptr), &dr,&dg,&db);
    da = 1.0f;
}

STAGE(load_s_565, true) {
    auto ptr = *(const uint16_t**)ctx + x;
    from_565(load<kIsTail>(tail, ptr), &r,&g,&b);
    a = 1.0f;
}

STAGE(store_565, false) {
    auto ptr = *(uint16_t**)ctx + x;
    store<kIsTail>(tail, to_565(r,g,b), ptr);
}

STAGE(load_d_f16, true) {
    auto ptr = *(const uint64_t**)ctx + x;

    SkNh rh, gh, bh, ah;
    if (kIsTail) {
        uint64_t buf[8] = {0};
        switch (tail & (N-1)) {
            case 7: buf[6] = ptr[6];
            case 6: buf[5] = ptr[5];
            case 5: buf[4] = ptr[4];
            case 4: buf[3] = ptr[3];
            case 3: buf[2] = ptr[2];
            case 2: buf[1] = ptr[1];
        }
        buf[0] = ptr[0];
        SkNh::Load4(buf, &rh, &gh, &bh, &ah);
    } else {
        SkNh::Load4(ptr, &rh, &gh, &bh, &ah);
    }

    dr = SkHalfToFloat_finite_ftz(rh);
    dg = SkHalfToFloat_finite_ftz(gh);
    db = SkHalfToFloat_finite_ftz(bh);
    da = SkHalfToFloat_finite_ftz(ah);
}

STAGE(load_s_f16, true) {
    auto ptr = *(const uint64_t**)ctx + x;

    SkNh rh, gh, bh, ah;
    if (kIsTail) {
        uint64_t buf[8] = {0};
        switch (tail & (N-1)) {
            case 7: buf[6] = ptr[6];
            case 6: buf[5] = ptr[5];
            case 5: buf[4] = ptr[4];
            case 4: buf[3] = ptr[3];
            case 3: buf[2] = ptr[2];
            case 2: buf[1] = ptr[1];
        }
        buf[0] = ptr[0];
        SkNh::Load4(buf, &rh, &gh, &bh, &ah);
    } else {
        SkNh::Load4(ptr, &rh, &gh, &bh, &ah);
    }

    r = SkHalfToFloat_finite_ftz(rh);
    g = SkHalfToFloat_finite_ftz(gh);
    b = SkHalfToFloat_finite_ftz(bh);
    a = SkHalfToFloat_finite_ftz(ah);
}

STAGE(store_f16, false) {
    auto ptr = *(uint64_t**)ctx + x;

    uint64_t buf[8];
    SkNh::Store4(kIsTail ? buf : ptr, SkFloatToHalf_finite_ftz(r),
                                      SkFloatToHalf_finite_ftz(g),
                                      SkFloatToHalf_finite_ftz(b),
                                      SkFloatToHalf_finite_ftz(a));
    if (kIsTail) {
        switch (tail & (N-1)) {
            case 7: ptr[6] = buf[6];
            case 6: ptr[5] = buf[5];
            case 5: ptr[4] = buf[4];
            case 4: ptr[3] = buf[3];
            case 3: ptr[2] = buf[2];
            case 2: ptr[1] = buf[1];
        }
        ptr[0] = buf[0];
    }
}

STAGE(store_f32, false) {
    auto ptr = *(SkPM4f**)ctx + x;

    SkPM4f buf[8];
    SkNf::Store4(kIsTail ? buf : ptr, r,g,b,a);
    if (kIsTail) {
        switch (tail & (N-1)) {
            case 7: ptr[6] = buf[6];
            case 6: ptr[5] = buf[5];
            case 5: ptr[4] = buf[4];
            case 4: ptr[3] = buf[3];
            case 3: ptr[2] = buf[2];
            case 2: ptr[1] = buf[1];
        }
        ptr[0] = buf[0];
    }
}


// Load 8-bit SkPMColor-order sRGB.
STAGE(load_d_srgb, true) {
    auto ptr = *(const uint32_t**)ctx + x;

    auto px = load<kIsTail>(tail, ptr);
    auto to_int = [](const SkNx<N, uint32_t>& v) { return SkNi::Load(&v); };
    dr =    sk_linear_from_srgb_math(to_int((px >> SK_R32_SHIFT) & 0xff));
    dg =    sk_linear_from_srgb_math(to_int((px >> SK_G32_SHIFT) & 0xff));
    db =    sk_linear_from_srgb_math(to_int((px >> SK_B32_SHIFT) & 0xff));
    da = (1/255.0f)*SkNx_cast<float>(to_int( px >> SK_A32_SHIFT        ));
}

STAGE(load_s_srgb, true) {
    auto ptr = *(const uint32_t**)ctx + x;

    auto px = load<kIsTail>(tail, ptr);
    auto to_int = [](const SkNx<N, uint32_t>& v) { return SkNi::Load(&v); };
    r =    sk_linear_from_srgb_math(to_int((px >> SK_R32_SHIFT) & 0xff));
    g =    sk_linear_from_srgb_math(to_int((px >> SK_G32_SHIFT) & 0xff));
    b =    sk_linear_from_srgb_math(to_int((px >> SK_B32_SHIFT) & 0xff));
    a = (1/255.0f)*SkNx_cast<float>(to_int( px >> SK_A32_SHIFT        ));
}

STAGE(store_srgb, false) {
    auto ptr = *(uint32_t**)ctx + x;
    store<kIsTail>(tail, (              sk_linear_to_srgb(r) << SK_R32_SHIFT
                         |              sk_linear_to_srgb(g) << SK_G32_SHIFT
                         |              sk_linear_to_srgb(b) << SK_B32_SHIFT
                         | SkNx_cast<int>(0.5f + 255.0f * a) << SK_A32_SHIFT), (int*)ptr);
}

RGBA_XFERMODE(clear)    { return 0.0f; }
//RGBA_XFERMODE(src)      { return s; }   // This would be a no-op stage, so we just omit it.
RGBA_XFERMODE(dst)      { return d; }

RGBA_XFERMODE(srcatop)  { return s*da + d*inv(sa); }
RGBA_XFERMODE(srcin)    { return s * da; }
RGBA_XFERMODE(srcout)   { return s * inv(da); }
RGBA_XFERMODE(srcover)  { return SkNx_fma(d, inv(sa), s); }
RGBA_XFERMODE(dstatop)  { return srcatop_kernel(d,da,s,sa); }
RGBA_XFERMODE(dstin)    { return srcin_kernel  (d,da,s,sa); }
RGBA_XFERMODE(dstout)   { return srcout_kernel (d,da,s,sa); }
RGBA_XFERMODE(dstover)  { return srcover_kernel(d,da,s,sa); }

RGBA_XFERMODE(modulate) { return s*d; }
RGBA_XFERMODE(multiply) { return s*inv(da) + d*inv(sa) + s*d; }
RGBA_XFERMODE(plus_)    { return s + d; }
RGBA_XFERMODE(screen)   { return s + d - s*d; }
RGBA_XFERMODE(xor_)     { return s*inv(da) + d*inv(sa); }

RGB_XFERMODE(colorburn) {
    return (d == da  ).thenElse(d + s*inv(da),
           (s == 0.0f).thenElse(s + d*inv(sa),
                                sa*(da - SkNf::Min(da, (da-d)*sa/s)) + s*inv(da) + d*inv(sa)));
}
RGB_XFERMODE(colordodge) {
    return (d == 0.0f).thenElse(d + s*inv(da),
           (s == sa  ).thenElse(s + d*inv(sa),
                                sa*SkNf::Min(da, (d*sa)/(sa - s)) + s*inv(da) + d*inv(sa)));
}
RGB_XFERMODE(darken)     { return s + d - SkNf::Max(s*da, d*sa); }
RGB_XFERMODE(difference) { return s + d - 2.0f*SkNf::Min(s*da,d*sa); }
RGB_XFERMODE(exclusion)  { return s + d - 2.0f*s*d; }
RGB_XFERMODE(hardlight) {
    return s*inv(da) + d*inv(sa)
         + (2.0f*s <= sa).thenElse(2.0f*s*d, sa*da - 2.0f*(da-d)*(sa-s));
}
RGB_XFERMODE(lighten) { return s + d - SkNf::Min(s*da, d*sa); }
RGB_XFERMODE(overlay) { return hardlight_kernel(d,da,s,sa); }
RGB_XFERMODE(softlight) {
    SkNf m  = (da > 0.0f).thenElse(d / da, 0.0f),
         s2 = 2.0f*s,
         m4 = 4.0f*m;

    // The logic forks three ways:
    //    1. dark src?
    //    2. light src, dark dst?
    //    3. light src, light dst?
    SkNf darkSrc = d*(sa + (s2 - sa)*(1.0f - m)),     // Used in case 1.
         darkDst = (m4*m4 + m4)*(m - 1.0f) + 7.0f*m,  // Used in case 2.
         liteDst = m.rsqrt().invert() - m,            // Used in case 3.
         liteSrc = d*sa + da*(s2 - sa) * (4.0f*d <= da).thenElse(darkDst, liteDst);  // 2 or 3?
    return s*inv(da) + d*inv(sa) + (s2 <= sa).thenElse(darkSrc, liteSrc);  // 1 or (2 or 3)?
}

STAGE(luminance_to_alpha, true) {
    a = SK_LUM_COEFF_R*r + SK_LUM_COEFF_G*g + SK_LUM_COEFF_B*b;
    r = g = b = 0;
}

STAGE(matrix_4x5, true) {
    auto m = (const float*)ctx;

    auto fma = [](const SkNf& f, const SkNf& m, const SkNf& a) { return SkNx_fma(f,m,a); };
    auto R = fma(r,m[0], fma(g,m[4], fma(b,m[ 8], fma(a,m[12], m[16])))),
         G = fma(r,m[1], fma(g,m[5], fma(b,m[ 9], fma(a,m[13], m[17])))),
         B = fma(r,m[2], fma(g,m[6], fma(b,m[10], fma(a,m[14], m[18])))),
         A = fma(r,m[3], fma(g,m[7], fma(b,m[11], fma(a,m[15], m[19]))));
    r = R;
    g = G;
    b = B;
    a = A;
}

template <typename Fn>
SI Fn enum_to_Fn(SkRasterPipeline::StockStage st) {
    switch (st) {
    #define M(stage) case SkRasterPipeline::stage: return stage;
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M
    }
    SkASSERT(false);
    return just_return;
}

namespace SK_OPTS_NS {

    SI std::function<void(size_t, size_t)> compile_pipeline(const SkRasterPipeline::Stage* stages,
                                                            int nstages) {
        if (nstages == 2 && stages[0].stage == SkRasterPipeline::constant_color) {
            SkPM4f src = *(const SkPM4f*)stages[0].ctx;
            void* dst = stages[1].ctx;
            switch (stages[1].stage) {
                case SkRasterPipeline::store_565: {
                    auto v = SkPackRGB16(src.r() * SK_R16_MASK + 0.5f,
                                         src.g() * SK_G16_MASK + 0.5f,
                                         src.b() * SK_B16_MASK + 0.5f);
                    return [v,dst](size_t x, size_t n) { sk_memset16(*(uint16_t**)dst + x, v, n); };
                }

                case SkRasterPipeline::store_srgb: {
                    auto v = Sk4f_toS32(src.to4f_pmorder());
                    return [v,dst](size_t x, size_t n) { sk_memset32(*(uint32_t**)dst + x, v, n); };
                }

                case SkRasterPipeline::store_f16: {
                    auto v = src.toF16();
                    return [v,dst](size_t x, size_t n) { sk_memset64(*(uint64_t**)dst + x, v, n); };
                }

                default: break;
            }
        }

        struct Compiled {
            Compiled(const SkRasterPipeline::Stage* stages, int nstages) {
                if (nstages == 0) {
                    return;
                }

                fBodyStart = enum_to_Fn<Body>(stages[0].stage);
                fTailStart = enum_to_Fn<Tail>(stages[0].stage);
                for (int i = 0; i < nstages-1; i++) {
                    fBody[i].next = enum_to_Fn<Body>(stages[i+1].stage);
                    fTail[i].next = enum_to_Fn<Tail>(stages[i+1].stage);
                    fBody[i].ctx = fTail[i].ctx = stages[i].ctx;
                }
                fBody[nstages-1].next = just_return;
                fTail[nstages-1].next = just_return;
                fBody[nstages-1].ctx = fTail[nstages-1].ctx = stages[nstages-1].ctx;
            }

            void operator()(size_t x, size_t n) {
                SkNf v;  // Fastest to start uninitialized.

                while (n >= N) {
                    fBodyStart(fBody, x, v,v,v,v, v,v,v,v);
                    x += N;
                    n -= N;
                }
                if (n) {
                    fTailStart(fTail, x,n, v,v,v,v, v,v,v,v);
                }
            }

            Body fBodyStart = just_return;
            Tail fTailStart = just_return;

            BodyStage fBody[SkRasterPipeline::kMaxStages];
            TailStage fTail[SkRasterPipeline::kMaxStages];

        } fn { stages, nstages };
        return fn;
    }

}  // namespace SK_OPTS_NS

#undef SI
#undef STAGE
#undef RGBA_XFERMODE
#undef RGB_XFERMODE

#endif//SkRasterPipeline_opts_DEFINED
