// ============================================================================
//  NovaDroid - sha256.cpp  Public-domain style SHA-256 + file helper.
// ============================================================================
#include "app.h"
#include "v2.h"

namespace {

struct ShaCtx {
    uint32_t st[8];
    uint64_t len = 0;
    uint8_t  buf[64];
    size_t   bl = 0;
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

void ShaInit(ShaCtx& c) {
    c.st[0]=0x6a09e667; c.st[1]=0xbb67ae85; c.st[2]=0x3c6ef372; c.st[3]=0xa54ff53a;
    c.st[4]=0x510e527f; c.st[5]=0x9b05688c; c.st[6]=0x1f83d9ab; c.st[7]=0x5be0cd19;
    c.len = 0; c.bl = 0;
}

void ShaBlock(ShaCtx& c, const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 |
               (uint32_t)p[i*4+2] << 8 | (uint32_t)p[i*4+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c.st[0],b=c.st[1],cc=c.st[2],d=c.st[3],e=c.st[4],f=c.st[5],g=c.st[6],h=c.st[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + mj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c.st[0]+=a; c.st[1]+=b; c.st[2]+=cc; c.st[3]+=d;
    c.st[4]+=e; c.st[5]+=f; c.st[6]+=g; c.st[7]+=h;
}

void ShaUpdate(ShaCtx& c, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    c.len += n;
    while (n) {
        size_t take = 64 - c.bl; if (take > n) take = n;
        memcpy(c.buf + c.bl, p, take);
        c.bl += take; p += take; n -= take;
        if (c.bl == 64) { ShaBlock(c, c.buf); c.bl = 0; }
    }
}

std::string ShaFinal(ShaCtx& c) {
    uint64_t bits = c.len * 8;
    uint8_t pad = 0x80;
    ShaUpdate(c, &pad, 1);
    uint8_t z = 0;
    while (c.bl != 56) ShaUpdate(c, &z, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; ++i) lenb[i] = (uint8_t)(bits >> (56 - i * 8));
    // careful: ShaUpdate already added len; but we only need bits written
    // undo length side-effect by finalizing directly into buffer:
    c.bl = 56; // 56 bytes padding already there per above loop
    memcpy(c.buf + 56, lenb, 8);
    ShaBlock(c, c.buf);
    std::string out(32, '\0');
    for (int i = 0; i < 8; ++i) {
        out[i*4]   = (char)(c.st[i] >> 24); out[i*4+1] = (char)(c.st[i] >> 16);
        out[i*4+2] = (char)(c.st[i] >> 8);  out[i*4+3] = (char)c.st[i];
    }
    return out;
}

} // namespace

std::wstring Sha256OfFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    ShaCtx c; ShaInit(c);
    char buf[256 * 1024];
    DWORD rd = 0;
    for (;;) {
        if (!ReadFile(h, buf, sizeof(buf), &rd, nullptr)) { CloseHandle(h); return L""; }
        if (rd == 0) break;
        ShaUpdate(c, buf, rd);
    }
    CloseHandle(h);
    std::string d = ShaFinal(c);
    static const wchar_t* hx = L"0123456789abcdef";
    std::wstring out;
    out.reserve(64);
    for (unsigned char ch : d) { out += hx[ch >> 4]; out += hx[ch & 15]; }
    return out;
}
