use p2d_gles_host::Bridge;
use pocket_gles::*;

// The literal CM ordinals intentionally come from the supplied ABI table.
fn cmd(op: u32, args: &[u32], payload: &[u8]) -> Vec<u8> {
    let mut words = [0u32; 18];
    words[0] = op;
    words[1..1 + args.len()].copy_from_slice(args);
    words[17] = payload.len() as u32;
    let mut out: Vec<u8> = words.iter().flat_map(|x| x.to_le_bytes()).collect();
    out.extend_from_slice(payload);
    while out.len() % 4 != 0 { out.push(0); }
    out
}
fn packet(seq: u32, commands: Vec<Vec<u8>>) -> Vec<u8> {
    let size = 24 + commands.iter().map(Vec::len).sum::<usize>();
    let mut out: Vec<u8> = [0x314c4750, seq, commands.len() as u32, 32, 32, size as u32]
        .iter().flat_map(|x| x.to_le_bytes()).collect();
    for c in commands { out.extend(c); }
    out
}
fn float_bytes(v: &[f32]) -> Vec<u8> { v.iter().flat_map(|x| x.to_le_bytes()).collect() }
fn pixel(frame: &[u8], x: usize, y: usize) -> u16 {
    let off = 24 + (y * 32 + x) * 2;
    u16::from_le_bytes([frame[off], frame[off + 1]])
}
fn triangle() -> Vec<Vec<u8>> {
    vec![
        cmd(0x10000, &[0x1000, 36], &float_bytes(&[-0.8,-0.8,0., 0.8,-0.8,0., 0.,0.8,0.])),
        cmd(139, &[3, GL_FLOAT, 0, 0x1000], &[]),
        cmd(66, &[GL_VERTEX_ARRAY], &[]),
    ]
}

#[test]
fn clear_packet_returns_exact_rgb565_and_sequence() {
    let mut b = Bridge::default();
    let frame = b.execute(&packet(9, vec![cmd(42, &[65536,0,0,65536], &[]), cmd(40, &[GL_COLOR_BUFFER_BIT], &[])])).unwrap();
    assert_eq!(&frame[..24], &[0x50,0x47,0x52,0x31, 9,0,0,0, 32,0,0,0, 32,0,0,0, 0,8,0,0, 0,0,0,0]);
    assert_eq!(frame.len(), 2072);
    assert_eq!(pixel(&frame, 16, 16), 0xf800);
}
#[test]
fn malformed_packet_is_rejected_before_clear_changes_state() {
    let mut b = Bridge::default();
    let mut p = packet(1, vec![cmd(42, &[0,65536,0,65536], &[]),cmd(40,&[GL_COLOR_BUFFER_BIT],&[])]);
    p.pop();
    assert!(b.execute(&p).is_err());
    let frame=b.execute(&packet(2, vec![])).unwrap();
    assert_eq!(pixel(&frame,16,16),0);
}
#[test]
fn triangle_uses_guest_snapshot_and_fixed_color() {
    let mut b=Bridge::default();
    let mut c=triangle();
    c.extend([cmd(48,&[0,65536,0,65536],&[]),cmd(63,&[GL_TRIANGLES,0,3],&[])]);
    let frame=b.execute(&packet(3,c)).unwrap();
    assert_eq!(pixel(&frame,16,16),0x07e0);
    assert_eq!(pixel(&frame,0,0),0);
}
#[test]
fn texture_upload_persists_across_batches_and_is_sampled() {
    let mut b=Bridge::default();
    b.execute(&packet(1,vec![cmd(38,&[GL_TEXTURE_2D,17],&[]),cmd(133,&[GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,0x9000],&[0,0,255,255]),cmd(135,&[GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST],&[])])).unwrap();
    let mut c=triangle();
    c.extend([cmd(65,&[GL_TEXTURE_2D],&[]),cmd(131,&[GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE],&[]),cmd(63,&[GL_TRIANGLES,0,3],&[])]);
    let frame=b.execute(&packet(2,c)).unwrap();
    assert_eq!(pixel(&frame,16,16),0x001f);
}
#[test]
fn later_overlapping_snapshot_overrides_old_vertex_bytes() {
    let mut b=Bridge::default();
    let mut c=triangle();
    // Replace final Y with -0.8, making all three vertices collinear.
    c.push(cmd(0x10000,&[0x101c,4],&(-0.8f32).to_le_bytes()));
    c.push(cmd(63,&[GL_TRIANGLES,0,3],&[]));
    let frame=b.execute(&packet(1,c)).unwrap();
    assert_eq!(pixel(&frame,16,16),0);
}
#[test]
fn unknown_opcode_is_explicit_failure() {
    let mut b=Bridge::default();
    assert!(b.execute(&packet(1,vec![cmd(0xdead,&[],&[])])).unwrap_err().contains("unsupported opcode"));
}
#[test]
fn indexed_draw_reads_ushort_indices_from_snapshots() {
    let mut b=Bridge::default();
    let mut c=triangle();
    c.extend([cmd(0x10000,&[0x3000,6],&[0,0,1,0,2,0]),cmd(64,&[GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0x3000],&[])]);
    let frame=b.execute(&packet(1,c)).unwrap();
    assert_eq!(pixel(&frame,16,16),0xffff);
}
#[test]
fn scissor_gl_bottom_origin_becomes_top_row_response() {
    let mut b=Bridge::default();
    let frame=b.execute(&packet(1,vec![cmd(42,&[65536,0,0,65536],&[]),cmd(65,&[GL_SCISSOR_TEST],&[]),cmd(123,&[0,16,32,16],&[]),cmd(40,&[GL_COLOR_BUFFER_BIT],&[])])).unwrap();
    assert_eq!(pixel(&frame,16,0),0xf800);
    assert_eq!(pixel(&frame,16,31),0);
}
#[test]
fn rejects_oversized_dimensions_and_snapshot_address_wrap() {
    let mut b=Bridge::default();
    let mut p=packet(1,vec![]);
    p[12..16].copy_from_slice(&0xffffffffu32.to_le_bytes());
    assert!(b.execute(&p).is_err());
    assert!(b.execute(&packet(2,vec![cmd(0x10000,&[0xfffffffe,4],&[0;4])])).is_err());
}
#[test]
fn missing_snapshot_is_an_error_instead_of_successful_empty_draw() {
    let mut b=Bridge::default();
    let frame=b.execute(&packet(1,vec![cmd(139,&[3,GL_FLOAT,0,0x1000],&[]),cmd(66,&[GL_VERTEX_ARRAY],&[]),cmd(63,&[GL_TRIANGLES,0,3],&[])])).unwrap();
    assert_eq!(&frame[20..24],&GL_INVALID_OPERATION.to_le_bytes());
}
#[test]
fn snapshot_from_previous_transport_batch_survives_until_draw() {
    let mut b=Bridge::default();
    let mut c=triangle();let snapshot=c.remove(0);
    b.execute(&packet(1,vec![snapshot])).unwrap();
    c.push(cmd(63,&[GL_TRIANGLES,0,3],&[]));
    let frame=b.execute(&packet(2,c)).unwrap();
    assert_eq!(pixel(&frame,16,16),0xffff);
    assert_eq!(&frame[20..24],&0u32.to_le_bytes());
    // Every subsequent draw needs its new guest snapshot, preventing stale reuse.
    let stale=b.execute(&packet(3,vec![cmd(63,&[GL_TRIANGLES,0,3],&[])])).unwrap();
    assert_eq!(&stale[20..24],&GL_INVALID_OPERATION.to_le_bytes());
}
#[test]
fn partial_arrays_split_across_transport_batches_are_combined_for_one_draw() {
    let mut b=Bridge::default();
    let mut c=triangle();let snapshot=c.remove(0);
    b.execute(&packet(1,vec![snapshot])).unwrap();
    c.extend([cmd(0x10000,&[0x2000,12],&[255,0,0,255,255,0,0,255,255,0,0,255]),cmd(50,&[4,GL_UNSIGNED_BYTE,0,0x2000],&[]),cmd(66,&[GL_COLOR_ARRAY],&[]),cmd(63,&[GL_TRIANGLES,0,3],&[])]);
    let frame=b.execute(&packet(2,c)).unwrap();
    assert_eq!(pixel(&frame,16,16),0xf800);
    assert_eq!(&frame[20..24],&0u32.to_le_bytes());
}
#[test]
fn fixed_fog_mode_remains_enum_while_color_is_fixed_vector() {
    let mut b=Bridge::default();
    let vertices=float_bytes(&[-0.8,-0.8,-0.5, 0.8,-0.8,-0.5, 0.,0.8,-0.5]);
    let c=vec![cmd(0x10000,&[0x1000,36],&vertices),cmd(139,&[3,GL_FLOAT,0,0x1000],&[]),cmd(66,&[GL_VERTEX_ARRAY],&[]),cmd(48,&[65536,0,0,65536],&[]),cmd(71,&[GL_FOG_MODE,GL_LINEAR],&[]),cmd(71,&[GL_FOG_START,0],&[]),cmd(71,&[GL_FOG_END,65536],&[]),cmd(72,&[GL_FOG_COLOR,0,0,65536,65536],&[]),cmd(65,&[GL_FOG],&[]),cmd(63,&[GL_TRIANGLES,0,3],&[])];
    let f=b.execute(&packet(1,c)).unwrap();
    assert_eq!(&f[20..24],&0u32.to_le_bytes());
    assert_eq!(pixel(&f,16,16),0x8010);
}
#[test]
fn inline_fixed_matrix_moves_triangle_then_identity_restores_it() {
    let mut b=Bridge::default();
    let mut m=[0;16];for i in [0,5,10,15]{m[i]=65536;}m[12]=131072;
    let mut c=vec![cmd(99,&[GL_MODELVIEW],&[]),cmd(93,&m,&[])];c.extend(triangle());c.push(cmd(63,&[GL_TRIANGLES,0,3],&[]));
    let moved=b.execute(&packet(1,c)).unwrap();assert_eq!(pixel(&moved,16,16),0);
    let mut c=vec![cmd(91,&[],&[])];c.extend(triangle());c.push(cmd(63,&[GL_TRIANGLES,0,3],&[]));
    let restored=b.execute(&packet(2,c)).unwrap();assert_eq!(pixel(&restored,16,16),0xffff);
}
#[test]
fn all_cod2_imported_gl_operations_have_dispatch_handlers() {
    let mut b=Bridge::default();let negative=(-65536i32) as u32;
    let mut identity=[0;16];for i in [0,5,10,15]{identity[i]=65536;}
    let mut c=vec![cmd(99,&[GL_PROJECTION],&[]),cmd(91,&[],&[]),cmd(75,&[negative,65536,negative,65536,65536,524288],&[]),cmd(93,&identity,&[]),cmd(124,&[GL_SMOOTH],&[]),cmd(140,&[0,0,32,32],&[]),cmd(42,&[0,0,0,65536],&[]),cmd(40,&[GL_COLOR_BUFFER_BIT],&[]),cmd(35,&[GL_TEXTURE0],&[]),cmd(46,&[GL_TEXTURE0],&[]),cmd(76,&[1],&[]),cmd(38,&[GL_TEXTURE_2D,9],&[]),cmd(133,&[GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,0x5000],&[0,255,0,255]),cmd(135,&[GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST],&[]),cmd(131,&[GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE],&[]),cmd(71,&[GL_FOG_MODE,GL_LINEAR],&[]),cmd(72,&[GL_FOG_COLOR,0,0,0,65536],&[]),cmd(37,&[GL_ALWAYS,0],&[]),cmd(55,&[GL_BACK],&[]),cmd(80,&[0xc54,0x1100],&[]),cmd(61,&[GL_FOG],&[]),cmd(65,&[GL_TEXTURE_2D],&[]),cmd(50,&[4,GL_UNSIGNED_BYTE,0,0x6000],&[]),cmd(128,&[2,GL_FLOAT,0,0x7000],&[]),cmd(62,&[GL_COLOR_ARRAY],&[])];
    c.extend(triangle());c.push(cmd(0x10000,&[0x3000,6],&[0,0,1,0,2,0]));c.push(cmd(64,&[GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0x3000],&[]));c.push(cmd(56,&[1],&9u32.to_le_bytes()));
    let mut f=Vec::new();
    for (i,command) in c.into_iter().enumerate(){
        let op=u32::from_le_bytes(command[..4].try_into().unwrap());
        f=b.execute(&packet(i as u32+1,vec![command])).unwrap();
        assert_eq!(&f[20..24],&0u32.to_le_bytes(),"opcode {op} {}",p2d_gles_host::op_name(op));
    }
    assert_eq!(pixel(&f,16,16),0x07e0);
    assert_eq!(b.counters.texture_uploads,1);assert_eq!(b.counters.submitted_triangles,1);
    assert!(b.context.textures.is_empty());
}
