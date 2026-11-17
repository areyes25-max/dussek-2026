//! The sound of one player: `rt.c` in Rust. The lips and the bore's modes at 4 × the base rate, the mouthpiece
//! pressure steepening in a short delay line, down to the base rate, the part of the impedance the modes leave out
//! (FIR from the flow), out through the bell (FIR). A note's change crossfades the filters over 10 ms.
//! A reed plays the same loop with a reed for the lips and the pieces the lips do without (`reed_loop`): the opening's
//! width ramping, the tongue (holds the reed shut, closes the opening under it), the seed (the tongue's push as it lets
//! go), the glide (a fingering change carries the vibration from the old modes into the new) and its output, each
//! note's share of the flow through its own Z H (`rt_filters_flow`). The loop is one, built twice: without the pieces
//! (a block where none is at work, the lips' always) it is the lips' sums as they were.
//! Double precision inside; the filters are the export's 32-bit tables, against a 64-bit history. Nothing allocates
//! after `new`.

use super::tables::Lips;
use std::sync::Arc;

pub const OS: usize = 4;
pub const MAXM: usize = 64;
pub const HMAX: usize = 4096;
const DMAX: usize = 256;
const SD: usize = 32;
const SD_D: usize = 24;
const FADE: i32 = 441;
/// a reed's notes radiating at once: the one sounding and two still ringing out (a trill)
const FLOWS: usize = 3;
/// Hz: the flow's DC blocker
const FLOW_HP: f64 = 30.0;
pub const SR: f64 = 44100.0;

/// What the block's controls ramp over: from `.0` to `.1`.
#[derive(Clone, Copy, Debug, Default)]
pub struct Ramp {
    pub pm: (f64, f64),
    pub lip: (f64, f64),
    pub burst: (f64, f64),
    /// the opening's width, m (the lips': the instrument's, both ends)
    pub width: (f64, f64),
    /// the tongue on the reed: 0 off, 1 holding it shut (the lips: 0)
    pub tongue: (f64, f64),
}

/// The bore's modes per oversampled step: each its pole's step `E` and its residue `r` (`rc T / 2`).
#[derive(Clone)]
pub struct ModeBank {
    pub n: usize,
    pub er: [f64; MAXM],
    pub ei: [f64; MAXM],
    pub rr: [f64; MAXM],
    pub ri: [f64; MAXM],
}

impl Default for ModeBank {
    fn default() -> Self {
        ModeBank { n: 0, er: [0.0; MAXM], ei: [0.0; MAXM], rr: [0.0; MAXM], ri: [0.0; MAXM] }
    }
}

/// An instrument's constants in the loop.
#[derive(Clone, Copy, Debug)]
pub struct CoreParams {
    pub width: f64,
    pub y0: f64,
    pub soft: f64,
    pub rho: f64,
    pub nl: f64,
}

pub struct Core {
    xr: [f64; MAXM],
    xi: [f64; MAXM],
    ly: [f64; 8],
    lv: [f64; 8],
    uprev: f64,
    pprev: f64,
    noise_pos: usize,
    sd: [f64; SD],
    si: usize,
    dp: Box<[f64; 2 * DMAX]>,
    du: Box<[f64; 2 * DMAX]>,
    dpos: usize,
    ht: Box<[f64; 2 * HMAX]>,
    hu: Box<[f64; 2 * HMAX]>,
    hpos_t: usize,
    hpos_u: usize,
    h: Box<[f32]>,
    nh: usize,
    h_old: Box<[f32]>,
    nh_old: usize,
    r: Option<Arc<[f32]>>,
    r_old: Option<Arc<[f32]>>,
    fade: i32,
    // a reed's output (`change_flow`): per slot a note's filter, its flow history (its weighted share of the flow,
    // base rate), the base samples it still sounds for (its fade, then its filter's length), its share at the last
    // change; the slot sounding; the fade's base samples left
    flow: bool,
    fcur: usize,
    fade_n: i32,
    zh: [Option<Arc<[f32]>>; FLOWS],
    nzh: [i32; FLOWS],
    fpos: [usize; FLOWS],
    ftail: [i32; FLOWS],
    w0: [f64; FLOWS],
    fu: Box<[[f64; 2 * HMAX]]>,
    // the flow's DC blocker: a biquad and its state
    hp_b: [f64; 3],
    hp_a: [f64; 2],
    hp_z: [f64; 2],
    // the seed (`start_seed`): m/s at its peak, the phase per oversampled step (rad), steps done and in all
    seed_amp: f64,
    seed_w: f64,
    seed_k: i64,
    seed_n: i64,
    // the glide (`start_glide`): the old bank (poles, residues, the log of the poles), the state of its modes
    eo_r: [f64; MAXM],
    eo_i: [f64; MAXM],
    ro_r: [f64; MAXM],
    ro_i: [f64; MAXM],
    leo_r: [f64; MAXM],
    leo_i: [f64; MAXM],
    xo_r: [f64; MAXM],
    xo_i: [f64; MAXM],
    /// the old bank's sum of 2 Re r
    g_old: f64,
    f_old: f64,
    f_new: f64,
    pair_max: f64,
    pair_tol: f64,
    /// the old bank's modes
    mo: usize,
    glide: bool,
    /// the pairing waits for the new bank (the first block)
    pending: bool,
    /// oversampled steps of the glide done
    gk: usize,
    /// per new mode the old one it glides from (-1: none, it starts from rest)
    partner: [i32; MAXM],
    /// per old mode the new one it glides into (-1: none, it fades out on its own)
    taken: [i32; MAXM],
    /// the portable path even where the CPU has the wider one (the test that they agree)
    pub force_scalar: bool,
}

impl Default for Core {
    fn default() -> Self {
        Self::new()
    }
}

/// The dot product of a 32-bit filter with a 64-bit history, in lanes the compiler vectorizes.
#[inline(always)]
fn dot<const FMA: bool>(h: &[f32], x: &[f64]) -> f64 {
    const L: usize = 4;
    let x = &x[..h.len()]; // the history as long as the filter: the remainders line up
    let mut acc = [0.0f64; L];
    let mut hc = h.chunks_exact(L);
    let mut xc = x.chunks_exact(L);
    for (a, b) in (&mut hc).zip(&mut xc) {
        for l in 0..L {
            acc[l] = if FMA { f64::from(a[l]).mul_add(b[l], acc[l]) } else { acc[l] + f64::from(a[l]) * b[l] };
        }
    }
    let mut t = acc[0] + acc[1] + acc[2] + acc[3];
    for (a, b) in hc.remainder().iter().zip(xc.remainder()) {
        t += f64::from(*a) * *b;
    }
    t
}

/// RBJ's Butterworth high-pass at `FLOW_HP` (Q 1/√2), for transposed direct form II: (b, a1 a2).
fn flow_hp() -> ([f64; 3], [f64; 2]) {
    let w = 2.0 * std::f64::consts::PI * FLOW_HP / SR;
    let (c, al) = (w.cos(), w.sin() / (2.0 * std::f64::consts::FRAC_1_SQRT_2));
    let a0 = 1.0 + al;
    let b = [(1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0];
    (b, [-2.0 * c / a0, (1.0 - al) / a0])
}

impl Core {
    pub fn new() -> Core {
        Core {
            xr: [0.0; MAXM],
            xi: [0.0; MAXM],
            ly: [0.0; 8],
            lv: [0.0; 8],
            uprev: 0.0,
            pprev: 0.0,
            noise_pos: 0,
            sd: [0.0; SD],
            si: 0,
            dp: Box::new([0.0; 2 * DMAX]),
            du: Box::new([0.0; 2 * DMAX]),
            dpos: 0,
            ht: Box::new([0.0; 2 * HMAX]),
            hu: Box::new([0.0; 2 * HMAX]),
            hpos_t: 0,
            hpos_u: 0,
            h: vec![0.0; HMAX].into_boxed_slice(),
            nh: 0,
            h_old: vec![0.0; HMAX].into_boxed_slice(),
            nh_old: 0,
            r: None,
            r_old: None,
            fade: 0,
            flow: false,
            fcur: 0,
            fade_n: 0,
            zh: [None, None, None],
            nzh: [0; FLOWS],
            fpos: [0; FLOWS],
            ftail: [0; FLOWS],
            w0: [0.0; FLOWS],
            fu: vec![[0.0; 2 * HMAX]; FLOWS].into_boxed_slice(),
            hp_b: flow_hp().0,
            hp_a: flow_hp().1,
            hp_z: [0.0; 2],
            seed_amp: 0.0,
            seed_w: 0.0,
            seed_k: 0,
            seed_n: 0,
            eo_r: [0.0; MAXM],
            eo_i: [0.0; MAXM],
            ro_r: [0.0; MAXM],
            ro_i: [0.0; MAXM],
            leo_r: [0.0; MAXM],
            leo_i: [0.0; MAXM],
            xo_r: [0.0; MAXM],
            xo_i: [0.0; MAXM],
            g_old: 0.0,
            f_old: 0.0,
            f_new: 0.0,
            pair_max: 0.0,
            pair_tol: 0.0,
            mo: 0,
            glide: false,
            pending: false,
            gk: 0,
            partner: [-1; MAXM],
            taken: [-1; MAXM],
            force_scalar: false,
        }
    }

    /// New filters, the old ones fading out over 10 ms (none before: at once). Fills the bell's in with `fill`.
    pub fn change_filters(&mut self, nh: usize, r: Arc<[f32]>, fill: impl FnOnce(&mut [f32])) {
        std::mem::swap(&mut self.h, &mut self.h_old);
        self.nh_old = self.nh;
        self.fade = if self.r.is_some() { FADE } else { 0 };
        self.r_old = self.r.replace(r);
        self.nh = nh;
        fill(&mut self.h[..nh]);
    }

    /// `change_filters` from a slice.
    pub fn set_filters(&mut self, h: &[f32], r: Arc<[f32]>) {
        self.change_filters(h.len(), r, |to| to.copy_from_slice(h));
    }

    /// A reed's output from here on (`rt_filters_flow`): each note's share of the flow (base rate, through a 30 Hz DC
    /// blocker) through its own flow-to-air filter `zh` (Z H); the modal pressure, the residual and the bell's filter
    /// play no part. A change gives the new note the flow's share rising over 10 ms, every old one the share it had,
    /// falling (the shares sum to 1 however close the changes); an old filter rings out its own past flow (its fade
    /// plus its length). With every slot sounding a change takes the one nearest its end. False (nothing changed) for a
    /// filter of no taps or over 4096: never cut short.
    pub fn change_flow(&mut self, zh: Arc<[f32]>) -> bool {
        let n = zh.len();
        if !(1..=HMAX).contains(&n) {
            return false;
        }
        let mut c = 0;
        if self.flow {
            let g = if self.fade_n > 0 { 1.0 - f64::from(self.fade_n) / f64::from(FADE) } else { 1.0 }; // the sounding note's share now
            for k in 0..FLOWS {
                // what every old slot has now: from here it falls over the new fade
                if k == self.fcur {
                    continue;
                }
                let w = if self.ftail[k] > self.nzh[k] { self.w0[k] * (1.0 - g) } else { 0.0 };
                self.w0[k] = w;
                if w > 0.0 {
                    self.ftail[k] = FADE + self.nzh[k];
                }
            }
            self.w0[self.fcur] = g;
            self.ftail[self.fcur] = FADE + self.nzh[self.fcur]; // the old note: its share falls, then it rings out
            for k in 0..FLOWS {
                // the slot that ends first, not the one sounding
                if k != self.fcur && (c == self.fcur || self.ftail[k] < self.ftail[c]) {
                    c = k;
                }
            }
            self.fade_n = FADE;
        } else {
            self.ftail = [0; FLOWS];
            self.w0 = [0.0; FLOWS];
            self.fade_n = 0;
        }
        self.fu[c].fill(0.0);
        self.fpos[c] = 0;
        self.zh[c] = Some(zh);
        self.nzh[c] = n as i32;
        self.ftail[c] = 0;
        self.w0[c] = 0.0;
        self.fcur = c;
        self.flow = true;
        true
    }

    /// The tongue's push as it lets go of the reed (`rt_seed`): for `seed_s` seconds each oversampled step adds
    /// −amp · hann · sin(2π f t) · 2π f / (sr · OS) to the reed mode's speed (numpy's Hann over the steps).
    pub fn start_seed(&mut self, amp: f64, f: f64, seed_s: f64) {
        self.seed_amp = amp;
        self.seed_w = 2.0 * std::f64::consts::PI * f / (SR * OS as f64);
        self.seed_n = (seed_s * SR * OS as f64).round() as i64;
        self.seed_k = 0;
    }

    /// A fingering change (`rt_glide`), before the first block of the new bank: the old bank and the state are kept;
    /// that block pairs each new mode with the old one at its harmonic number (under `pair_max`, the nearest within
    /// `pair_tol` of its log, an old mode given once) and the pairs glide from the old to the new over 10 ms, one mode
    /// at full weight whose log pole and residue move linearly. An unpaired new mode starts from rest at the glide's
    /// progress, an unpaired old one goes on at the rest. The new bank keeps its count of modes during the glide. A
    /// glide during a glide starts over from the bank sounding: the first one's unpaired old modes stop.
    pub fn start_glide(&mut self, old: &ModeBank, f_old: f64, f_new: f64, pair_max: f64, pair_tol: f64) {
        let mo = old.n.min(MAXM);
        self.mo = mo;
        self.g_old = 0.0;
        for i in 0..mo {
            self.eo_r[i] = old.er[i];
            self.eo_i[i] = old.ei[i];
            self.ro_r[i] = old.rr[i];
            self.ro_i[i] = old.ri[i];
            self.leo_r[i] = old.er[i].hypot(old.ei[i]).ln();
            self.leo_i[i] = old.ei[i].atan2(old.er[i]);
            self.xo_r[i] = self.xr[i];
            self.xo_i[i] = self.xi[i];
            self.g_old += 2.0 * old.rr[i];
        }
        self.f_old = f_old;
        self.f_new = f_new;
        self.pair_max = pair_max;
        self.pair_tol = pair_tol;
        self.glide = true;
        self.pending = true;
        self.gk = 0;
    }

    /// The pairing, at the first block of the new bank (`er`, `ei` its poles per step `t`).
    fn pair(&mut self, er: &[f64], ei: &[f64], t: f64) {
        let top = self.pair_max.ln();
        let two_pi_t = 2.0 * std::f64::consts::PI * t;
        let mut fmo = [0.0; MAXM];
        self.partner = [-1; MAXM];
        self.taken = [-1; MAXM];
        for i in 0..self.mo {
            fmo[i] = (self.eo_i[i].atan2(self.eo_r[i]) / two_pi_t / self.f_old).ln();
        }
        for j in 0..er.len() {
            self.xr[j] = 0.0;
            self.xi[j] = 0.0;
            let fm = (ei[j].atan2(er[j]) / two_pi_t / self.f_new).ln();
            if fm.partial_cmp(&top) != Some(std::cmp::Ordering::Less) {
                // at or over pair_max (or no pole to read): from rest
                continue;
            }
            let (mut best, mut bd) = (None, self.pair_tol);
            for i in 0..self.mo {
                if self.taken[i] < 0 && fmo[i] < top && (fmo[i] - fm).abs() < bd {
                    bd = (fmo[i] - fm).abs();
                    best = Some(i);
                }
            }
            if let Some(i) = best {
                self.partner[j] = i as i32;
                self.taken[i] = j as i32;
                self.xr[j] = self.xo_r[i];
                self.xi[j] = self.xo_i[i];
            }
        }
        self.pending = false;
    }

    /// A reed's output sample from the base-rate flow `ub`: through the DC blocker, each sounding note's share
    /// through its zh. The exact Z H vanishes at DC, cut to its taps it keeps a gain under 40 Hz: the flow's step from
    /// rest as the tongue lets go came out as a thump.
    #[inline(always)]
    fn flow_out<const FMA: bool>(&mut self, ub: f64) -> f64 {
        let v = self.hp_b[0] * ub + self.hp_z[0];
        self.hp_z[0] = self.hp_b[1] * ub - self.hp_a[0] * v + self.hp_z[1];
        self.hp_z[1] = self.hp_b[2] * ub - self.hp_a[1] * v;
        let ub = v;
        let g = if self.fade_n > 0 { 1.0 - f64::from(self.fade_n) / f64::from(FADE) } else { 1.0 }; // the new note's share
        if self.fade_n > 0 {
            self.fade_n -= 1;
        }
        let mut y = 0.0;
        for k in 0..FLOWS {
            if k != self.fcur && self.ftail[k] <= 0 {
                continue;
            }
            let Some(zh) = &self.zh[k] else { continue };
            let share = if k == self.fcur { g } else if self.ftail[k] > self.nzh[k] { self.w0[k] * (1.0 - g) } else { 0.0 };
            let at = if self.fpos[k] == 0 { HMAX - 1 } else { self.fpos[k] - 1 };
            self.fpos[k] = at;
            let fu = &mut self.fu[k];
            fu[at] = ub * share;
            fu[at + HMAX] = ub * share;
            y += dot::<FMA>(zh, &fu[at..]);
            if k != self.fcur {
                self.ftail[k] -= 1;
            }
        }
        y
    }

    /// A piece of the reed's at work in this block, or its output the flow's: `rt_process`'s choice of `reed_loop`.
    fn reed_block(&self, ctl: &Ramp) -> bool {
        self.flow || ctl.width.0 != ctl.width.1 || ctl.tongue.0 != 0.0 || ctl.tongue.1 != 0.0 || self.glide || self.seed_k < self.seed_n
    }

    /// A fingering change's glide at work (`start_glide`, for 10 ms).
    pub fn gliding(&self) -> bool {
        self.glide
    }

    /// Where in the noise it reads.
    pub fn set_noise_pos(&mut self, at: usize) {
        self.noise_pos = at;
    }

    /// The bell's filter as it stands: the player's blend moves it in place (no crossfade: its steps are small).
    pub fn h_mut(&mut self) -> &mut [f32] {
        &mut self.h[..self.nh]
    }

    /// The state from nothing, the filters kept and no fade (a voice that went non-finite). A reed's output stops, its
    /// slots empty (`rt_new`): the player gives it the sounding note's filter again (`change_flow`, slot 0), as live.py.
    pub fn reset(&mut self) {
        self.xr = [0.0; MAXM];
        self.xi = [0.0; MAXM];
        self.ly = [0.0; 8];
        self.lv = [0.0; 8];
        self.uprev = 0.0;
        self.pprev = 0.0;
        self.noise_pos = 0;
        self.sd = [0.0; SD];
        self.si = 0;
        self.dp.fill(0.0);
        self.du.fill(0.0);
        self.dpos = 0;
        self.ht.fill(0.0);
        self.hu.fill(0.0);
        self.hpos_t = 0;
        self.hpos_u = 0;
        self.fade = 0;
        // a reed's output off, its slots empty; no seed, no glide
        if self.flow {
            for f in self.fu.iter_mut() {
                f.fill(0.0);
            }
        }
        self.flow = false;
        self.fcur = 0;
        self.zh = [None, None, None];
        self.nzh = [0; FLOWS];
        self.fpos = [0; FLOWS];
        self.ftail = [0; FLOWS];
        self.w0 = [0.0; FLOWS];
        self.fade_n = 0;
        self.hp_z = [0.0; 2];
        self.seed_k = 0;
        self.seed_n = 0;
        self.glide = false;
        self.pending = false;
        self.partner = [-1; MAXM];
    }

    /// The state not a number (the test of what follows it).
    pub fn poison(&mut self) {
        self.xr[0] = f64::NAN;
        self.uprev = f64::NAN;
    }

    /// `out.len()` base-rate samples.
    #[allow(clippy::too_many_arguments)]
    pub fn process(&mut self, ctl: &Ramp, kick: f64, mu: f64, modes: &ModeBank, lips: &Lips, p: &CoreParams, noise: &[f32], dec: &[f64], out: &mut [f64]) {
        let reed = self.reed_block(ctl);
        #[cfg(target_arch = "x86_64")]
        if !self.force_scalar && std::arch::is_x86_feature_detected!("avx2") && std::arch::is_x86_feature_detected!("fma") {
            // SAFETY: the CPU has them, just checked
            unsafe { self.process_avx2(reed, ctl, kick, mu, modes, lips, p, noise, dec, out) };
            return;
        }
        if reed {
            self.body::<false, true>(ctl, kick, mu, modes, lips, p, noise, dec, out);
        } else {
            self.body::<false, false>(ctl, kick, mu, modes, lips, p, noise, dec, out);
        }
    }

    #[cfg(target_arch = "x86_64")]
    #[target_feature(enable = "avx2,fma")]
    #[allow(clippy::too_many_arguments)]
    unsafe fn process_avx2(&mut self, reed: bool, ctl: &Ramp, kick: f64, mu: f64, modes: &ModeBank, lips: &Lips, p: &CoreParams, noise: &[f32], dec: &[f64], out: &mut [f64]) {
        if reed {
            self.body::<true, true>(ctl, kick, mu, modes, lips, p, noise, dec, out);
        } else {
            self.body::<true, false>(ctl, kick, mu, modes, lips, p, noise, dec, out);
        }
    }

    /// The loop; `REED`: with the reed's pieces (`reed_loop`), else the lips' (`lips_loop`, the width `ctl.width.0`).
    #[inline(always)]
    #[allow(clippy::too_many_arguments)]
    fn body<const FMA: bool, const REED: bool>(&mut self, ctl: &Ramp, kick: f64, mu: f64, modes: &ModeBank, lips: &Lips, p: &CoreParams, noise: &[f32], dec: &[f64], out: &mut [f64]) {
        let t_step = 1.0 / (SR * OS as f64);
        let m = modes.n;
        let (er, ei, rr, ri) = (&modes.er[..m], &modes.ei[..m], &modes.rr[..m], &modes.ri[..m]);
        let g_sum: f64 = rr.iter().map(|x| 2.0 * x).sum();
        let total = out.len() * OS;
        let ndec = dec.len();
        let nlip = lips.ratio.len();
        let mut xhr = [0.0; MAXM];
        let mut xhi = [0.0; MAXM];
        // the glide's: the log of the new poles, the residues of the step, the old modes' next state
        let (mut le_r, mut le_i) = ([0.0; MAXM], [0.0; MAXM]);
        let (mut rkr, mut rki) = ([0.0; MAXM], [0.0; MAXM]);
        let (mut xhor, mut xhoi) = ([0.0; MAXM], [0.0; MAXM]);
        if REED && self.glide {
            if self.pending {
                self.pair(er, ei, t_step);
            }
            for j in 0..m {
                if self.partner[j] >= 0 {
                    le_r[j] = er[j].hypot(ei[j]).ln();
                    le_i[j] = ei[j].atan2(er[j]);
                }
            }
        }
        for (i, o_sample) in out.iter_mut().enumerate() {
            for o in 0..OS {
                let k = i * OS + o;
                let w = k as f64 / total as f64;
                let pm = ctl.pm.0 + (ctl.pm.1 - ctl.pm.0) * w;
                let lf = ctl.lip.0 + (ctl.lip.1 - ctl.lip.0) * w;
                let bu = ctl.burst.0 + (ctl.burst.1 - ctl.burst.0) * w;
                let (wd, tg) = if REED {
                    (ctl.width.0 + (ctl.width.1 - ctl.width.0) * w, ctl.tongue.0 + (ctl.tongue.1 - ctl.tongue.0) * w)
                } else {
                    (ctl.width.0, 0.0)
                };
                let dp = pm - self.pprev;
                let mut y = p.y0;
                for l in 0..nlip {
                    let wl = 2.0 * std::f64::consts::PI * lf * lips.ratio[l];
                    self.lv[l] += t_step * (lips.sign[l] * lips.comp[l] * dp / mu - wl / lips.q[l] * self.lv[l] - wl * wl * self.ly[l]);
                    if l == 0 && kick != 0.0 {
                        self.lv[l] += kick / total as f64; // over the block: at once it clicks
                    }
                    if REED && l == 0 && self.seed_k < self.seed_n {
                        // the seed
                        let span = (self.seed_n - 1).max(1) as f64;
                        let hann = 0.5 - 0.5 * (2.0 * std::f64::consts::PI * self.seed_k as f64 / span).cos();
                        self.lv[l] -= self.seed_amp * hann * (self.seed_w * self.seed_k as f64).sin() * self.seed_w;
                        self.seed_k += 1;
                    }
                    self.ly[l] += t_step * self.lv[l];
                    if REED && tg >= 1.0 {
                        // the tongue holds the reed shut
                        self.ly[l] = if l == 0 { -p.y0 } else { 0.0 };
                        self.lv[l] = 0.0;
                    }
                    y += self.ly[l];
                }
                let mut s = wd * 0.5 * (y + (y * y + p.soft * p.soft).sqrt());
                if REED {
                    s *= 1.0 - tg; // the opening closes under the tongue, opens as it lets go
                }
                let up = self.uprev;
                let gliding = REED && self.glide;
                let mut ph = 0.0;
                let mut g_k = g_sum;
                if gliding {
                    // the pairs at full weight, their pole and residue between the old and the new; the unpaired new
                    // modes at ga, the unpaired old ones at 1 - ga
                    let ga = (self.gk + 1) as f64 / (FADE as usize * OS) as f64;
                    g_k = (1.0 - ga) * self.g_old + ga * g_sum;
                    for j in 0..m {
                        let (ekr, eki);
                        if self.partner[j] >= 0 {
                            let q = self.partner[j] as usize;
                            let lr = (1.0 - ga) * self.leo_r[q] + ga * le_r[j];
                            let li = (1.0 - ga) * self.leo_i[q] + ga * le_i[j];
                            let (sn, cs) = li.sin_cos();
                            let mag = lr.exp();
                            (ekr, eki) = (mag * cs, mag * sn);
                            rkr[j] = (1.0 - ga) * self.ro_r[q] + ga * rr[j];
                            rki[j] = (1.0 - ga) * self.ro_i[q] + ga * ri[j];
                        } else {
                            (ekr, eki) = (er[j], ei[j]);
                            rkr[j] = rr[j];
                            rki[j] = ri[j];
                        }
                        let (ax, ay) = (self.xr[j] + rkr[j] * up, self.xi[j] + rki[j] * up);
                        xhr[j] = ekr * ax - eki * ay;
                        xhi[j] = ekr * ay + eki * ax;
                        ph += if self.partner[j] >= 0 { 2.0 * xhr[j] } else { ga * 2.0 * xhr[j] };
                    }
                    for q in 0..self.mo {
                        if self.taken[q] >= 0 {
                            continue;
                        }
                        let (ax, ay) = (self.xo_r[q] + self.ro_r[q] * up, self.xo_i[q] + self.ro_i[q] * up);
                        xhor[q] = self.eo_r[q] * ax - self.eo_i[q] * ay;
                        xhoi[q] = self.eo_r[q] * ay + self.eo_i[q] * ax;
                        ph += (1.0 - ga) * 2.0 * xhor[q];
                    }
                } else {
                    for j in 0..m {
                        // E x + r u E
                        let (ax, ay) = (self.xr[j] + rr[j] * up, self.xi[j] + ri[j] * up);
                        xhr[j] = er[j] * ax - ei[j] * ay;
                        xhi[j] = er[j] * ay + ei[j] * ax;
                    }
                    for x in &xhr[..m] {
                        ph += 2.0 * *x;
                    }
                }
                let pp = pm - ph;
                let mut uk = 0.0;
                if s > 0.0 {
                    let b = 2.0 * s * s * g_k / p.rho;
                    let c = 2.0 * s * s * pp.abs() / p.rho;
                    uk = (-b / 2.0 + (b * b / 4.0 + c).sqrt()).copysign(pp);
                }
                uk += f64::from(noise[self.noise_pos]) * bu * uk.abs();
                self.noise_pos += 1;
                if self.noise_pos >= noise.len() {
                    self.noise_pos = 0;
                }
                if gliding {
                    for j in 0..m {
                        self.xr[j] = xhr[j] + rkr[j] * uk;
                        self.xi[j] = xhi[j] + rki[j] * uk;
                    }
                    for q in 0..self.mo {
                        if self.taken[q] < 0 {
                            self.xo_r[q] = xhor[q] + self.ro_r[q] * uk;
                            self.xo_i[q] = xhoi[q] + self.ro_i[q] * uk;
                        }
                    }
                    self.gk += 1;
                    if self.gk >= FADE as usize * OS {
                        // the glide's end: the old bank goes, the pairs run as the new modes
                        self.glide = false;
                        self.partner = [-1; MAXM];
                    }
                } else {
                    for j in 0..m {
                        self.xr[j] = xhr[j] + rr[j] * uk;
                        self.xi[j] = xhi[j] + ri[j] * uk;
                    }
                }
                let pk = ph + g_k * uk;
                self.uprev = uk;
                self.pprev = pk;
                // steepening: a sample arrives earlier the higher its pressure
                self.sd[self.si] = pk;
                let ri0 = (self.si + SD - SD_D) % SD;
                let adv = (p.nl * self.sd[ri0]).min((SD_D - 2) as f64).max(-((SD - SD_D - 2) as f64));
                let pos = (self.si + SD - SD_D) as f64 + adv;
                let a = pos.floor();
                let fr = pos - a;
                let a = a as usize;
                let ys = self.sd[a % SD] * (1.0 - fr) + self.sd[(a + 1) % SD] * fr;
                self.si = (self.si + 1) % SD;
                // the anti-alias filter's history, for the base rate
                self.dpos = if self.dpos == 0 { ndec - 1 } else { self.dpos - 1 };
                self.dp[self.dpos] = ys;
                self.dp[self.dpos + ndec] = ys;
                self.du[self.dpos] = uk;
                self.du[self.dpos + ndec] = uk;
            }
            let mut pb = 0.0;
            let mut ub = 0.0;
            for j in 0..ndec {
                pb += dec[j] * self.dp[self.dpos + j];
                ub += dec[j] * self.du[self.dpos + j];
            }
            if REED && self.flow {
                // a reed's output: each note's share of the flow through its own Z H
                *o_sample = self.flow_out::<FMA>(ub);
                continue;
            }
            self.hpos_u = if self.hpos_u == 0 { HMAX - 1 } else { self.hpos_u - 1 };
            self.hu[self.hpos_u] = ub;
            self.hu[self.hpos_u + HMAX] = ub;
            let hist_u = &self.hu[self.hpos_u..];
            let mut a = self.r.as_ref().map_or(0.0, |r| dot::<FMA>(r, hist_u));
            if self.fade > 0 {
                // the part the modes leave out crossfades with the bell's filter
                let g = f64::from(self.fade) / f64::from(FADE);
                let old = self.r_old.as_ref().map_or(0.0, |r| dot::<FMA>(r, hist_u));
                a = a * (1.0 - g) + old * g;
            }
            a += pb;
            self.hpos_t = if self.hpos_t == 0 { HMAX - 1 } else { self.hpos_t - 1 };
            self.ht[self.hpos_t] = a;
            self.ht[self.hpos_t + HMAX] = a;
            let hist_t = &self.ht[self.hpos_t..];
            let mut y = dot::<FMA>(&self.h[..self.nh], hist_t);
            if self.fade > 0 {
                let yo = dot::<FMA>(&self.h_old[..self.nh_old], hist_t);
                let g = f64::from(self.fade) / f64::from(FADE);
                y = y * (1.0 - g) + yo * g;
                self.fade -= 1;
            }
            *o_sample = y;
        }
    }
}
