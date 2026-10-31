//! Bounded OES matrix-palette math shared by CPU and GPU adapters.
use crate::{matrix::{self,Matrix4},fixed_to_f32,GL_INVALID_VALUE,GL_INVALID_OPERATION};
pub const MATRICES:usize=32;
/// Alternative OES normal rule: weighted inverse-transpose of each matrix.
pub fn blend(matrices:&[Matrix4],position:[f32;4],normal:Option<[f32;3]>,weights:&[f32],indices:&[u8])->Result<([f32;4],[f32;3]),u32>{
 blend_lookup(|i|matrices.get(i as usize),position,normal,weights,indices)
}
pub fn blend_lookup<'a>(mut get:impl FnMut(u8)->Option<&'a Matrix4>,position:[f32;4],normal:Option<[f32;3]>,weights:&[f32],indices:&[u8])->Result<([f32;4],[f32;3]),u32>{
 if weights.is_empty()||weights.len()>4||weights.len()!=indices.len(){return Err(GL_INVALID_OPERATION);}
 let mut p=[0.;4];let mut n=[0.;3];
 for (&w,&idx) in weights.iter().zip(indices){
  let Some(m)=get(idx) else{return Err(GL_INVALID_OPERATION)};
  let v=matrix::transform(m,position);for i in 0..4{p[i]+=w*v[i];}
  if let Some(v)=normal{
   let (a,b,c,d,e,f,g,h,i)=(m[0],m[4],m[8],m[1],m[5],m[9],m[2],m[6],m[10]);
   let det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
   if det==0.||!det.is_finite(){return Err(GL_INVALID_OPERATION);}
   let cofactor=[[e*i-f*h,f*g-d*i,d*h-e*g],[c*h-b*i,a*i-c*g,b*g-a*h],[b*f-c*e,c*d-a*f,a*e-b*d]];
   for j in 0..3{n[j]+=w*(cofactor[j][0]*v[0]+cofactor[j][1]*v[1]+cofactor[j][2]*v[2])/det;}
  }
 }Ok((p,n))
}
/// Apply the existing matrix vocabulary without constructing a GL context.
pub fn command(m:&mut Matrix4,op:u32,a:&[u32;16])->Result<(),u32>{
 let fixed=matches!(op,75|93|101|108|118|122|138);
 let v:[f32;16]=std::array::from_fn(|i|if fixed{fixed_to_f32(a[i] as i32)}else{f32::from_bits(a[i])});
 let next=match op{
  91=>matrix::IDENTITY,92|93=>v,100|101=>matrix::multiply(m,&v),
  137|138=>matrix::multiply(m,&matrix::translate(v[0],v[1],v[2])),
  121|122=>matrix::multiply(m,&matrix::scale(v[0],v[1],v[2])),
  117|118=>matrix::multiply(m,&matrix::rotate(v[0],v[1],v[2],v[3])),
  74|75=>{if v[4]<=0.||v[5]<=0.||v[0]==v[1]||v[2]==v[3]||v[4]==v[5]{return Err(GL_INVALID_VALUE);}matrix::multiply(m,&matrix::frustum(v[0],v[1],v[2],v[3],v[4],v[5]))},
  107|108=>{if v[0]==v[1]||v[2]==v[3]||v[4]==v[5]{return Err(GL_INVALID_VALUE);}matrix::multiply(m,&matrix::ortho(v[0],v[1],v[2],v[3],v[4],v[5]))},
  _=>return Err(GL_INVALID_OPERATION),
 };*m=next;Ok(())
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn normals_use_inverse_transpose_and_indices_are_bounded(){
  let m=matrix::scale(2.,4.,8.);let(p,n)=blend(&[m],[1.,2.,3.,1.],Some([1.,1.,1.]),&[1.],&[0]).unwrap();assert_eq!(p,[2.,8.,24.,1.]);assert_eq!(n,[0.5,0.25,0.125]);assert!(blend(&[m],[0.;4],None,&[1.],&[1]).is_err());
 }
}
