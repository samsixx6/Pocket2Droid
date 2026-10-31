use std::{fs,path::PathBuf,process::Command,sync::atomic::{AtomicU64,Ordering}};
static NEXT:AtomicU64=AtomicU64::new(0);
struct Dir(PathBuf);
impl Dir { fn new()->Self {let d=std::env::temp_dir().join(format!("p2d-gles-test-{}-{}",std::process::id(),NEXT.fetch_add(1,Ordering::Relaxed)));fs::create_dir(&d).unwrap();Self(d)} }
impl Drop for Dir {fn drop(&mut self){fs::remove_dir_all(&self.0).unwrap();}}
#[test]
fn daemon_refuses_folder_without_ownership_marker(){
    let d=Dir::new();
    let r=Command::new(env!("CARGO_BIN_EXE_p2d-gles-host")).arg("--rpc-dir").arg(&d.0).args(["--once","--idle-seconds","1"]).output().unwrap();
    assert!(!r.status.success());
    assert_eq!(fs::read_dir(&d.0).unwrap().count(),0);
}
#[test]
fn daemon_reads_ready_packet_publishes_response_and_evidence(){
    let d=Dir::new();
    fs::write(d.0.join(".p2d-gles-owned"),"P2D_GLES_BRIDGE_LAB_V1\n").unwrap();
    let packet:Vec<u8>=[0x314c4750u32,17,0,16,16,24].iter().flat_map(|v|v.to_le_bytes()).collect();
    fs::write(d.0.join("request.bin"),packet).unwrap();
    fs::write(d.0.join("request.ready"),17u32.to_le_bytes()).unwrap();
    let r=Command::new(env!("CARGO_BIN_EXE_p2d-gles-host")).arg("--rpc-dir").arg(&d.0).args(["--once","--idle-seconds","1"]).output().unwrap();
    assert!(r.status.success(),"{}",String::from_utf8_lossy(&r.stderr));
    let response=fs::read(d.0.join("response.bin")).unwrap();
    assert_eq!(&response[..8],&[0x50,0x47,0x52,0x31,17,0,0,0]);
    assert_eq!(response.len(),24+16*16*2);
    // vvfat-backed guest retains this file and rewrites the sequence in place.
    assert_eq!(fs::read(d.0.join("request.ready")).unwrap(),17u32.to_le_bytes());
    assert!(!d.0.join("response.tmp").exists());
    let bmp=fs::read(d.0.join("latest-frame.bmp")).unwrap();
    assert_eq!(&bmp[..2],b"BM");assert_eq!(bmp.len(),54+16*16*4);
    let counters:serde_json::Value=serde_json::from_slice(&fs::read(d.0.join("counters.json")).unwrap()).unwrap();
    assert_eq!(counters["batches"],1);assert_eq!(counters["last_sequence"],17);
}
#[test]
fn startup_zero_is_ignored_even_after_next_request_file_is_published(){
    use std::{io::{BufRead,BufReader},process::Stdio,thread,time::Duration};
    let d=Dir::new();
    fs::write(d.0.join(".p2d-gles-owned"),"P2D_GLES_BRIDGE_LAB_V1\n").unwrap();
    // Guest publication order: request.bin sequence17 exists before ready becomes17.
    let packet:Vec<u8>=[0x314c4750u32,17,0,16,16,24].iter().flat_map(|v|v.to_le_bytes()).collect();
    fs::write(d.0.join("request.bin"),packet).unwrap();
    fs::write(d.0.join("request.ready"),0u32.to_le_bytes()).unwrap();
    let mut child=Command::new(env!("CARGO_BIN_EXE_p2d-gles-host")).arg("--rpc-dir").arg(&d.0).args(["--once","--idle-seconds","2"]).stderr(Stdio::piped()).spawn().unwrap();
    let mut stderr=BufReader::new(child.stderr.take().unwrap());
    let mut startup=String::new();stderr.read_line(&mut startup).unwrap();
    assert!(startup.contains("renderer bridge"));
    thread::sleep(Duration::from_millis(250));
    let published_zero=d.0.join("response.bin").exists();
    fs::write(d.0.join("request.ready"),17u32.to_le_bytes()).unwrap();
    let result=child.wait().unwrap();
    assert!(result.success());
    assert!(!published_zero,"reserved startup sequence must not produce a response or failure");
    let counters:serde_json::Value=serde_json::from_slice(&fs::read(d.0.join("counters.json")).unwrap()).unwrap();
    assert_eq!(counters["failures"],0);assert_eq!(counters["batches"],1);assert_eq!(counters["last_sequence"],17);
}
