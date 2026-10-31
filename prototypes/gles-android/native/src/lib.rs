//! Sequential single-context C ABI around the unchanged CPU software renderer.
use p2d_gles_host::{Bridge, MAX_REQUEST};
use std::{ffi::c_void, mem, panic::{catch_unwind, AssertUnwindSafe}, ptr, slice};

const MAX_RESPONSE: usize = 24 + 2048 * 2048 * 2 + 76;

/// Shared palette ABI. Spans describe live, nonoverlapping input/output storage;
/// callers validate their copied client arrays before entering this boundary.
#[repr(C)]
pub struct PaletteSpan {data:*const u8,bytes:usize,size:u32,ty:u32,stride:u32}
#[no_mangle]
pub unsafe extern "C" fn p2d_gles_palette_matrix(matrix:*mut f32,op:u32,args:*const u32)->u32 {
    if matrix.is_null()||args.is_null()||(matrix as usize)%4!=0||(args as usize)%4!=0{return 0x501;}
    let a=&*(args as *const [u32;16]);let m=&mut*(matrix as *mut [f32;16]);
    p2d_gles_host::palette_math::command(m,op,a).err().unwrap_or(0)
}
fn palette_value(b:&[u8],ty:u32,normal:bool)->Option<f32>{Some(match ty{
    0x1406=>f32::from_le_bytes(b.get(..4)?.try_into().ok()?),
    0x140c=>i32::from_le_bytes(b.get(..4)?.try_into().ok()?) as f32/65536.,
    0x1400=>{let v=*b.first()? as i8 as f32;if normal{(v/127.).max(-1.)}else{v}},
    0x1401=>*b.first()? as f32,
    0x1402=>{let v=i16::from_le_bytes(b.get(..2)?.try_into().ok()?) as f32;if normal{(v/32767.).max(-1.)}else{v}},
    _=>return None})}
/// Weighted vertices and inverse-transpose normals, with explicit span bounds.
/// The optional `used` mask has `count` bytes. A size-zero normal span omits
/// normal processing and permits NULL normal output. Other storage is live,
/// aligned as required, and disjoint from all output storage for the call.
#[no_mangle]
pub unsafe extern "C" fn p2d_gles_palette_blend(matrices:*const f32,spans:*const PaletteSpan,count:u32,positions:*mut f32,normals:*mut f32,used:*const u8)->u32 {
    if matrices.is_null()||spans.is_null()||positions.is_null()||count>65536||(matrices as usize)%4!=0||(spans as usize)%mem::align_of::<PaletteSpan>()!=0||(positions as usize)%4!=0||(normals as usize)%4!=0{return 0x501;}
    let mats=&*(matrices as *const [[f32;16];32]);let spans=slice::from_raw_parts(spans,4);
    // Unlit draws need no inverse-transpose normal, including singular matrices.
    // Indexed draws may omit unused holes in their copied min/max interval.
    let has_normal=spans[1].size!=0;
    if has_normal&&normals.is_null(){return 0x501;}
    let used=if used.is_null(){None}else{Some(slice::from_raw_parts(used,count as usize))};
    if spans[2].size==0||spans[2].size>4||spans[2].size!=spans[3].size||spans[3].ty!=0x1401{return 0x502;}
    let mut views=Vec::with_capacity(4);
    for (j,s) in spans.iter().enumerate(){
        if j==1&&!has_normal{views.push((&[][..],0,0));continue;}
        let c=match s.ty{0x1400|0x1401=>1,0x1402=>2,0x1406|0x140c=>4,_=>return 0x500};
        if s.size==0||s.size>4||s.data.is_null()||s.bytes>MAX_REQUEST||(s.data as usize).checked_add(s.bytes).is_none(){return 0x501;}
        let step=if s.stride==0{s.size*c}else{s.stride} as usize;
        let needed=if count==0{Some(0)}else{(count as usize-1).checked_mul(step).and_then(|n|n.checked_add(s.size as usize*c as usize))};if needed.is_none_or(|n|n>s.bytes){return 0x502;}
        views.push((slice::from_raw_parts(s.data,s.bytes),step,c as usize));
    }
    for i in 0..count as usize{
        if used.is_some_and(|v|v[i]==0){continue;}
        let mut values=[[0.,0.,0.,1.];4];
        for j in 0..4{let(s,step,c)=views[j];for k in 0..spans[j].size as usize{let Some(v)=palette_value(&s[i*step+k*c..],spans[j].ty,j==1)else{return 0x500;};values[j][k]=v;}}
        let ids:[u8;4]=std::array::from_fn(|k|values[3][k] as u8);let units=spans[2].size as usize;
        match p2d_gles_host::palette_math::blend(mats,values[0],has_normal.then_some([values[1][0],values[1][1],values[1][2]]),&values[2][..units],&ids[..units]){
            Ok((p,n))=>{ptr::copy_nonoverlapping(p.as_ptr(),positions.add(i*4),4);if has_normal{ptr::copy_nonoverlapping(n.as_ptr(),normals.add(i*3),3);}},Err(e)=>return e
        }
    }0
}

/// Pure, bounded decoder shared with the GPU adapter. No GL context or renderer
/// state is touched. Valid source/output storage must not alias.
#[no_mangle]
pub unsafe extern "C" fn p2d_gles_decode_compressed(format:u32,width:u32,height:u32,source:*const u8,len:usize,out:*mut u8,capacity:usize)->i32 {
    if source.is_null()||out.is_null()||width==0||height==0||width>2048||height>2048||len>MAX_REQUEST||capacity!=(width as usize*height as usize*4){return -1;}
    let a=source as usize;let b=out as usize;
    if a.checked_add(len).is_none()||b.checked_add(capacity).is_none()||(a<b+capacity&&b<a+len){return -1;}
    if p2d_gles_host::texture_codec::compressed_image_size(format,width,height).is_none_or(|n|n>len){return -1;}
    match catch_unwind(||p2d_gles_host::texture_codec::decode_compressed_to_rgba(slice::from_raw_parts(source,len),width,height,format)) {
        Ok(Some(v)) if v.len()==capacity=>{ptr::copy_nonoverlapping(v.as_ptr(),out,capacity);0},_=>-1
    }
}
struct Native {
    // A caught panic poisons the context: only destroy/recreate may reuse it.
    bridge: Option<Bridge>,
    response: Vec<u8>,
}

fn execute_guarded(native: &mut Native, seq: u32,
                   operation: impl FnOnce(&mut Bridge) -> Result<Vec<u8>, String>) -> i32 {
    native.response.clear();
    let Some(bridge) = native.bridge.as_mut() else { return -2; };
    match catch_unwind(AssertUnwindSafe(|| {
        match operation(bridge) {
            Ok(response) => (0, response),
            Err(why) => (1, bridge.failure_response(seq, why)),
        }
    })) {
        Ok((status, response)) if (24..=MAX_RESPONSE).contains(&response.len()) => {
            native.response = response;
            status
        }
        _ => {
            native.bridge = None;
            -2
        }
    }
}

#[no_mangle]
pub extern "C" fn p2d_gles_create() -> *mut c_void {
    // Upstream optionally writes texture dumps. Never permit that side effect
    // in an embedded renderer, and do not mutate process-global environment.
    if std::env::var_os("POCKETHLE_DUMP_TEXTURES").is_some() { return ptr::null_mut(); }
    catch_unwind(|| Box::into_raw(Box::new(Native {
        bridge: Some(Bridge::default()), response: Vec::new(),
    })) as *mut c_void).unwrap_or(ptr::null_mut())
}

/// # Safety
/// `context` must be NULL or a live handle from create, with no concurrent calls.
#[no_mangle]
pub unsafe extern "C" fn p2d_gles_destroy(context: *mut c_void) {
    if context.is_null() { return; }
    let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(context.cast::<Native>()))));
}

/// # Safety
/// Non-null pointers must designate valid live storage as specified in the C
/// header. No C ABI can validate arbitrary non-null dangling addresses. Calls
/// on a context are sequential; the response must not alias the request/slots.
#[no_mangle]
pub unsafe extern "C" fn p2d_gles_execute(
    context: *mut c_void, request: *const u8, request_size: usize,
    response: *mut *const u8, response_size: *mut usize,
) -> i32 {
    if response.is_null() || response_size.is_null()
        || (response as usize) % mem::align_of::<*const u8>() != 0
        || (response_size as usize) % mem::align_of::<usize>() != 0
        || (response as usize).abs_diff(response_size as usize) < mem::size_of::<usize>() {
        return -1;
    }
    response.write(ptr::null());
    response_size.write(0);
    if context.is_null() || (context as usize) % mem::align_of::<Native>() != 0
        || request.is_null() || !(24..=MAX_REQUEST).contains(&request_size)
        || (request as usize).checked_add(request_size).is_none() { return -1; }
    let native = &mut *context.cast::<Native>();
    // Invalid ABI arguments do not read request bytes or modify renderer state.
    // Even on valid-but-malformed packets the sequence is safely available.
    let input = slice::from_raw_parts(request, request_size);
    let seq = u32::from_le_bytes(input[4..8].try_into().unwrap());
    let status = execute_guarded(native, seq, |bridge| bridge.execute(input));
    if status >= 0 {
        response.write(native.response.as_ptr());
        response_size.write(native.response.len());
    }
    status
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn renderer_panic_is_caught_and_context_is_poisoned_without_output() {
        let mut native = Native { bridge: Some(Bridge::default()), response: vec![1,2,3] };
        assert_eq!(execute_guarded(&mut native, 7, |_| panic!("renderer panic")), -2);
        assert!(native.response.is_empty());
        assert!(native.bridge.is_none());
        assert_eq!(execute_guarded(&mut native, 8, |_| unreachable!()), -2);
    }
    #[test]
    fn palette_unlit_sparse_draw_skips_unused_indices_and_normal_inverse() {
        let mut matrices=[[0f32;16];32];for m in &mut matrices{for i in [0,5,15]{m[i]=1.;}}
        let vertices=[-0.5f32,-0.5,0., 0.,0.,0., 0.5,-0.5,0., 0.,0.,0., 0.,0.5,0.];
        let weights=[1f32;5];let indices=[0u8,255,0,255,0];let used=[1u8,0,1,0,1];
        let spans=[PaletteSpan{data:vertices.as_ptr().cast(),bytes:60,size:3,ty:0x1406,stride:0},PaletteSpan{data:ptr::null(),bytes:0,size:0,ty:0,stride:0},PaletteSpan{data:weights.as_ptr().cast(),bytes:20,size:1,ty:0x1406,stride:0},PaletteSpan{data:indices.as_ptr(),bytes:5,size:1,ty:0x1401,stride:0}];
        let mut out=[99f32;20];unsafe{
            assert_eq!(p2d_gles_palette_blend(matrices.as_ptr().cast(),spans.as_ptr(),5,out.as_mut_ptr(),ptr::null_mut(),used.as_ptr()),0);
            assert_eq!(&out[..4],&[-0.5,-0.5,0.,1.]);assert_eq!(&out[4..8],&[99.;4]);
            assert_eq!(p2d_gles_palette_blend(matrices.as_ptr().cast(),spans.as_ptr(),5,out.as_mut_ptr(),ptr::null_mut(),ptr::null()),0x502);
        }
    }
}
