use std::env;
#[derive(Clone,Copy)]
struct Body { x:f64, y:f64, z:f64, vx:f64, vy:f64, vz:f64, mass:f64 }
fn init() -> [Body;5] {
    let pi=3.141592653589793f64; let sm=4.0*pi*pi; let dpy=365.24f64;
    let mut b=[Body{x:0.0,y:0.0,z:0.0,vx:0.0,vy:0.0,vz:0.0,mass:0.0};5];
    b[0]=Body{x:0.0,y:0.0,z:0.0,vx:0.0,vy:0.0,vz:0.0,mass:sm};
    b[1]=Body{x:4.84143144246472090,y:-1.16032004402742839,z:-0.103622044471123109,
        vx:0.00166007664274403694*dpy,vy:0.00769901118419740425*dpy,vz:-0.0000690460016972063023*dpy,mass:0.000954791938424326609*sm};
    b[2]=Body{x:8.34336671824457987,y:4.12479856412430479,z:-0.403523417114321381,
        vx:-0.00276742510726862411*dpy,vy:0.00499852801234917238*dpy,vz:0.0000230417297573763929*dpy,mass:0.000285885980666130812*sm};
    b[3]=Body{x:12.8943695621391310,y:-15.1111514016986312,z:-0.223307578892655734,
        vx:0.00296460137564761618*dpy,vy:0.00237847173959480950*dpy,vz:-0.0000296589568540237556*dpy,mass:0.0000436624404335156298*sm};
    b[4]=Body{x:15.3796971148509165,y:-25.9193146099879641,z:0.179258772950371181,
        vx:0.00268067772490389322*dpy,vy:0.00162824170038242295*dpy,vz:-0.0000951592254519715870*dpy,mass:0.0000515138902046611451*sm};
    let (mut px,mut py,mut pz)=(0.0,0.0,0.0);
    for i in 0..5 { px+=b[i].vx*b[i].mass; py+=b[i].vy*b[i].mass; pz+=b[i].vz*b[i].mass; }
    b[0].vx=-px/sm; b[0].vy=-py/sm; b[0].vz=-pz/sm; b
}
#[inline(never)]
fn advance(b:&mut [Body;5], dt:f64) {
    for i in 0..5 { for j in (i+1)..5 {
        let dx=b[i].x-b[j].x; let dy=b[i].y-b[j].y; let dz=b[i].z-b[j].z;
        let d2=dx*dx+dy*dy+dz*dz; let dist=d2.sqrt(); let mag=dt/(d2*dist);
        let mi=b[i].mass*mag; let mj=b[j].mass*mag;
        b[i].vx-=dx*mj; b[i].vy-=dy*mj; b[i].vz-=dz*mj;
        b[j].vx+=dx*mi; b[j].vy+=dy*mi; b[j].vz+=dz*mi;
    }}
    for i in 0..5 { b[i].x+=dt*b[i].vx; b[i].y+=dt*b[i].vy; b[i].z+=dt*b[i].vz; }
}
fn energy(b:&[Body;5])->f64 {
    let mut e=0.0; for i in 0..5 {
        e+=0.5*b[i].mass*(b[i].vx*b[i].vx+b[i].vy*b[i].vy+b[i].vz*b[i].vz);
        for j in (i+1)..5 { let dx=b[i].x-b[j].x; let dy=b[i].y-b[j].y; let dz=b[i].z-b[j].z;
            e-=(b[i].mass*b[j].mass)/(dx*dx+dy*dy+dz*dz).sqrt(); }
    } e
}
#[inline(never)]
fn kernel(r:i64)->i64 {
    let mut b=init(); let mut k=0i64;
    while k<r { advance(&mut b,0.01); k+=1; }
    (energy(&b)*1000000000.0) as i64
}
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    println!("{}", kernel(r)); }
