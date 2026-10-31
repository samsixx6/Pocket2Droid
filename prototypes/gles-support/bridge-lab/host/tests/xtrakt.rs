use p2d_gles_host::Bridge;
fn packet(records:&[(u32,[u32;16],Vec<u8>)])->Vec<u8>{
 let mut p=Vec::new();for x in [0x314c4750,1,records.len() as u32,4,4,0]{p.extend(x.to_le_bytes());}
 for (op,a,data) in records {p.extend(op.to_le_bytes());for x in a{p.extend(x.to_le_bytes());}p.extend((data.len() as u32).to_le_bytes());p.extend(data);while p.len()%4!=0{p.push(0);}}
 let n=p.len() as u32;p[20..24].copy_from_slice(&n.to_le_bytes());p
}
fn args(v:&[u32])->[u32;16]{let mut a=[0;16];a[..v.len()].copy_from_slice(v);a}
#[test] fn palette_weighted_vertices_match_explicit_eye_coordinates(){
 let vertices=[-0.6f32,-0.5,0.,0.1,-0.5,0.,-0.25,0.5,0.];
 let bytes=|v:&[f32]|v.iter().flat_map(|x|x.to_le_bytes()).collect::<Vec<_>>();
 let weights=[0.75f32,0.25,0.75,0.25,0.75,0.25];
 let mut m0=[0u32;16];for i in [0,5,10,15]{m0[i]=1f32.to_bits();}m0[12]=0.5f32.to_bits();let mut m1=m0;m1[12]=(-0.5f32).to_bits();
 let mut setup=vec![(0x10000,args(&[0x1000,36]),bytes(&vertices)),(139,args(&[3,0x1406,0,0x1000]),vec![]),(66,args(&[0x8074]),vec![])];
 let mut p=setup.clone();p.extend([(0x10000,args(&[0x2000,24]),bytes(&weights)),(0x10000,args(&[0x3000,6]),vec![0,1,0,1,0,1]),(150,args(&[2,0x1406,0,0x2000]),vec![]),(148,args(&[2,0x1401,0,0x3000]),vec![]),(66,args(&[0x86ad]),vec![]),(66,args(&[0x8844]),vec![]),(65,args(&[0x8840]),vec![]),(99,args(&[0x8840]),vec![]),(144,args(&[0]),vec![]),(92,m0,vec![]),(144,args(&[1]),vec![]),(92,m1,vec![]),(99,args(&[0x1700]),vec![]),(137,args(&[0.7f32.to_bits(),0,0]),vec![]),(63,args(&[4,0,3]),vec![])]);
 let mut expected=vertices;for i in [0,3,6]{expected[i]+=0.25;}
 setup[0]=(0x10000,args(&[0x1000,36]),bytes(&expected));setup.push((63,args(&[4,0,3]),vec![]));
 let a=Bridge::default().execute(&packet(&p)).expect("palette packet supported");let b=Bridge::default().execute(&packet(&setup)).unwrap();assert_eq!(a,b);assert!(a[24..].iter().any(|x|*x!=0));
}
#[test] fn dxt1_rgb_black_is_opaque_and_rgba_black_is_transparent(){
 let block=[0,0,0,0,255,255,255,255];
 let rgb=p2d_gles_host::texture_codec::decode_compressed_to_rgba(&block,4,4,0x83f0).unwrap();
 let rgba=p2d_gles_host::texture_codec::decode_compressed_to_rgba(&block,4,4,0x83f1).unwrap();
 assert!(rgb.chunks_exact(4).all(|p|p==[0,0,0,255]));assert!(rgba.chunks_exact(4).all(|p|p==[0,0,0,0]));
}
#[test] fn matrix_readback_is_real_and_does_not_change_pixels(){
 let mut b=Bridge::default();let p=packet(&[(99,args(&[0x1700]),vec![]),(137,args(&[2f32.to_bits(),3f32.to_bits(),4f32.to_bits()]),vec![]),(147,args(&[0xba6]),vec![])]);
 let r=b.execute(&p).expect("matrix query supported");assert_eq!(r.len(),24+32+76);
 let tail=&r[56..];assert_eq!(u32::from_le_bytes(tail[..4].try_into().unwrap()),0x31514750);
 let f=|i:usize|f32::from_le_bytes(tail[12+i*4..16+i*4].try_into().unwrap());
 assert_eq!((f(0),f(5),f(10),f(15)),(1.,1.,1.,1.));assert_eq!((f(12),f(13),f(14)),(2.,3.,4.));
 let plain=b.execute(&packet(&[])).unwrap();assert_eq!(&r[..56],&plain[..]);
}
#[test] fn compressed_dxt_upload_decodes_and_rejects_truncation_before_state_changes(){
 let mut b=Bridge::default();let block=vec![0,248,0,0,0,0,0,0];
 let p=packet(&[(38,args(&[0xde1,1]),vec![]),(51,args(&[0xde1,0,0x83f0,4,4,0,8]),block)]);
 let r=b.execute(&p).expect("compressed upload supported");assert_eq!(u32::from_le_bytes(r[20..24].try_into().unwrap()),0);
 assert_eq!(&b.context.textures[&1].rgba[..4],&[255,0,0,255]);
 // Xtrakt reports an allocation-sized imageSize for some ATC images. The
 // shared decoder consumes the required prefix and ignores bounded spare bytes.
 let padded=packet(&[(51,args(&[0xde1,0,0x8c93,4,4,0,32]),vec![255;32])]);
 assert!(b.execute(&padded).is_ok(),"bounded allocation-sized ATC upload");
 let bad=packet(&[(41,args(&[1f32.to_bits(),0,0,0]),vec![]),(51,args(&[0xde1,0,0x83f0,4,4,0,8]),vec![0;7])]);
 let before=b.context.clear_color;assert!(b.execute(&bad).is_err());assert_eq!(b.context.clear_color,before);
}

