//! The Rust core against `rt.c`, on a synthetic note and the reed's cases (`proto/brass/core_fixture.py`).
use dussek::brass::core::{Core, CoreParams, ModeBank, Ramp};
use dussek::brass::tables::Lips;
use serde_json::Value;
use std::sync::Arc;

struct Fixture {
    doc: Value,
}

fn list(v: &Value) -> Vec<f64> {
    v.as_array().unwrap().iter().map(|x| x.as_f64().unwrap()).collect()
}

fn noise(n: usize) -> Vec<f32> {
    (0..n)
        .map(|i| {
            let i = i as f64;
            (0.015 * (i * 0.37).sin() * (i * 0.011).cos() + 0.01 * (i * i * 1e-5).sin()) as f32
        })
        .collect()
}

fn fixture() -> Fixture {
    Fixture { doc: serde_json::from_str(include_str!("fixtures/brass_core.json")).unwrap() }
}

fn run(f: &Fixture, scalar: bool) -> Vec<f64> {
    let d = &f.doc;
    let p = &d["params"];
    let params = CoreParams {
        width: p["width"].as_f64().unwrap(),
        y0: p["y0"].as_f64().unwrap(),
        soft: p["soft"].as_f64().unwrap(),
        rho: p["rho"].as_f64().unwrap(),
        nl: p["nl"].as_f64().unwrap(),
    };
    let mu = p["mu"].as_f64().unwrap();
    let mut bank = ModeBank::default();
    let (er, ei, rr, ri) = (list(&d["E"][0]), list(&d["E"][1]), list(&d["r"][0]), list(&d["r"][1]));
    bank.n = er.len();
    bank.er[..bank.n].copy_from_slice(&er);
    bank.ei[..bank.n].copy_from_slice(&ei);
    bank.rr[..bank.n].copy_from_slice(&rr);
    bank.ri[..bank.n].copy_from_slice(&ri);
    let lips = Lips { ratio: list(&d["lips"][0]), q: list(&d["lips"][1]), sign: list(&d["lips"][2]), comp: list(&d["lips"][3]) };
    let dec = list(&d["dec"]);
    let nz = noise(d["noise_n"].as_u64().unwrap() as usize);
    let filters: Vec<(Vec<f32>, Vec<f32>)> = d["filters"]
        .as_array()
        .unwrap()
        .iter()
        .map(|x| (list(&x[0]).iter().map(|v| *v as f32).collect(), list(&x[1]).iter().map(|v| *v as f32).collect()))
        .collect();
    let mut core = Core::new();
    core.force_scalar = scalar;
    core.set_filters(&filters[0].0, std::sync::Arc::from(filters[0].1.clone()));
    let mut out = Vec::new();
    let mut block = [0.0; 64];
    for (b, c) in d["ctl"].as_array().unwrap().iter().enumerate() {
        if let Some(i) = d["change"].get(b.to_string()) {
            let i = i.as_u64().unwrap() as usize;
            core.set_filters(&filters[i].0, std::sync::Arc::from(filters[i].1.clone()));
        }
        let c = list(c);
        let ramp = Ramp { pm: (c[0], c[1]), lip: (c[2], c[3]), burst: (c[4], c[5]), width: (params.width, params.width), tongue: (0.0, 0.0) };
        core.process(&ramp, c[6], mu, &bank, &lips, &params, &nz, &dec, &mut block);
        out.extend_from_slice(&block);
    }
    out
}

fn bank_of(v: &Value) -> ModeBank {
    let mut bank = ModeBank::default();
    let (er, ei, rr, ri) = (list(&v[0]), list(&v[1]), list(&v[2]), list(&v[3]));
    bank.n = er.len();
    bank.er[..bank.n].copy_from_slice(&er);
    bank.ei[..bank.n].copy_from_slice(&ei);
    bank.rr[..bank.n].copy_from_slice(&rr);
    bank.ri[..bank.n].copy_from_slice(&ri);
    bank
}

/// A reed case from rest: its events (a seed, a glide, a flow filter) before their blocks, each block's ramps.
fn run_reed(f: &Fixture, name: &str, scalar: bool) -> Vec<f64> {
    let d = &f.doc["reed"];
    let p = &d["params"];
    let num = |k: &str| p[k].as_f64().unwrap();
    let params = CoreParams { width: num("width_low"), y0: num("y0"), soft: num("soft"), rho: num("rho"), nl: num("nl") };
    let (mu, fr) = (num("mu"), num("reed_hz"));
    let lips = Lips { ratio: list(&d["lips"][0]), q: list(&d["lips"][1]), sign: list(&d["lips"][2]), comp: list(&d["lips"][3]) };
    let dec = list(&f.doc["dec"]);
    let zh: Vec<Arc<[f32]>> = d["zh"].as_array().unwrap().iter().map(|z| list(z).iter().map(|v| *v as f32).collect()).collect();
    let case = &d["cases"][name];
    let mut core = Core::new();
    core.force_scalar = scalar;
    core.set_filters(&[1.0], Arc::from(vec![0.0f32]));
    let nz = [0.0f32];
    let mut out = Vec::new();
    let mut block = [0.0; 64];
    let events = case["events"].as_array().unwrap();
    for (b, c) in case["ctl"].as_array().unwrap().iter().enumerate() {
        for e in events.iter().filter(|e| e[0].as_u64() == Some(b as u64)) {
            match e[1].as_str().unwrap() {
                "seed" => core.start_seed(e[2].as_f64().unwrap(), e[3].as_f64().unwrap(), e[4].as_f64().unwrap()),
                "glide" => {
                    let old = bank_of(&d["banks"][e[2].as_str().unwrap()]);
                    let n = |i: usize| e[i].as_f64().unwrap();
                    core.start_glide(&old, n(3), n(4), n(5), n(6));
                }
                "flow" => assert!(core.change_flow(zh[e[2].as_u64().unwrap() as usize].clone())),
                other => panic!("an event {other}"),
            }
        }
        let a = c.as_array().unwrap();
        let x = |i: usize| a[i].as_f64().unwrap();
        let bank = bank_of(&d["banks"][a[6].as_str().unwrap()]);
        let ramp = Ramp { pm: (x(0), x(1)), lip: (fr, fr), burst: (0.0, 0.0), width: (x(2), x(3)), tongue: (x(4), x(5)) };
        core.process(&ramp, 0.0, mu, &bank, &lips, &params, &nz, &dec, &mut block);
        out.extend_from_slice(&block);
    }
    out
}

const REED_CASES: [&str; 5] = ["seed", "tongue", "glide", "register", "flow"];

/// The largest difference re the peak of `want`.
fn off(got: &[f64], want: &[f64]) -> (f64, usize, f64) {
    assert_eq!(got.len(), want.len());
    let peak = want.iter().fold(0.0f64, |a, x| a.max(x.abs()));
    let (mut worst, mut at) = (0.0f64, 0);
    for (i, (a, b)) in got.iter().zip(want).enumerate() {
        let e = (a - b).abs() / peak;
        if e > worst || e.is_nan() {
            (worst, at) = (e, i);
        }
    }
    (worst, at, peak)
}

#[test]
fn the_reed_is_rt_c() {
    let f = fixture();
    for name in REED_CASES {
        let want = list(&f.doc["reed"]["cases"][name]["out"]);
        let (worst, at, peak) = off(&run_reed(&f, name, false), &want);
        eprintln!("{name}: {worst:.2e} of the peak {peak:.3e} (sample {at})");
        assert!(peak > 0.0, "{name}: silent");
        assert!(worst <= 1e-6, "{name}: sample {at} off by {worst:e} of the peak {peak}");
    }
}

#[test]
fn the_reed_s_vector_path_is_the_scalar_path() {
    let f = fixture();
    for name in REED_CASES {
        let (worst, at, _) = off(&run_reed(&f, name, false), &run_reed(&f, name, true));
        assert!(worst <= 1e-9, "{name}: sample {at} off by {worst:e}");
    }
}

#[test]
fn an_oversize_flow_filter_is_refused() {
    // rt_filters_flow's -1: the core holds 4096 taps of history, a longer filter is never cut short
    let mut core = Core::new();
    assert!(!core.change_flow(Arc::from(vec![0.0f32; 4097])));
    assert!(!core.change_flow(Arc::from(Vec::<f32>::new())));
    assert!(core.change_flow(Arc::from(vec![0.0f32; 4096])));
}

#[test]
fn the_core_is_rt_c() {
    let f = fixture();
    let got = run(&f, false);
    let want = list(&f.doc["out"]);
    assert_eq!(got.len(), want.len());
    let peak = want.iter().fold(0.0f64, |a, x| a.max(x.abs()));
    assert!(peak > 1.0, "the fixture's note sounds: peak {peak}");
    for (i, (a, b)) in got.iter().zip(&want).enumerate() {
        assert!((a - b).abs() <= 1e-6 * peak, "sample {i}: {a} against {b} (peak {peak})");
    }
}

#[test]
fn the_vector_path_is_the_scalar_path() {
    let f = fixture();
    let (a, b) = (run(&f, false), run(&f, true));
    let peak = b.iter().fold(0.0f64, |a, x| a.max(x.abs()));
    for (i, (x, y)) in a.iter().zip(&b).enumerate() {
        assert!((x - y).abs() <= 1e-9 * peak, "sample {i}: {x} against {y}");
    }
}
