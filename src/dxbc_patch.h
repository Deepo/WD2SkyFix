// DXBC patching for WD2SkyFix. Header-only.
// - RewriteSkyReduction (used by the add-on): replaces the 8-step UAV reduction of the sky-light SH shader with a
//   groupshared reduction of one float4 field at a time, with barriers.
// - InsertGroupBarriers (the minimal alternative, not used by the add-on): a sync_uglobal_t
//   (DeviceMemoryBarrierWithGroupSync) before every top-level `if`.
// Both fix the chunk sizes and re-sign the container (MD5-variant checksum).
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace dxbc_patch {

constexpr uint32_t kSyncUglobalT = 0x010048BE;  // opcode 190 (sync) | threads in group | UAV memory global, length 1
constexpr uint32_t kOpIf = 31, kOpEndIf = 21, kOpLoop = 48, kOpEndLoop = 22, kOpSwitch = 76, kOpEndSwitch = 23,
                   kOpCustomData = 53, kOpSync = 190;

inline uint32_t Rd(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline void Wr(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// ---- DXBC checksum: MD5 core with DXBC padding (bytes 20..end)
inline void Md5Block(uint32_t st[4], const uint8_t* blk)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t X[16];
    std::memcpy(X, blk, 64);
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        f = f + a + K[i] + X[g];
        a = d;
        d = c;
        c = b;
        b = b + ((f << S[i]) | (f >> (32 - S[i])));
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

inline void Checksum(const uint8_t* blob, uint32_t total, uint32_t out[4])
{
    const uint8_t* data = blob + 20;
    const uint32_t n = total - 20, bits = n * 8, full = n & ~63u;
    uint32_t st[4] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476};
    for (uint32_t o = 0; o < full; o += 64)
        Md5Block(st, data + o);
    const uint32_t last = n - full;
    uint8_t blk[64];
    const uint32_t tail = (bits >> 2) | 1;
    if (last >= 56) {
        std::memset(blk, 0, 64); std::memcpy(blk, data + full, last); blk[last] = 0x80; Md5Block(st, blk);
        std::memset(blk, 0, 64); std::memcpy(blk, &bits, 4); std::memcpy(blk + 60, &tail, 4); Md5Block(st, blk);
    } else {
        std::memset(blk, 0, 64); std::memcpy(blk, &bits, 4); std::memcpy(blk + 4, data + full, last); blk[4 + last] = 0x80;
        std::memcpy(blk + 60, &tail, 4); Md5Block(st, blk);
    }
    std::memcpy(out, st, 16);
}

// Returns the number of barriers inserted (0 on any parse problem, or if the shader already has a sync instruction).
inline int InsertGroupBarriers(const uint8_t* blob, size_t size, std::vector<uint8_t>& out)
{
    if (size < 32 || std::memcmp(blob, "DXBC", 4) != 0 || Rd(blob + 24) != size)
        return 0;
    const uint32_t nch = Rd(blob + 28);
    if (nch > 64 || 32 + nch * 4 > size)
        return 0;
    uint32_t shex_off = 0;
    for (uint32_t i = 0; i < nch; ++i) {
        const uint32_t o = Rd(blob + 32 + i * 4);
        if (o + 8 <= size && (!std::memcmp(blob + o, "SHEX", 4) || !std::memcmp(blob + o, "SHDR", 4)))
            shex_off = o;
    }
    if (!shex_off)
        return 0;
    const uint32_t csize = Rd(blob + shex_off + 4);
    if (shex_off + 8 + csize > size || csize < 8 || (csize & 3))
        return 0;
    const uint8_t* tk = blob + shex_off + 8;
    const uint32_t ntok = csize / 4;
    if (Rd(tk + 4) != ntok)
        return 0;
    std::vector<uint32_t> toks;
    toks.reserve(ntok + 16);
    toks.push_back(Rd(tk));
    toks.push_back(0);  // length, set below
    int depth = 0, inserted = 0;
    for (uint32_t pos = 2; pos < ntok;) {
        const uint32_t t = Rd(tk + pos * 4), op = t & 0x7FF;
        const uint32_t n = op == kOpCustomData ? (pos + 1 < ntok ? Rd(tk + (pos + 1) * 4) : 0) : (t >> 24) & 0x7F;
        if (n == 0 || pos + n > ntok || op == kOpSync)
            return 0;  // malformed, or already synchronised: leave the shader alone
        if (op == kOpIf && depth == 0) {
            toks.push_back(kSyncUglobalT);
            ++inserted;
        }
        if (op == kOpIf || op == kOpLoop || op == kOpSwitch)
            ++depth;
        else if (op == kOpEndIf || op == kOpEndLoop || op == kOpEndSwitch)
            --depth;
        for (uint32_t k = 0; k < n; ++k)
            toks.push_back(Rd(tk + (pos + k) * 4));
        pos += n;
    }
    if (depth != 0 || !inserted)
        return 0;
    toks[1] = (uint32_t)toks.size();
    const uint32_t grow = inserted * 4;
    out.assign(blob, blob + shex_off + 8);
    Wr(out.data() + shex_off + 4, csize + grow);
    const size_t tok_at = out.size();
    out.resize(tok_at + toks.size() * 4);
    std::memcpy(out.data() + tok_at, toks.data(), toks.size() * 4);
    out.insert(out.end(), blob + shex_off + 8 + csize, blob + size);
    for (uint32_t i = 0; i < nch; ++i) {
        const uint32_t o = Rd(out.data() + 32 + i * 4);
        if (o > shex_off)
            Wr(out.data() + 32 + i * 4, o + grow);
    }
    Wr(out.data() + 24, (uint32_t)out.size());
    uint32_t sum[4];
    Checksum(out.data(), (uint32_t)out.size(), sum);
    std::memcpy(out.data() + 4, sum, 16);
    return inserted;
}

// ---- reduction rewrite ------------------------------------------------------------------------------------------
// The sky-light SH shader (e90c6810): 256 threads store 29 float4 partial sums each in u1 (SkyTempAccumulationBuffer,
// stride 464), then 8 steps `if (tid < n) u1[tid] += u1[tid + n]` that keep 58 float4 in flight (59 temps) and have
// no barrier, then thread 0 writes the totals to u0. The per-thread part and the final write stay byte for byte; the
// 8 steps become (temps r59-r62, groupshared g0[256] float4):
//     sync_uglobal_t
//     for (k = 0; k < 29; ++k) {
//         g0[tid] = u1[tid] @ 16k; sync_g_t
//         for (s = 128; s != 0; s >>= 1) { if (tid < s) g0[tid] += g0[tid + s]; sync_g_t }
//         if (tid == 0) u1[0] @ 16k = g0[0];
//         sync_g_t
//     }
//     sync_uglobal_t
namespace gs {
constexpr uint32_t kOpUlt = 79, kOpDclTemps = 104, kOpDclThreadGroup = 155;
constexpr uint32_t kSyncGT = 0x010018BE, kFlatTidX = 0x0002400A, kVTidX = 0x0002000A;
constexpr uint32_t kFields = 29, kOldTemps = 59, kNewTemps = 63;
constexpr uint32_t R59 = 59, R60 = 60, R61 = 61, R62 = 62, X = 0, Y = 1, Z = 2, W = 3;

struct Op {
    std::vector<uint32_t> t;
    Op& Mask(uint32_t r, uint32_t c) { t.insert(t.end(), {0x00100002u | ((1u << c) << 4), r}); return *this; }  // rN.c dst
    Op& Sel(uint32_t r, uint32_t c) { t.insert(t.end(), {0x0010000Au | (c << 4), r}); return *this; }           // rN.c src
    Op& Dst4(uint32_t r) { t.insert(t.end(), {0x001000F2u, r}); return *this; }
    Op& Src4(uint32_t r) { t.insert(t.end(), {0x00100E46u, r}); return *this; }
    Op& Imm(uint32_t v) { t.insert(t.end(), {0x00004001u, v}); return *this; }
    Op& Tid() { t.push_back(kVTidX); return *this; }
    Op& U1Src() { t.insert(t.end(), {0x0011EE46u, 1u}); return *this; }
    Op& U1Dst() { t.insert(t.end(), {0x0011E0F2u, 1u}); return *this; }
    Op& G0Src() { t.insert(t.end(), {0x0011FE46u, 0u}); return *this; }
    Op& G0Dst() { t.insert(t.end(), {0x0011F0F2u, 0u}); return *this; }
};

// opcode token + optional extended tokens + operands
inline void Emit(std::vector<uint32_t>& c, uint32_t op, const Op& o, uint32_t flags = 0, std::initializer_list<uint32_t> ext = {})
{
    const uint32_t n = 1 + (uint32_t)ext.size() + (uint32_t)o.t.size();
    c.push_back(op | flags | (n << 24) | (ext.size() ? 0x80000000u : 0u));
    c.insert(c.end(), ext.begin(), ext.end());
    c.insert(c.end(), o.t.begin(), o.t.end());
}

inline void Reduction(std::vector<uint32_t>& c)
{
    const uint32_t nz = 1u << 18;  // _nz test
    c.push_back(kSyncUglobalT);
    Emit(c, 54, Op().Mask(R59, X).Imm(0));                       // mov r59.x, 0
    c.push_back(0x01000030);                                     // loop
    Emit(c, 33, Op().Mask(R59, Z).Sel(R59, X).Imm(kFields));     //   ige r59.z, r59.x, 29
    Emit(c, 3, Op().Sel(R59, Z), nz);                            //   breakc_nz r59.z
    Emit(c, 41, Op().Mask(R59, Y).Sel(R59, X).Imm(4));           //   ishl r59.y, r59.x, 4
    Emit(c, 167, Op().Dst4(R60).Tid().Sel(R59, Y).U1Src(), 0, {0x800E8302u, 0x00199983u});  // ld r60, u1[tid] @ r59.y
    Emit(c, 168, Op().G0Dst().Tid().Imm(0).Src4(R60));           //   g0[tid] = r60
    c.push_back(kSyncGT);
    Emit(c, 54, Op().Mask(R59, W).Imm(128));                     //   mov r59.w, 128
    c.push_back(0x01000030);                                     //   loop
    Emit(c, 32, Op().Mask(R61, X).Sel(R59, W).Imm(0));           //     ieq r61.x, r59.w, 0
    Emit(c, 3, Op().Sel(R61, X), nz);                            //     breakc_nz r61.x
    Emit(c, 79, Op().Mask(R61, Y).Tid().Sel(R59, W));            //     ult r61.y, tid, r59.w
    Emit(c, 31, Op().Sel(R61, Y), nz);                           //     if_nz r61.y
    Emit(c, 30, Op().Mask(R61, Z).Tid().Sel(R59, W));            //       iadd r61.z, tid, r59.w
    Emit(c, 167, Op().Dst4(R60).Tid().Imm(0).G0Src());           //       ld r60, g0[tid]
    Emit(c, 167, Op().Dst4(R62).Sel(R61, Z).Imm(0).G0Src());     //       ld r62, g0[tid + s]
    Emit(c, 0, Op().Dst4(R60).Src4(R60).Src4(R62));              //       add r60, r60, r62
    Emit(c, 168, Op().G0Dst().Tid().Imm(0).Src4(R60));           //       g0[tid] = r60
    c.push_back(0x01000015);                                     //     endif
    c.push_back(kSyncGT);
    Emit(c, 85, Op().Mask(R59, W).Sel(R59, W).Imm(1));           //     ushr r59.w, r59.w, 1
    c.push_back(0x01000016);                                     //   endloop
    Emit(c, 31, Op().Tid());                                     //   if_z tid
    Emit(c, 167, Op().Dst4(R60).Imm(0).Imm(0).G0Src());          //     ld r60, g0[0]
    Emit(c, 168, Op().U1Dst().Imm(0).Sel(R59, Y).Src4(R60));     //     u1[0] @ r59.y = r60
    c.push_back(0x01000015);                                     //   endif
    c.push_back(kSyncGT);
    Emit(c, 30, Op().Mask(R59, X).Sel(R59, X).Imm(1));           //   iadd r59.x, r59.x, 1
    c.push_back(0x01000016);                                     // endloop
    c.push_back(kSyncUglobalT);
}
}  // namespace gs

// Returns true if `out` holds the rewritten shader. Refuses (false) anything that doesn't look exactly like the
// sky-light SH shader's structure, or that already has a sync instruction.
inline bool RewriteSkyReduction(const uint8_t* blob, size_t size, std::vector<uint8_t>& out)
{
    if (size < 32 || std::memcmp(blob, "DXBC", 4) != 0 || Rd(blob + 24) != size)
        return false;
    const uint32_t nch = Rd(blob + 28);
    if (nch > 64 || 32 + nch * 4 > size)
        return false;
    uint32_t shex_off = 0;
    for (uint32_t i = 0; i < nch; ++i) {
        const uint32_t o = Rd(blob + 32 + i * 4);
        if (o + 8 <= size && (!std::memcmp(blob + o, "SHEX", 4) || !std::memcmp(blob + o, "SHDR", 4)))
            shex_off = o;
    }
    if (!shex_off)
        return false;
    const uint32_t csize = Rd(blob + shex_off + 4);
    if (shex_off + 8 + csize > size || csize < 8 || (csize & 3))
        return false;
    const uint8_t* tk = blob + shex_off + 8;
    const uint32_t ntok = csize / 4;
    if (Rd(tk + 4) != ntok)
        return false;
    auto T = [&](uint32_t i) { return Rd(tk + i * 4); };
    std::vector<uint32_t> at;  // instruction start positions
    std::vector<int> top_ifs;
    int depth = 0;
    for (uint32_t pos = 2; pos < ntok;) {
        const uint32_t t = T(pos), op = t & 0x7FF;
        const uint32_t n = op == kOpCustomData ? (pos + 1 < ntok ? T(pos + 1) : 0) : (t >> 24) & 0x7F;
        if (n == 0 || pos + n > ntok || op == kOpSync)
            return false;
        if (op == kOpIf && depth == 0)
            top_ifs.push_back((int)at.size());
        if (op == kOpIf || op == kOpLoop || op == kOpSwitch)
            ++depth;
        else if (op == kOpEndIf || op == kOpEndLoop || op == kOpEndSwitch)
            --depth;
        at.push_back(pos);
        pos += n;
    }
    if (depth != 0 || top_ifs.size() != 9)
        return false;
    const size_t first = (size_t)top_ifs[0] - 1, final = (size_t)top_ifs[8];
    if ((T(at[first]) & 0x7FF) != gs::kOpUlt || T(at[final] + 1) != gs::kFlatTidX)
        return false;
    std::vector<uint32_t> toks = {T(0), 0};
    bool temps = false, group = false;
    for (size_t k = 0; k < at.size(); ++k) {
        const uint32_t pos = at[k], end = k + 1 < at.size() ? at[k + 1] : ntok, op = T(pos) & 0x7FF;
        if (k >= first && k < final) {
            if (k == first)
                gs::Reduction(toks);
            continue;
        }
        if (op == gs::kOpDclTemps) {
            if (T(pos + 1) != gs::kOldTemps)
                return false;
            toks.insert(toks.end(), {T(pos), gs::kNewTemps});
            temps = true;
            continue;
        }
        if (op == gs::kOpDclThreadGroup) {
            if (T(pos + 1) != 256 || T(pos + 2) != 1 || T(pos + 3) != 1)
                return false;
            toks.insert(toks.end(), {0x050000A0u, 0x0011F000u, 0u, 16u, 256u});  // dcl_tgsm_structured g0, 16, 256
            group = true;
        }
        for (uint32_t i = pos; i < end; ++i)
            toks.push_back(T(i));
    }
    if (!temps || !group)
        return false;
    toks[1] = (uint32_t)toks.size();
    const int64_t grow = (int64_t)toks.size() * 4 - csize;
    out.assign(blob, blob + shex_off + 8);
    Wr(out.data() + shex_off + 4, (uint32_t)(toks.size() * 4));
    const size_t tok_at = out.size();
    out.resize(tok_at + toks.size() * 4);
    std::memcpy(out.data() + tok_at, toks.data(), toks.size() * 4);
    out.insert(out.end(), blob + shex_off + 8 + csize, blob + size);
    for (uint32_t i = 0; i < nch; ++i) {
        const uint32_t o = Rd(out.data() + 32 + i * 4);
        if (o > shex_off)
            Wr(out.data() + 32 + i * 4, (uint32_t)(o + grow));
    }
    Wr(out.data() + 24, (uint32_t)out.size());
    uint32_t sum[4];
    Checksum(out.data(), (uint32_t)out.size(), sum);
    std::memcpy(out.data() + 4, sum, 16);
    return true;
}

}  // namespace dxbc_patch
