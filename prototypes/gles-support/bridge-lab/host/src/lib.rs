//! Host-only file protocol adapter; all geometry/pixels use PocketHLE.
use pocket_gles::{context::{Context, GuestMemory}, *};
pub use pocket_gles::texture as texture_codec;
pub use pocket_gles::palette as palette_math;
use serde::Serialize;
use std::collections::{BTreeMap, HashMap};
use std::io::Write;

pub const REQUEST_MAGIC:u32=0x314c4750;
pub const RESPONSE_MAGIC:u32=0x31524750;
pub const MAX_REQUEST:usize=16*1024*1024;
pub const OWNERSHIP_MARKER:&str="P2D_GLES_BRIDGE_LAB_V1";
const MAX_DIM:u32=2048;
const MAX_DRAW:u32=65536;
const MAX_TEXTURE_BYTES:usize=128*1024*1024;

pub struct Record<'a>{pub opcode:u32,pub args:[u32;16],pub payload:&'a[u8]}
pub struct Packet<'a>{pub seq:u32,pub width:u32,pub height:u32,pub records:Vec<Record<'a>>}
fn word(b:&[u8],o:usize)->u32{u32::from_le_bytes(b[o..o+4].try_into().unwrap())}
fn supported(op:u32)->bool{matches!(op,35..=51|55..=77|80|82|86|91..=93|95..=108|114..=118|121..=124|128..=131|133..=135|137..=140|144|147|148|150|151|0x10000)}
fn query_count(p:u32)->usize{match p{0xba6..=0xba8=>16,0xb00|0xba2=>4,0xb70=>2,0xd33|0xd31|0x84e2=>1,_=>0}}
pub fn parse(b:&[u8])->Result<Packet<'_>,String>{
    if !(24..=MAX_REQUEST).contains(&b.len()){return Err("request length outside limits".into());}
    if word(b,0)!=REQUEST_MAGIC||word(b,20) as usize!=b.len(){return Err("bad request magic/total_bytes".into());}
    let count=word(b,8) as usize;
    let (width,height)=(word(b,12),word(b,16));
    if width==0||height==0||width>MAX_DIM||height>MAX_DIM{return Err("frame dimensions outside limits".into());}
    if count>32768||count>(b.len()-24)/72{return Err("record count outside packet limits".into());}
    let mut records=Vec::with_capacity(count);
    let mut off=24usize;
    for _ in 0..count{
        let head=off.checked_add(72).ok_or("record header overflow")?;
        if head>b.len(){return Err("truncated record header".into());}
        let opcode=word(b,off);
        if !supported(opcode){return Err(format!("unsupported opcode 0x{opcode:x} ({})",op_name(opcode)));}
        let mut args=[0u32;16];
        for(i,a)in args.iter_mut().enumerate(){*a=word(b,off+4+i*4);}
        let len=word(b,off+68) as usize;
        let end=head.checked_add(len).ok_or("payload overflow")?;
        let aligned=end.checked_add(3).ok_or("padding overflow")?&!3;
        if aligned>b.len(){return Err("truncated record payload".into());}
        match opcode{
            0x10000=>if args[1] as usize!=len||args[0] as u64+len as u64>0x1_0000_0000{return Err("snapshot length/address invalid".into());},
            56=>if args[0]>4096||len!=args[0] as usize*4{return Err("texture delete payload invalid".into());},
            133=>if args[3]>MAX_DIM||args[4]>MAX_DIM{return Err("texture dimensions outside limits".into());},
            51=>if args[3]>MAX_DIM||args[4]>MAX_DIM||args[6] as usize!=len||texture::compressed_image_size(args[2],args[3],args[4]).is_some_and(|n|n>len){return Err("compressed texture payload invalid".into());},
            147=>if len!=0||records.len()+1!=count||query_count(args[0])==0{return Err("invalid/nonterminal float query".into());},
            _=>if len!=0{return Err(format!("unexpected payload for {}",op_name(opcode)));}
        }
        records.push(Record{opcode,args,payload:&b[head..end]});off=aligned;
    }
    if off!=b.len(){return Err("trailing bytes after records".into());}
    Ok(Packet{seq:word(b,4),width,height,records})
}
pub fn op_name(op:u32)->String{
    if op==0x10000{"guest_snapshot".into()}else{u16::try_from(op).ok().and_then(|v|lookup("libgles_cm.dll",v)).unwrap_or_else(||format!("unknown_{op:x}"))}
}
struct Snapshot{base:u32,bytes:Vec<u8>}
#[derive(Default)]
struct Memory{snapshots:Vec<Snapshot>,failed_reads:u64,reads:u64,bytes:u64}
impl GuestMemory for Memory{
    fn read(&mut self,addr:u32,len:usize,out:&mut Vec<u8>)->bool{
        self.reads+=1;
        let end=addr as u64+len as u64;
        if len>MAX_REQUEST||end>0x1_0000_0000{self.failed_reads+=1;return false;}
        out.clear();let mut cur=addr as u64;
        while cur<end{
            let found=self.snapshots.iter().enumerate().rev().find(|(_,s)|cur>=s.base as u64&&cur<s.base as u64+s.bytes.len() as u64);
            let Some((i,s))=found else{self.failed_reads+=1;out.clear();return false;};
            let mut until=end.min(s.base as u64+s.bytes.len() as u64);
            for newer in &self.snapshots[i+1..]{if newer.base as u64>cur{until=until.min(newer.base as u64);}}
            let off=(cur-s.base as u64) as usize;
            out.extend_from_slice(&s.bytes[off..off+(until-cur) as usize]);cur=until;
        }
        self.bytes+=len as u64;true
    }
}
#[derive(Default,Serialize)]
pub struct Counters{
    pub batches:u64,pub records:u64,pub draws:u64,
    /// Before clipping/culling; not visible triangles or pixels.
    pub submitted_triangles:u64,pub frames_with_draws:u64,
    pub texture_uploads:u64,pub texture_upload_bytes:u64,
    pub guest_reads:u64,pub guest_read_bytes:u64,pub missing_guest_reads:u64,
    pub failures:u64,pub last_sequence:u32,pub last_gl_error:u32,
    pub last_error:Option<String>,pub operations:BTreeMap<String,u64>,
}
/// GLES1 permits only FRONT_AND_BACK, so both faces share these retained values.
/// The CPU adapter rejects enabling lighting; no admitted draw consumes this
/// state. Retaining it permits valid unlit material calls without changing the
/// sealed vendor rasterizer or claiming CPU lighting support.
#[derive(Clone,Debug,PartialEq)]
pub struct MaterialState{pub ambient:[f32;4],pub diffuse:[f32;4],pub specular:[f32;4],pub emission:[f32;4],pub shininess:f32}
impl Default for MaterialState{fn default()->Self{Self{ambient:[0.2,0.2,0.2,1.],diffuse:[0.8,0.8,0.8,1.],specular:[0.,0.,0.,1.],emission:[0.,0.,0.,1.],shininess:0.}}}
impl MaterialState{
    fn set(&mut self,op:u32,a:&[u32;16])->Result<(),u32>{
        let vector=op==96||op==98;let shininess=a[1]==0x1601;
        if a[0]!=0x408||(!shininess&&(!vector||!matches!(a[1],0x1200|0x1201|0x1202|0x1600|0x1602))){return Err(GL_INVALID_ENUM);}
        let is_fixed=op==97||op==98;
        if shininess{let value=decoded(a[2],is_fixed);if !(0.0..=128.0).contains(&value){return Err(GL_INVALID_VALUE);}self.shininess=value;}
        else{let values=std::array::from_fn(|i|decoded(a[i+2],is_fixed));match a[1]{0x1200=>self.ambient=values,0x1201=>self.diffuse=values,0x1202=>self.specular=values,0x1600=>self.emission=values,0x1602=>{self.ambient=values;self.diffuse=values;},_=>unreachable!()}}
        Ok(())
    }
}
pub struct Bridge{pub context:Context,pub counters:Counters,pub material:MaterialState,texture_sizes:HashMap<(u32,u32),usize>,memory:Memory,query:Option<(u32,Vec<f32>)>}
impl Default for Bridge{fn default()->Self{Self{context:Context::new(1,1),counters:Counters::default(),material:MaterialState::default(),texture_sizes:HashMap::new(),memory:Memory::default(),query:None}}}
fn fixed(v:u32)->f32{fixed_to_f32(v as i32)}
fn decoded(v:u32,is_fixed:bool)->f32{if is_fixed{fixed(v)}else{f32::from_bits(v)}}
fn triangles(mode:u32,n:u32)->u64{match mode{GL_TRIANGLES=>(n/3) as u64,GL_TRIANGLE_STRIP|GL_TRIANGLE_FAN=>n.saturating_sub(2) as u64,_=>0}}
impl Bridge{
    pub fn execute(&mut self,b:&[u8])->Result<Vec<u8>,String>{
        let p=parse(b)?;
        self.query=None;
        if p.width!=self.context.target.width||p.height!=self.context.target.height{
            self.context.target.resize(p.width,p.height);
            if self.counters.batches==0{self.context.set_viewport(0,0,p.width as i32,p.height as i32);}
        }
        let mut mem=std::mem::take(&mut self.memory);let before=self.counters.draws;
        mem.failed_reads=0;mem.reads=0;mem.bytes=0;
        for r in &p.records{
            self.counters.records+=1;*self.counters.operations.entry(op_name(r.opcode)).or_default()+=1;
            if let Err(e)=self.dispatch(r,&mut mem){self.memory=mem;return Err(e);}
        }
        self.counters.guest_reads+=mem.reads;self.counters.guest_read_bytes+=mem.bytes;
        self.counters.missing_guest_reads+=mem.failed_reads;
        if mem.failed_reads!=0{self.context.set_error(GL_INVALID_OPERATION);}
        self.memory=mem;
        self.counters.batches+=1;if self.counters.draws!=before{self.counters.frames_with_draws+=1;}
        self.counters.last_sequence=p.seq;
        let e=self.context.take_error();self.counters.last_gl_error=e;Ok(self.response(p.seq,e))
    }
    pub fn failure_response(&mut self,seq:u32,why:String)->Vec<u8>{
        self.counters.failures+=1;self.counters.last_error=Some(why);self.counters.last_sequence=seq;
        self.counters.last_gl_error=GL_INVALID_OPERATION;self.query=None;self.response(seq,GL_INVALID_OPERATION)
    }
    fn response(&self,seq:u32,error:u32)->Vec<u8>{
        let t=&self.context.target;let mut out=Vec::with_capacity(24+t.color.len()/2);
        for v in [RESPONSE_MAGIC,seq,t.width,t.height,t.width*t.height*2,error]{out.extend(v.to_le_bytes());}
        for p in t.color.chunks_exact(4){let v=((p[0] as u16>>3)<<11)|((p[1] as u16>>2)<<5)|(p[2] as u16>>3);out.extend(v.to_le_bytes());}
        if let Some((p,v))=&self.query{for x in [0x31514750,*p,v.len() as u32]{out.extend(x.to_le_bytes());}for i in 0..16{out.extend(v.get(i).copied().unwrap_or(0.).to_le_bytes());}}
        out
    }
    /// Original renderer pixels in 32-bit top-down BGRA BMP (alpha retained).
    pub fn write_bmp(&self,mut out:impl Write)->std::io::Result<()>{
        let t=&self.context.target;let n=t.color.len() as u32;
        out.write_all(b"BM")?;out.write_all(&(54+n).to_le_bytes())?;out.write_all(&[0;4])?;out.write_all(&54u32.to_le_bytes())?;
        out.write_all(&40u32.to_le_bytes())?;out.write_all(&(t.width as i32).to_le_bytes())?;out.write_all(&(-(t.height as i32)).to_le_bytes())?;
        out.write_all(&1u16.to_le_bytes())?;out.write_all(&32u16.to_le_bytes())?;out.write_all(&[0;4])?;out.write_all(&n.to_le_bytes())?;out.write_all(&[0;16])?;
        for p in t.color.chunks_exact(4){out.write_all(&[p[2],p[1],p[0],p[3]])?;}Ok(())
    }
    fn dispatch(&mut self,r:&Record<'_>,mem:&mut Memory)->Result<(),String>{
        let a=&r.args;let c=&mut self.context;
        match r.opcode{
            144=>if a[0]>=32{c.set_error(GL_INVALID_VALUE);}else{c.palette_index=a[0];},
            151=>{let m=*c.modelview.current();c.palette_matrices[c.palette_index as usize].load(m);},
            148|150=>{
                if !(1..=4).contains(&a[0])||(a[2] as i32)<0{c.set_error(GL_INVALID_VALUE);return Ok(());}
                if if r.opcode==148{a[1]!=GL_UNSIGNED_BYTE}else{!matches!(a[1],GL_FLOAT|GL_FIXED)}{c.set_error(GL_INVALID_ENUM);return Ok(());}
                let p=if r.opcode==148{&mut c.matrix_index_array}else{&mut c.weight_array};
                *p=pocket_gles::context::ArrayPointer{size:a[0],ty:a[1],stride:a[2],pointer:a[3],enabled:p.enabled,buffer:0};
            },
            147=>{let v=match a[0]{0xba6=>c.modelview.current().to_vec(),0xba7=>c.projection.current().to_vec(),0xba8=>c.texture_matrix.current().to_vec(),0xb00=>c.current_color.to_vec(),0xb70=>vec![c.state.depth_range.0,c.state.depth_range.1],0xba2=>{let p=c.state.viewport;vec![p.0 as f32,p.1 as f32,p.2 as f32,p.3 as f32]},0xd33=>vec![2048.],0xd31=>vec![0.],0x84e2=>vec![c.texture_units.len() as f32],_=>unreachable!()};self.query=Some((a[0],v));},
            51=>{
                if a[0]!=GL_TEXTURE_2D{c.set_error(GL_INVALID_ENUM);return Ok(());}
                if a[1]>11||a[5]!=0||a[3]>(MAX_DIM>>a[1])||a[4]>(MAX_DIM>>a[1]){c.set_error(GL_INVALID_VALUE);return Ok(());}
                let name=c.texture_units[c.active_texture as usize].bound_texture;let key=(name,a[1]);let size=a[3] as usize*a[4] as usize*4;
                let total=self.texture_sizes.values().sum::<usize>()-self.texture_sizes.get(&key).copied().unwrap_or(0)+size;
                if total>MAX_TEXTURE_BYTES{c.set_error(GL_OUT_OF_MEMORY);return Ok(());}
                c.compressed_tex_image_2d(a[0],a[1] as i32,a[3],a[4],a[2],r.payload);
                if name!=0{self.texture_sizes.insert(key,size);}self.counters.texture_uploads+=1;self.counters.texture_upload_bytes+=r.payload.len() as u64;
            },
            82|86=>c.set_error(GL_INVALID_OPERATION), // Lighting is available on the GPU backend only.
            0x10000=>{
                let retained=mem.snapshots.iter().map(|s|s.bytes.len()).sum::<usize>();
                if retained+r.payload.len()>MAX_REQUEST{return Err("pending guest snapshots exceed 16 MiB limit".into());}
                mem.snapshots.push(Snapshot{base:a[0],bytes:r.payload.to_vec()});
            },
            35=>if let Some(unit)=a[0].checked_sub(GL_TEXTURE0){c.set_active_texture(unit);}else{c.set_error(GL_INVALID_ENUM);},
            36|37=>c.set_alpha_func(a[0],decoded(a[1],r.opcode==37)),
            38=>{if c.textures.len()>=4096&&a[1]!=0&&!c.textures.contains_key(&a[1]){return Err("texture object limit reached".into());}c.bind_texture(a[0],a[1]);},
            39=>c.set_blend_func(a[0],a[1]),40=>c.clear(a[0]),
            41|42=>c.clear_color=std::array::from_fn(|i|decoded(a[i],r.opcode==42).clamp(0.,1.)),
            43|44=>c.clear_depth=decoded(a[0],r.opcode==44).clamp(0.,1.),
            45=>return Err("unsupported glClearStencil: no stencil buffer".into()),
            46=>if let Some(unit)=a[0].checked_sub(GL_TEXTURE0){c.set_client_active_texture(unit);}else{c.set_error(GL_INVALID_ENUM);},
            47|48=>c.current_color=std::array::from_fn(|i|decoded(a[i],r.opcode==48).clamp(0.,1.)),
            49=>c.state.color_mask=std::array::from_fn(|i|a[i]!=0),
            50|128|139=>{
                let sz=if r.opcode==50{a[0]==4}else{(2..=4).contains(&a[0])};
                if !sz||(a[2] as i32)<0{c.set_error(GL_INVALID_VALUE);return Ok(());}
                let ty=if r.opcode==50{matches!(a[1],GL_UNSIGNED_BYTE|GL_FIXED|GL_FLOAT)}else{matches!(a[1],GL_BYTE|GL_SHORT|GL_FIXED|GL_FLOAT)};
                if !ty{c.set_error(GL_INVALID_ENUM);return Ok(());}
                match r.opcode{50=>c.set_color_pointer(a[0],a[1],a[2],a[3]),128=>c.set_texcoord_pointer(a[0],a[1],a[2],a[3]),_=>c.set_vertex_pointer(a[0],a[1],a[2],a[3])}
            },
            55=>c.set_cull_face(a[0]),
            56=>{let ids:Vec<u32>=r.payload.chunks_exact(4).map(|b|u32::from_le_bytes(b.try_into().unwrap())).collect();c.delete_textures(&ids);self.texture_sizes.retain(|(id,_),_|!ids.contains(id));},
            57=>c.set_depth_func(a[0]),58=>c.state.depth_write=a[0]!=0,
            59|60=>c.set_depth_range(decoded(a[0],r.opcode==60),decoded(a[1],r.opcode==60)),
            61|65=>{if r.opcode==65&&matches!(a[0],GL_LIGHTING|GL_STENCIL_TEST|GL_POLYGON_OFFSET_FILL|GL_COLOR_MATERIAL){return Err(format!("unsupported enabled renderer capability 0x{:x}",a[0]));}c.set_capability(a[0],r.opcode==65);},
            62|66=>c.set_client_state(a[0],r.opcode==66),
            63|64=>{
                let n=if r.opcode==63{a[2]}else{a[1]};
                if n>MAX_DRAW{return Err("draw count exceeds prototype limit".into());}
                if !matches!(a[0],GL_TRIANGLES|GL_TRIANGLE_STRIP|GL_TRIANGLE_FAN){return Err(format!("unsupported primitive 0x{:x}: renderer only rasterizes triangles",a[0]));}
                if r.opcode==63&&a[1].checked_add(n).is_none(){return Err("draw first/count wraps".into());}
                self.counters.draws+=1;self.counters.submitted_triangles+=triangles(a[0],n);
                if r.opcode==63{c.draw_arrays(mem,a[0],a[1],n);}else{c.draw_elements_from_guest(mem,a[0],n,a[2],a[3]);}
                mem.snapshots.clear();
            },
            67|68=>{}, // Software rendering completes synchronously.
            69..=72=>{let f=r.opcode>=71;if matches!(r.opcode,70|72)&&a[0]==GL_FOG_COLOR{c.state.fog_color=std::array::from_fn(|i|decoded(a[i+1],f));}else{let v=if f&&a[0]==GL_FOG_MODE{a[1] as f32}else{decoded(a[1],f)};c.set_fog(a[0],v);}},
            73=>c.set_front_face(a[0]),
            74|75|107|108=>{let v:[f32;6]=std::array::from_fn(|i|decoded(a[i],r.opcode==75||r.opcode==108));if r.opcode<=75{c.frustum(v[0],v[1],v[2],v[3],v[4],v[5]);}else{c.ortho(v[0],v[1],v[2],v[3],v[4],v[5]);}},
            76=>if a[0]>4096{return Err("guest texture generation count exceeds limit".into());}, // IDs allocated guest-side.
            77=>{}, // Sticky error returned/taken in response header.
            80=>{}, // glHint is advisory; software renderer chooses quality.
            91=>c.load_identity(),
            92|93|100|101=>{let m=std::array::from_fn(|i|decoded(a[i],r.opcode==93||r.opcode==101));if r.opcode<=93{c.load_matrix(m);}else{c.mult_matrix(m);}},
            95..=98=>if let Err(error)=self.material.set(r.opcode,a){c.set_error(error);},
            99=>c.set_matrix_mode(a[0]),
            102|103=>c.set_multi_texcoord(a[0],decoded(a[1],r.opcode==103),decoded(a[2],r.opcode==103)),
            104|105=>c.current_normal=std::array::from_fn(|i|decoded(a[i],r.opcode==105)),
            106=>if(a[1] as i32)<0{c.set_error(GL_INVALID_VALUE);}else{c.set_normal_pointer(a[0],a[1],a[2]);},
            114=>c.pop_matrix(),115=>c.push_matrix(),
            116=>if a[2]>MAX_DIM||a[3]>MAX_DIM{c.set_error(GL_INVALID_VALUE);}, // Guest crops/converts returned framebuffer.
            117|118=>c.rotate(decoded(a[0],r.opcode==118),decoded(a[1],r.opcode==118),decoded(a[2],r.opcode==118),decoded(a[3],r.opcode==118)),
            121|122=>c.scale(decoded(a[0],r.opcode==122),decoded(a[1],r.opcode==122),decoded(a[2],r.opcode==122)),
            123|140=>{if a[..4].iter().any(|&x|(x as i32).unsigned_abs()>65536){return Err("viewport/scissor exceeds safe arithmetic range".into());}if r.opcode==123{c.set_scissor(a[0] as i32,a[1] as i32,a[2] as i32,a[3] as i32);}else{c.set_viewport(a[0] as i32,a[1] as i32,a[2] as i32,a[3] as i32);}},
            124=>c.set_shade_model(a[0]),
            129|131=>if a[0]!=GL_TEXTURE_ENV{c.set_error(GL_INVALID_ENUM);}else{c.tex_env(a[1],if r.opcode==131{a[2]}else{f32::from_bits(a[2]) as u32});},
            130=>if a[0]!=GL_TEXTURE_ENV{c.set_error(GL_INVALID_ENUM);}else if a[1]==GL_TEXTURE_ENV_COLOR{c.set_tex_env_color(std::array::from_fn(|i|f32::from_bits(a[i+2])));}else{c.tex_env(a[1],f32::from_bits(a[2]) as u32);},
            133=>{
                if a[5]!=0||a[1]>11||a[2]!=a[6]{c.set_error(GL_INVALID_VALUE);return Ok(());}
                let bpp=texture::bytes_per_texel(a[6],a[7]).ok_or("unsupported texture format/type")?;
                let needed=a[3] as usize*a[4] as usize*bpp;
                if !r.payload.is_empty()&&needed!=r.payload.len(){return Err("texture payload length mismatch: tightly packed texels required".into());}
                if r.payload.is_empty()&&a[8]!=0&&needed!=0{return Err("non-null texture pointer without payload".into());}
                let name=c.texture_units[c.active_texture as usize].bound_texture;let key=(name,a[1]);let size=a[3] as usize*a[4] as usize*4;
                let total=self.texture_sizes.values().sum::<usize>()-self.texture_sizes.get(&key).copied().unwrap_or(0)+size;
                if total>MAX_TEXTURE_BYTES{return Err("resident texture memory exceeds limit".into());}
                c.tex_image_2d(a[0],a[1] as i32,a[3],a[4],a[6],a[7],r.payload);
                if name!=0{self.texture_sizes.insert(key,size);}self.counters.texture_uploads+=1;self.counters.texture_upload_bytes+=r.payload.len() as u64;
            },
            134|135=>c.tex_parameter(a[0],a[1],if r.opcode==135{a[2]}else{f32::from_bits(a[2]) as u32}),
            137|138=>c.translate(decoded(a[0],r.opcode==138),decoded(a[1],r.opcode==138),decoded(a[2],r.opcode==138)),
            _=>return Err(format!("unsupported opcode 0x{:x}",r.opcode)),
        }Ok(())
    }
}
