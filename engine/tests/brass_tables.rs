//! A brass export read back (`brass::Tables`): point `PE_BRASS_EXPORT` at a scratch export or a committed one.
use dussek::Instrument;
use dussek::brass::tables::Exciter;
use std::path::PathBuf;

fn export() -> Option<PathBuf> {
    let p = PathBuf::from(std::env::var_os("PE_BRASS_EXPORT")?);
    p.join("instrument.json").exists().then_some(p)
}

#[test]
fn tables_read_what_the_export_wrote() {
    let Some(dir) = export() else { return };
    let inst = Instrument::load(&dir).unwrap();
    let t = inst.brass.as_ref().expect("a brass block");
    let doc: serde_json::Value = serde_json::from_slice(&std::fs::read(dir.join("instrument.json")).unwrap()).unwrap();
    let b = &doc["brass"];
    assert_eq!(t.mutes[0], "open");
    assert_eq!(t.table.len(), b["table"].as_array().unwrap().len());
    let key = t.table[t.table.len() / 2];
    let n = t.note(0, key);
    let want = &b["mutes"]["open"]["notes"][key.to_string()];
    assert_eq!(n.f, want["f"].as_f64().unwrap());
    assert_eq!(n.settings[1].0, want["settings"]["mf"][0].as_f64().unwrap());
    assert_eq!(n.h[0].len(), want["h"]["pp"]["n"].as_u64().unwrap() as usize);
    assert_eq!(n.r.len(), want["r"]["n"].as_u64().unwrap() as usize);
    assert_eq!(n.modes[2].len(), want["modes"]["ff"]["sp"]["re"]["n"].as_u64().unwrap() as usize);
    assert!(n.modes[1].sp_im.iter().all(|x| x.is_finite()) && n.h[1].iter().any(|x| *x != 0.0));
    assert_eq!(t.dec.len(), 96);
}

#[test]
fn a_mute_the_instrument_lacks_is_an_error() {
    let Some(dir) = export() else { return };
    let inst = Instrument::load(&dir).unwrap();
    let t = inst.brass.as_ref().unwrap();
    assert!(t.mute_index("no such mute").is_err());
    assert_eq!(t.mute_index("open").unwrap(), 0);
}

#[test]
fn a_brass_export_has_no_notes() {
    // the brass block is the sound: no silent keys for the strings' machinery to idle on, and it plays
    let dir = export().unwrap_or_else(|| PathBuf::from(concat!(env!("CARGO_MANIFEST_DIR"), "/../instruments/trumpet")));
    let Ok(inst) = Instrument::load(&dir) else { return };
    assert!(inst.notes.is_empty(), "{} notes", inst.notes.len());
    assert!(!inst.is_hammer_piano() && inst.registers() == 0);
    assert!(matches!(inst.body(), dussek::mic::Body::Bell(_)));
    let mut e = dussek::Engine::new(inst, 44100.0).unwrap();
    let speed = e.hammer_speed(67, 0.7);
    e.queue(0, dussek::Event::NoteOn { key: 67, speed });
    let (mut l, mut r) = (vec![0.0f32; 22050], vec![0.0f32; 22050]);
    for (a, b) in l.chunks_mut(512).zip(r.chunks_mut(512)) {
        e.process(a, b);
    }
    let rms = (l.iter().map(|v| f64::from(*v).powi(2)).sum::<f64>() / l.len() as f64).sqrt();
    assert!(rms > 1e-4, "rms {rms}");
}

#[test]
fn the_bell_is_posed_as_its_export_says() {
    // the tuba's `[pose]`: up and to the right, the ears under and left of the bell; its radius from the bell's mouth
    let dir = PathBuf::from(concat!(env!("CARGO_MANIFEST_DIR"), "/../instruments/tuba"));
    let Ok(inst) = Instrument::load(&dir) else { return };
    let dussek::mic::Body::Bell(pose) = inst.body() else { panic!("not a bell: {:?}", inst.body()) };
    assert_eq!((pose.azimuth, pose.elevation, pose.ears), (60, 75, [-20, 0, -40]));
    assert_eq!(f64::from(pose.radius_mm) / 1000.0, (inst.brass.as_ref().unwrap().bell_radius * 1000.0).round() / 1000.0);
}

const INSTRUMENTS: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../instruments/");

fn doc_of(dir: &std::path::Path) -> serde_json::Value {
    serde_json::from_slice(&std::fs::read(dir.join("instrument.json")).unwrap()).unwrap()
}

/// `PE_BRASS_EXPORT` when it is a reed's, else the committed clarinet.
fn reed_export() -> PathBuf {
    export().filter(|d| doc_of(d)["brass"]["exciter"] == "reed").unwrap_or_else(|| PathBuf::from(format!("{INSTRUMENTS}clarinet")))
}

#[test]
fn a_reed_export_reads_its_reed() {
    let dir = reed_export();
    let Ok(inst) = Instrument::load(&dir) else { panic!("{} loads", dir.display()) };
    let t = inst.brass.as_ref().expect("a brass block");
    let doc = doc_of(&dir);
    let b = &doc["brass"];
    assert_eq!(t.exciter(), Exciter::Reed);
    let r = t.reed().expect("the reed's settings");
    let num = |k: &str| b[k].as_f64().unwrap();
    assert_eq!((r.hz, r.q, r.k), (num("reed_hz"), num("reed_q"), num("reed_k")));
    assert_eq!((r.tongue_hold, r.tongue_let, r.seed_s), (num("tongue_hold"), num("tongue_let"), num("seed_s")));
    assert_eq!((r.glide_s, r.release_s, r.pair_max, r.pair_tol), (num("glide_s"), num("release_s"), num("pair_max"), num("pair_tol")));
    assert_eq!(r.noise_band, [b["noise_band"][0].as_f64().unwrap(), b["noise_band"][1].as_f64().unwrap()]);
    assert_eq!(r.noise_lp, b["noise_lp"].as_f64());
    assert_eq!(t.table.len(), 33);
    for k in &t.table {
        let n = t.note(0, *k);
        let want = &b["mutes"]["open"]["notes"][k.to_string()];
        let zh = n.zh.as_ref().expect("a reed note's Z H");
        assert_eq!(zh.len(), want["zh"]["n"].as_u64().unwrap() as usize);
        assert!(zh.iter().all(|x| x.is_finite()) && zh.iter().any(|x| *x != 0.0), "key {k}");
        assert!(n.h.iter().all(|h| h.is_empty()) && n.r.is_empty(), "key {k}: the reed radiates its zh alone");
        assert_eq!(n.width, want["width"].as_f64().unwrap());
        for (i, d) in dussek::brass::tables::DYNS.iter().enumerate() {
            assert_eq!(n.breath[i], want["breath"][d].as_f64().unwrap(), "key {k} {d}");
            assert_eq!(n.seed[i], want["seed"][d].as_f64().unwrap(), "key {k} {d}");
        }
    }
    // the widths differ by register: the chalumeau's opening is the narrower
    assert!(t.note(0, t.table[0]).width < t.note(0, *t.table.last().unwrap()).width);
}

#[test]
fn a_lip_export_is_lips() {
    let dir = PathBuf::from(format!("{INSTRUMENTS}trumpet"));
    if !dir.join("instrument.json").exists() {
        return;
    }
    let inst = Instrument::load(&dir).expect("the committed trumpet loads");
    let t = inst.brass.as_ref().unwrap();
    assert_eq!(t.exciter(), Exciter::Lips);
    assert!(t.reed().is_none());
    let n = t.note(0, t.table[t.table.len() / 2]);
    assert!(n.zh.is_none() && !n.h[0].is_empty() && !n.r.is_empty());
}

/// `dir`'s instrument copied to a scratch directory with its document edited.
fn edited(dir: &std::path::Path, tag: &str, edit: impl FnOnce(&mut serde_json::Value)) -> PathBuf {
    let to = std::env::temp_dir().join(format!("dussek-brass-tables-{tag}-{}", std::process::id()));
    std::fs::create_dir_all(&to).unwrap();
    std::fs::copy(dir.join("data.bin"), to.join("data.bin")).unwrap();
    let mut doc = doc_of(dir);
    edit(&mut doc);
    std::fs::write(to.join("instrument.json"), doc.to_string()).unwrap();
    to
}

#[test]
fn a_note_without_its_filters_is_an_error() {
    // a lip note needs its bell's filters and the residual, a reed note its Z H
    let trumpet = PathBuf::from(format!("{INSTRUMENTS}trumpet"));
    let clarinet = PathBuf::from(format!("{INSTRUMENTS}clarinet"));
    if !trumpet.exists() || !clarinet.exists() {
        return;
    }
    let key = |doc: &serde_json::Value| doc["brass"]["table"][0].as_u64().unwrap().to_string();
    for (dir, field) in [(&trumpet, "h"), (&trumpet, "r"), (&clarinet, "zh"), (&clarinet, "width")] {
        let to = edited(dir, field, |doc| {
            let k = key(doc);
            doc["brass"]["mutes"]["open"]["notes"][&k].as_object_mut().unwrap().remove(field);
        });
        let got = Instrument::load(&to);
        std::fs::remove_dir_all(&to).ok();
        let Err(e) = got else { panic!("{}: a note without {field} loads", dir.display()) };
        assert!(e.to_string().contains(&format!("no {field}")), "{field}: {e}");
    }
}

#[test]
fn a_filter_longer_than_the_core_s_history_is_an_error() {
    // the core's histories hold 4096 taps: a longer bell, residual or open bell is refused at load, never on the audio
    // thread
    let trumpet = PathBuf::from(format!("{INSTRUMENTS}trumpet"));
    let tenor = PathBuf::from(format!("{INSTRUMENTS}tenor_trombone"));
    if !trumpet.exists() || !tenor.exists() {
        return;
    }
    let cases: [(&PathBuf, &str, &str, &[&str]); 3] = [(&trumpet, "open", "h", &["h", "pp"]), (&trumpet, "open", "r", &["r"]), (&tenor, "plunger", "open", &["open", "ff"])];
    for (dir, mute, field, path) in cases {
        let to = edited(dir, &format!("long-{field}"), |doc| {
            let k = doc["brass"]["table"][0].as_u64().unwrap().to_string();
            let mut a = &mut doc["brass"]["mutes"][mute]["notes"][&k];
            for p in path {
                a = &mut a[*p];
            }
            a["n"] = serde_json::json!(4097);
        });
        let got = Instrument::load(&to);
        std::fs::remove_dir_all(&to).ok();
        let Err(e) = got else { panic!("{}: a {field} of 4097 taps loads", dir.display()) };
        assert!(e.to_string().contains(&format!("{field} of 4097 taps")), "{field}: {e}");
    }
}

#[test]
fn a_reed_time_not_over_zero_is_an_error() {
    // the reed's articulation times divide its control steps (a glide of 0 s gave 0 / 0): each must be over 0 s
    let clarinet = PathBuf::from(format!("{INSTRUMENTS}clarinet"));
    if !clarinet.exists() {
        return;
    }
    for field in ["tongue_hold", "tongue_let", "seed_s", "glide_s", "release_s"] {
        for bad in [0.0, -0.01] {
            let to = edited(&clarinet, &format!("time-{field}"), |doc| doc["brass"][field] = serde_json::json!(bad));
            let got = Instrument::load(&to);
            std::fs::remove_dir_all(&to).ok();
            let Err(e) = got else { panic!("a reed's {field} of {bad} s loads") };
            assert!(e.to_string().contains(&format!("{field} must be over 0 s")), "{field}: {e}");
        }
    }
}
