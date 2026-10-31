/* SHA-256 fingerprints for recognizing opaque shader inputs without bundling
 * game binaries. Freestanding C; no host library or allocation dependency. */
#ifndef P2D_SHADER_FINGERPRINT_H
#define P2D_SHADER_FINGERPRINT_H
static U32 p2d_sha_rotr(U32 x,U32 n){return (x>>n)|(x<<(32-n));}
static void p2d_shader_sha256(const unsigned char *data,U32 size,unsigned char out[32]){
 static const U32 k[64]={
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 U32 h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 /* Callers bound recognized inputs to small shader sizes. A 64-bit bit count
    is represented as two words so ARMv4 needs no 64-bit runtime helpers. */
 U32 blocks=size/64+1+(size%64>=56);
 for(U32 block=0;block<blocks;block++){
  U32 w[64];
  for(U32 i=0;i<16;i++){
   U32 value=0;
   for(U32 j=0;j<4;j++){
    U32 pos=block*64+i*4+j;
    U32 byte=pos<size?data[pos]:pos==size?0x80:0;
    if(block==blocks-1&&i>=14){
     U32 bits=i==14?size>>29:size<<3;byte=(bits>>(24-j*8))&255;
    }
    value=(value<<8)|byte;
   }
   w[i]=value;
  }
  for(U32 i=16;i<64;i++){
   U32 x=w[i-15],y=w[i-2];
   U32 s0=p2d_sha_rotr(x,7)^p2d_sha_rotr(x,18)^(x>>3);
   U32 s1=p2d_sha_rotr(y,17)^p2d_sha_rotr(y,19)^(y>>10);
   w[i]=w[i-16]+s0+w[i-7]+s1;
  }
  U32 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
  for(U32 i=0;i<64;i++){
   U32 s1=p2d_sha_rotr(e,6)^p2d_sha_rotr(e,11)^p2d_sha_rotr(e,25);
   U32 t1=z+s1+((e&f)^(~e&g))+k[i]+w[i];
   U32 s0=p2d_sha_rotr(a,2)^p2d_sha_rotr(a,13)^p2d_sha_rotr(a,22);
   U32 t2=s0+((a&b)^(a&c)^(b&c));
   z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
  }
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
 }
 for(U32 i=0;i<32;i++)out[i]=(unsigned char)(h[i/4]>>(24-(i%4)*8));
}
#endif
