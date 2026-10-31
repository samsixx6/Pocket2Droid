use p2d_gles_host::Bridge;
use pocket_gles::*;

fn cmd(op:u32,args:&[u32],payload:&[u8])->Vec<u8>{
    let mut words=[0u32;18];words[0]=op;words[1..1+args.len()].copy_from_slice(args);words[17]=payload.len() as u32;
    let mut out:Vec<u8>=words.iter().flat_map(|v|v.to_le_bytes()).collect();out.extend(payload);while out.len()%4!=0{out.push(0);}out
}
fn packet(commands:Vec<Vec<u8>>)->Vec<u8>{
    let size=24+commands.iter().map(Vec::len).sum::<usize>();
    let mut out:Vec<u8>=[0x314c4750,17,commands.len() as u32,32,32,size as u32].iter().flat_map(|v|v.to_le_bytes()).collect();for c in commands{out.extend(c);}out
}
fn triangle()->Vec<Vec<u8>>{
    let vertices:Vec<u8>=[-0.8f32,-0.8,0.,0.8,-0.8,0.,0.,0.8,0.].iter().flat_map(|v|v.to_le_bytes()).collect();
    vec![cmd(0x10000,&[0x1000,36],&vertices),cmd(139,&[3,GL_FLOAT,0,0x1000],&[]),cmd(66,&[GL_VERTEX_ARRAY],&[]),cmd(48,&[0,65536,0,65536],&[]),cmd(63,&[GL_TRIANGLES,0,3],&[])]
}
#[test]
fn material_packet_preserves_following_unlit_triangle(){
    let mut b=Bridge::default();let mut commands=vec![cmd(98,&[0x408,0x1202,32768,65536,0xffff0000,131072],&[])];commands.extend(triangle());
    let response=b.execute(&packet(commands)).expect("valid material must not reject the draw packet");
    assert_eq!(&response[20..24],&0u32.to_le_bytes());let center=24+(16*32+16)*2;assert_eq!(&response[center..center+2],&0x07e0u16.to_le_bytes());assert_eq!(b.counters.draws,1);
}
#[test]
fn invalid_material_reports_gl_error_and_keeps_draw(){
    let cases=[(98,0x404,0x1202,0,GL_INVALID_ENUM),(98,0x405,0x1202,0,GL_INVALID_ENUM),(98,0x408,0x1603,0,GL_INVALID_ENUM),(95,0x408,0x1202,0,GL_INVALID_ENUM),(97,0x408,0x1200,0,GL_INVALID_ENUM),(97,0x408,0x1601,0xffff0000,GL_INVALID_VALUE),(98,0x408,0x1601,129*65536,GL_INVALID_VALUE),(95,0x408,0x1601,(-1.0f32).to_bits(),GL_INVALID_VALUE),(96,0x408,0x1601,129.0f32.to_bits(),GL_INVALID_VALUE)];
    for(op,face,pname,value,want)in cases{let mut b=Bridge::default();let mut commands=vec![cmd(op,&[face,pname,value],&[])];commands.extend(triangle());let response=b.execute(&packet(commands)).unwrap();assert_eq!(&response[20..24],&want.to_le_bytes());assert_eq!(b.counters.draws,1);}
}
#[test]
fn material_support_does_not_admit_lighting_or_unknown_commands(){
    for commands in [vec![cmd(0xfeed,&[],&[])],vec![cmd(98,&[0x408,0x1601,65536],&[]),cmd(65,&[GL_LIGHTING],&[])]]{let mut b=Bridge::default();assert!(b.execute(&packet(commands)).is_err());assert_eq!(b.counters.draws,0);}
}
#[test]
fn material_values_persist_across_packets_without_clamping_or_aliasing(){
    let mut b=Bridge::default();assert_eq!(b.material.ambient,[0.2,0.2,0.2,1.]);assert_eq!(b.material.diffuse,[0.8,0.8,0.8,1.]);assert_eq!(b.material.specular,[0.,0.,0.,1.]);assert_eq!(b.material.emission,[0.,0.,0.,1.]);assert_eq!(b.material.shininess,0.);
    for op in [95,96,97,98]{let value=if op<97{128.0f32.to_bits()}else{128*65536};let response=b.execute(&packet(vec![cmd(op,&[0x408,0x1601,value],&[])])).unwrap();assert_eq!(&response[20..24],&0u32.to_le_bytes());assert_eq!(b.material.shininess,128.);}
    for pname in [0x1200,0x1201,0x1202,0x1600,0x1602]{b.execute(&packet(vec![cmd(98,&[0x408,pname,32768,65536,0xffff0000,131072],&[])])).unwrap();let expected=[0.5,1.,-1.,2.];match pname{0x1200=>assert_eq!(b.material.ambient,expected),0x1201=>assert_eq!(b.material.diffuse,expected),0x1202=>assert_eq!(b.material.specular,expected),0x1600=>assert_eq!(b.material.emission,expected),0x1602=>{assert_eq!(b.material.ambient,expected);assert_eq!(b.material.diffuse,expected);},_=>unreachable!()}}
    b.execute(&packet(vec![cmd(96,&[0x408,0x1600,(-0.5f32).to_bits(),0.25f32.to_bits(),1.0f32.to_bits(),2.0f32.to_bits()],&[])])).unwrap();assert_eq!(b.material.emission,[-0.5,0.25,1.,2.]);
    let expected=b.material.clone();b.execute(&packet(vec![])).unwrap();assert_eq!(b.material,expected);assert_eq!(Bridge::default().material.shininess,0.);
}
#[test]
fn invalid_material_leaves_retained_state_and_error_precedence_intact(){
    let mut b=Bridge::default();b.execute(&packet(vec![cmd(97,&[0x408,0x1601,8*65536],&[])])).unwrap();let expected=b.material.clone();
    let response=b.execute(&packet(vec![cmd(98,&[0x404,0x1601,0xffff0000],&[]),cmd(97,&[0x408,0x1601,129*65536],&[])])).unwrap();assert_eq!(&response[20..24],&GL_INVALID_ENUM.to_le_bytes());assert_eq!(b.material,expected);
    for value in [f32::NAN,f32::INFINITY,-1.,129.]{let response=b.execute(&packet(vec![cmd(96,&[0x408,0x1601,value.to_bits()],&[])])).unwrap();assert_eq!(&response[20..24],&GL_INVALID_VALUE.to_le_bytes());assert_eq!(b.material,expected);}
    b.execute(&packet(vec![cmd(95,&[0x408,0x1601,(-0.0f32).to_bits()],&[])])).unwrap();assert_eq!(b.material.shininess,0.);
    let expected=b.material.clone();assert!(b.execute(&packet(vec![cmd(97,&[0x408,0x1601,65536],&[]),cmd(0xfeed,&[],&[])])).is_err());assert_eq!(b.material,expected);
}
