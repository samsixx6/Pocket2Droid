/* GLES1 material argument packing; U32/I32 are the bridge's 32-bit ABI types.
 * Inputs remain untouched on validation failure. No pointer is serialized.
 * GLES1 requires FRONT_AND_BACK; scalar calls accept only SHININESS.
 * https://registry.khronos.org/OpenGL-Refpages/es1.1/xhtml/glMaterial.xml */
#ifndef P2D_MATERIAL_H
#define P2D_MATERIAL_H
static U32 p2d_material_prepare(U32 op,U32 a[16],const U32 *vector){
 U32 count,values[4],is_vector=op==96||op==98;
 if(a[0]!=0x408)return 0x500;
 switch(a[1]){
 case 0x1601:count=1;break;
 case 0x1200:case 0x1201:case 0x1202:case 0x1600:case 0x1602:
  if(!is_vector)return 0x500;count=4;break;
 default:return 0x500;
 }
 if(is_vector){if(!vector)return 0x501;for(U32 i=0;i<count;i++)values[i]=vector[i];}
 else values[0]=a[2];
 if(count==1){
  U32 v=values[0];
  if(op==97||op==98){if((I32)v<0||v>128u*65536u)return 0x501;}
  /* IEEE float bit comparison avoids freestanding ARM float-runtime imports;
   * accept either zero sign, reject negatives/nonfinite/out-of-range values. */
  else if(((v&0x80000000u)&&(v&0x7fffffffu))||(v&0x7fffffffu)>0x43000000u)return 0x501;
 }
 for(U32 i=0;i<count;i++)a[2+i]=values[i];
 return 0;
}
#endif
