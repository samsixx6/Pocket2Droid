use p2d_gles_host::{Bridge,MAX_REQUEST,OWNERSHIP_MARKER};
use std::{fs::{self,File},io::{Read,Write},path::{Path,PathBuf},time::{Duration,Instant},thread};

fn bounded_read(path:&Path,max:usize)->Result<Vec<u8>,String>{
    bounded_read_io(path,max).map_err(|e|format!("read {}: {e}",path.display()))
}
fn bounded_read_io(path:&Path,max:usize)->std::io::Result<Vec<u8>>{
    use std::io::{Error,ErrorKind};
    let f=File::open(path)?;
    let metadata=f.metadata()?;
    if !metadata.is_file(){return Err(Error::new(ErrorKind::InvalidInput,"RPC input must be a regular file"));}
    if metadata.len()>max as u64{return Err(Error::new(ErrorKind::InvalidData,"RPC input exceeds file size limit"));}
    let mut bytes=Vec::new();f.take(max as u64+1).read_to_end(&mut bytes)?;
    if bytes.len()>max{return Err(Error::new(ErrorKind::InvalidData,"RPC input grew beyond limit"));}Ok(bytes)
}
fn readiness_result(read:std::io::Result<Vec<u8>>)->Result<Option<u32>,String>{
    use std::io::ErrorKind;
    let raw=match read{
        Ok(raw)=>raw,
        // Guest CREATE_ALWAYS/publication and WSL DrvFS can briefly expose no
        // data or a disappearing file. The caller retries under its idle deadline.
        Err(e) if matches!(e.kind(),ErrorKind::Interrupted|ErrorKind::WouldBlock|ErrorKind::NotFound|ErrorKind::UnexpectedEof)
            ||(cfg!(target_os="linux")&&e.raw_os_error()==Some(61))=>return Ok(None),
        Err(e)=>return Err(e.to_string()),
    };
    if raw.len()!=4{return Ok(None);}
    Ok(Some(u32::from_le_bytes(raw.try_into().unwrap())))
}
#[cfg(not(windows))]
fn replace_file(src:&Path,dst:&Path)->std::io::Result<()>{fs::rename(src,dst)}
#[cfg(windows)]
fn replace_file(src:&Path,dst:&Path)->std::io::Result<()>{
    use std::os::windows::ffi::OsStrExt;
    #[link(name="kernel32")]
    extern "system" {fn MoveFileExW(old:*const u16,new:*const u16,flags:u32)->i32;}
    let from:Vec<u16>=src.as_os_str().encode_wide().chain(Some(0)).collect();
    let to:Vec<u16>=dst.as_os_str().encode_wide().chain(Some(0)).collect();
    if unsafe{MoveFileExW(from.as_ptr(),to.as_ptr(),1|8)}==0{Err(std::io::Error::last_os_error())}else{Ok(())}
}
fn publish(dir:&Path,name:&str,bytes:&[u8])->Result<(),String>{
    let destination=dir.join(name);
    let temp=dir.join(if name=="response.bin"{"response.tmp"}else if name=="counters.json"{"counters.tmp"}else{"latest-frame.tmp"});
    let mut f=File::create(&temp).map_err(|e|format!("publish {} create temporary {}: {e}",destination.display(),temp.display()))?;
    f.write_all(bytes).map_err(|e|format!("publish {} write temporary {}: {e}",destination.display(),temp.display()))?;
    f.flush().map_err(|e|format!("publish {} flush temporary {}: {e}",destination.display(),temp.display()))?;drop(f);
    replace_file(&temp,&destination).map_err(|e|format!("publish rename {} -> {}: {e}",temp.display(),destination.display()))
}
fn run()->Result<(),String>{
    let mut args=std::env::args().skip(1);
    let mut rpc:Option<PathBuf>=None;let mut once=false;let mut idle=300u64;
    while let Some(a)=args.next(){match a.as_str(){
        "--rpc-dir"=>rpc=Some(PathBuf::from(args.next().ok_or("--rpc-dir needs an absolute path")?)),
        "--once"=>once=true,
        "--idle-seconds"=>{idle=args.next().ok_or("missing idle seconds")?.parse().map_err(|_|"invalid idle seconds")?;if !(1..=3600).contains(&idle){return Err("idle seconds must be 1..3600".into());}},
        "--help"=>{println!("p2d-gles-host --rpc-dir <owned absolute private RPC folder> [--once] [--idle-seconds 300]");return Ok(());},
        _=>return Err(format!("unknown CLI argument {a}"))
    }}
    let requested=rpc.ok_or("explicit --rpc-dir is required")?;
    if !requested.is_absolute(){return Err("RPC path must be absolute".into());}
    let dir=fs::canonicalize(&requested).map_err(|e|format!("resolve RPC directory {}: {e}",requested.display()))?;
    if !dir.is_dir(){return Err("RPC path must be an existing directory".into());}
    let marker=bounded_read(&dir.join(".p2d-gles-owned"),128)?;
    if std::str::from_utf8(&marker).unwrap_or("").trim()!=OWNERSHIP_MARKER{return Err("RPC ownership marker mismatch".into());}
    // The upstream optional texture-dump environment must never select another folder.
    std::env::remove_var("POCKETHLE_DUMP_TEXTURES");
    let mut bridge=Bridge::default();let mut last=Instant::now();
    let mut completed:Option<u32>=None;
    eprintln!("PocketHLE software renderer bridge; RPC {}",dir.display());
    loop{
        if last.elapsed()>Duration::from_secs(idle){return Err("RPC idle timeout".into());}
        let ready=dir.join("request.ready");
        let seq=match readiness_result(bounded_read_io(&ready,4)).map_err(|e|format!("ready read {}: {e}",ready.display()))?{
            Some(seq)=>seq,None=>{thread::sleep(Duration::from_millis(5));continue;}
        };
        if completed==Some(seq){thread::sleep(Duration::from_millis(5));continue;}
        // Zero is reserved for the harness seed. request.bin may already contain
        // sequence1 while the guest has not yet replaced request.ready's zero.
        if seq==0{thread::sleep(Duration::from_millis(5));continue;}
        let input=bounded_read(&dir.join("request.bin"),MAX_REQUEST).map_err(|e|format!("request read: {e}"));
        // Keep guest directory entries stable; a changed sequence is the handshake.
        let response=process(&mut bridge,seq,input);
        let mut bmp=Vec::new();bridge.write_bmp(&mut bmp).map_err(|e|e.to_string())?;
        publish(&dir,"latest-frame.bmp",&bmp)?;
        let counters=serde_json::to_vec_pretty(&bridge.counters).map_err(|e|e.to_string())?;
        publish(&dir,"counters.json",&counters)?;
        publish(&dir,"response.bin",&response)?;
        completed=Some(seq);last=Instant::now();
        if once{return Ok(());}
    }
}
fn process(bridge:&mut Bridge,seq:u32,input:Result<Vec<u8>,String>)->Vec<u8>{
    let result=input.and_then(|b|{
        if b.len()<8||u32::from_le_bytes(b[4..8].try_into().unwrap())!=seq{return Err("ready/request sequence mismatch".into());}
        bridge.execute(&b)
    });
    match result{Ok(bytes)=>bytes,Err(why)=>{eprintln!("sequence {seq}: {why}");bridge.failure_response(seq,why)}}
}
fn main(){if let Err(e)=run(){eprintln!("p2d-gles-host: {e}");std::process::exit(1);}}

#[cfg(test)]
mod tests{
    use super::readiness_result;
    use std::io::{Error,ErrorKind};
    #[test]
    fn interrupted_or_disappearing_ready_reads_request_retry(){
        for kind in [ErrorKind::Interrupted,ErrorKind::WouldBlock,ErrorKind::NotFound,ErrorKind::UnexpectedEof]{
            assert_eq!(readiness_result(Err(Error::from(kind))).unwrap(),None,"{kind:?}");
        }
        for raw in [vec![],vec![1],vec![1,0,0]]{assert_eq!(readiness_result(Ok(raw)).unwrap(),None);}
        assert_eq!(readiness_result(Ok(vec![17,0,0,0])).unwrap(),Some(17));
    }
    #[cfg(target_os="linux")]
    #[test]
    fn drvfs_enodata_during_ready_publication_requests_retry(){
        assert_eq!(readiness_result(Err(Error::from_raw_os_error(61))).unwrap(),None);
    }
    #[test]
    fn real_ready_permission_path_and_size_errors_remain_fatal(){
        for kind in [ErrorKind::PermissionDenied,ErrorKind::NotADirectory,ErrorKind::InvalidData]{
            assert!(readiness_result(Err(Error::from(kind))).is_err(),"{kind:?}");
        }
    }
}
