//! `--video`: the instrument as the engine plays it, as the plugin's Visualization panel draws it
//! (each key's strings from above, a sounding string a glowing band as wide as its partials move it,
//! exaggerated, the strings ringing along larger; the dampers on them; a piano's hammers from the
//! front; the keys; bars on a bar instrument). A second render of the piece gives each frame's
//! state (the engine is deterministic: the audio's own); the frames are drawn on every core and
//! piped into ffmpeg, which takes out.wav as the sound.
//!
//! Offline the score is known ahead, so a hammer rises to the string before its note at its own
//! speed (live, a note-on comes as the hammer arrives).

use std::io::Write;
use std::process::{Command, Stdio};
use std::time::Instant;

use crate::info::Info;
use crate::telemetry::{KEY_DAMPER, KEY_HAMMER, KeyFrame, VIZ_PARTIALS};
use crate::{Engine, Event, Instrument, Mix, Params, RadiationCache, Tuning};
use tiny_skia::{
    BlendMode, Color, FillRule, GradientStop, LinearGradient, Paint, Path, PathBuilder, Pixmap, Point, RadialGradient, Rect,
    Shader, SpreadMode, Stroke, Transform,
};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

/// ffmpeg's output options unless `--ffmpeg` gives others
pub const DEFAULT_FFMPEG: &str = "-c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 256k -movflags +faststart";
/// the same with `--transparent`: ProRes 4444 keeps the alpha (a .mov)
pub const TRANSPARENT_FFMPEG: &str = "-c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le -c:a pcm_s16le";

/// The picture's look: `Classic` the plugin's own, `Deco` black, gold and ivory (art deco)
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Theme {
    #[default]
    Classic,
    Deco,
}

impl std::str::FromStr for Theme {
    type Err = String;
    fn from_str(s: &str) -> std::result::Result<Self, String> {
        match s.trim().to_ascii_lowercase().as_str() {
            "classic" => Ok(Theme::Classic),
            "deco" => Ok(Theme::Deco),
            _ => Err("classic or deco".into()),
        }
    }
}

/// The colors a theme gives the picture.
struct Palette {
    theme: Theme,
    bg: [Color; 2],
    idle_string: Color,
    pin: Color,
    bar: Color,
    front: Color,
    shank: Color,
    hammer: [u8; 3],
    white: Color,
    white_held_base: [f32; 3],
    black: Color,
    pedal_track: Color,
    pedal: Color,
}

impl Palette {
    fn of(theme: Theme) -> Self {
        match theme {
            Theme::Classic => Palette {
                theme,
                bg: [Color::from_rgba8(8, 10, 22, 255), Color::from_rgba8(18, 16, 26, 255)],
                idle_string: Color::from_rgba8(70, 74, 88, 255),
                pin: Color::from_rgba8(110, 100, 80, 255),
                bar: Color::from_rgba8(46, 45, 52, 255),
                front: Color::from_rgba8(90, 90, 100, 255),
                shank: Color::from_rgba8(120, 95, 70, 255),
                hammer: [225, 215, 195],
                white: Color::from_rgba8(235, 232, 222, 255),
                white_held_base: [0.92, 0.91, 0.87],
                black: gray(22),
                pedal_track: Color::from_rgba8(40, 42, 52, 255),
                pedal: Color::from_rgba8(230, 170, 90, 255),
            },
            Theme::Deco => Palette {
                theme,
                bg: [Color::from_rgba8(5, 4, 3, 255), Color::from_rgba8(16, 11, 6, 255)],
                idle_string: Color::from_rgba8(92, 72, 40, 255),
                pin: Color::from_rgba8(212, 175, 90, 255),
                bar: Color::from_rgba8(52, 40, 22, 255),
                front: Color::from_rgba8(190, 150, 70, 255),
                shank: Color::from_rgba8(150, 115, 55, 255),
                hammer: [240, 228, 200],
                white: Color::from_rgba8(240, 232, 212, 255),
                white_held_base: [0.94, 0.9, 0.8],
                black: Color::from_rgba8(14, 11, 8, 255),
                pedal_track: Color::from_rgba8(40, 31, 18, 255),
                pedal: Color::from_rgba8(222, 182, 92, 255),
            },
        }
    }

    /// key 0 = A0 … 87 = C8 (0 … 1 each channel)
    fn hue(&self, key: usize) -> [f32; 3] {
        match self.theme {
            Theme::Classic => hue(key),
            Theme::Deco => {
                // copper in the bass, through amber and gold, to champagne and pearl in the treble
                const STOPS: [[f32; 3]; 5] = [[0.86, 0.42, 0.2], [1.0, 0.62, 0.22], [1.0, 0.78, 0.34], [1.0, 0.88, 0.6], [0.96, 0.93, 0.84]];
                let x = (key as f32 / 87.0).clamp(0.0, 1.0) * 4.0;
                let i = (x as usize).min(3);
                let f = x - i as f32;
                std::array::from_fn(|c| STOPS[i][c] * (1.0 - f) + STOPS[i + 1][c] * f)
            }
        }
    }
}

pub struct VideoOpts {
    pub path: String,
    pub width: u32,
    pub height: u32,
    pub fps: f64,
    /// ffmpeg's output options, split at spaces (None: `DEFAULT_FFMPEG` or `TRANSPARENT_FFMPEG`)
    pub ffmpeg: Option<String>,
    /// no background: the frames' alpha for compositing
    pub transparent: bool,
    /// the picture's look
    pub theme: Theme,
}

/// "1920x1080" → (1920, 1080): even sides (most codecs need them), 64 … 8192
pub fn parse_size(s: &str) -> std::result::Result<(u32, u32), String> {
    let usage = "WxH in pixels, e.g. 1920x1080";
    let (w, h) = s.split_once(['x', 'X']).ok_or(usage)?;
    let (Ok(w), Ok(h)) = (w.trim().parse::<u32>(), h.trim().parse::<u32>()) else { return Err(usage.into()) };
    if !(64..=8192).contains(&w) || !(64..=8192).contains(&h) || w % 2 == 1 || h % 2 == 1 {
        return Err("even sides from 64 to 8192 pixels (most codecs need even ones)".into());
    }
    Ok((w, h))
}

// the hammers, as the plugin's (plugin/src/ui/Viz.hpp)
/// the hammer's travel from rest to the string (m), the backcheck's height, the fall after a release (heights/s)
const BLOW_M: f64 = 0.047;
const CHECK: f64 = 0.45;
const DROP: f64 = 6.0;
/// at the string this long from the strike (s)
const CONTACT_S: f64 = 0.02;
/// the longest rise before a note (a pp hammer's)
const MAX_RISE_S: f64 = 0.2;

/// A strike on a key: its time (s, the output's), its hammer speed (m/s), the key's release (s; ∞ none)
#[derive(Clone, Copy, Debug)]
struct Strike {
    at: f64,
    speed: f64,
    up: f64,
}

/// Each key's strikes from the score, for the hammers and the keys.
pub struct Hammers {
    keys: Vec<Vec<Strike>>,
}

impl Hammers {
    pub fn new(events: &[(u64, Event)], sr: f64) -> Self {
        let mut keys = vec![Vec::<Strike>::new(); 88];
        for &(t, ev) in events {
            let s = t as f64 / sr;
            match ev {
                Event::NoteOn { key, speed } if (21..=108).contains(&key) => keys[key as usize - 21].push(Strike { at: s, speed, up: f64::INFINITY }),
                Event::KeyUp { key } if (21..=108).contains(&key) => {
                    if let Some(last) = keys[key as usize - 21].last_mut().filter(|l| l.up.is_infinite()) {
                        last.up = s;
                    }
                }
                Event::Silence => {
                    for last in keys.iter_mut().filter_map(|k| k.last_mut()).filter(|l| l.up.is_infinite()) {
                        last.up = s;
                    }
                }
                _ => {}
            }
        }
        Hammers { keys }
    }

    /// strike `i`'s rise begins: before it by the time its speed takes over twice the blow (it
    /// accelerates), at most MAX_RISE_S, never before the key's previous strike has happened
    fn rise_start(&self, k: usize, i: usize) -> f64 {
        let s = &self.keys[k][i];
        let rise = (2.0 * BLOW_M / s.speed.max(0.3)).min(MAX_RISE_S);
        let earliest = if i > 0 { self.keys[k][i - 1].at + CONTACT_S } else { f64::NEG_INFINITY };
        (s.at - rise).max(earliest).min(s.at)
    }

    /// Key `k`'s hammer at time `t` (s): 0 at rest … 1 at the string.
    pub fn height(&self, k: usize, t: f64) -> f64 {
        self.height_upto(k, self.keys[k].len(), t)
    }

    /// the height from the key's first `n` strikes alone
    fn height_upto(&self, k: usize, n: usize, t: f64) -> f64 {
        let notes = &self.keys[k][..n];
        let last = notes.partition_point(|s| s.at - MAX_RISE_S <= t);
        let Some(i) = (0..last).rev().find(|&i| self.rise_start(k, i) <= t) else { return 0.0 };
        let s = &notes[i];
        if t >= s.at {
            return after(s, t);
        }
        // rising from where the hammer was (the earlier strikes' alone: never above the string)
        let from = self.rise_start(k, i);
        let start = self.height_upto(k, i, from);
        let f = (t - from) / (s.at - from);
        (start + (1.0 - start) * f * f).clamp(0.0, 1.0)
    }

    /// the strike on key `k` at or before `t`, if within `window` s
    fn struck(&self, k: usize, t: f64, window: f64) -> Option<&Strike> {
        let notes = &self.keys[k];
        let i = notes.partition_point(|s| s.at <= t);
        (i > 0 && t - notes[i - 1].at < window).then(|| &notes[i - 1])
    }

    /// the key is down at `t`: its hammer speed (m/s)
    fn held(&self, k: usize, t: f64) -> Option<f64> {
        let notes = &self.keys[k];
        let i = notes.partition_point(|s| s.at <= t);
        (i > 0 && t < notes[i - 1].up).then(|| notes[i - 1].speed)
    }
}

/// after the strike: at the string for CONTACT_S, back at half its speed, caught by the backcheck
/// while the key is held, then down to rest (a key released on the way back goes on at its speed)
fn after(s: &Strike, t: f64) -> f64 {
    let back = 0.5 * s.speed.max(0.3) / BLOW_M;
    let rebound = |x: f64| (1.0 - back * (x - s.at - CONTACT_S)).min(1.0);
    if t <= s.up {
        return rebound(t).max(CHECK);
    }
    let y = if rebound(s.up) > CHECK { rebound(t) } else { CHECK - DROP * (t - s.up) };
    y.clamp(0.0, 1.0)
}

/// One frame's state: the keys as the engine has them, the pedals (soft, sostenuto, sustain: 0 … 1).
#[derive(Clone)]
struct State {
    keys: Box<[KeyFrame; 88]>,
    pedals: [f32; 3],
}

fn empty_keys() -> Box<[KeyFrame; 88]> {
    // SAFETY: KeyFrame is plain old data; all-zero is valid
    unsafe { Box::<[KeyFrame; 88]>::new_zeroed().assume_init() }
}

/// the strings ringing along, drawn this much larger than they move (as the plugin)
const RING_GAIN: f32 = 2.5;
/// a string's level (m) from which it is drawn
const SOUNDING: f32 = 3e-8;
/// points along a string
const SAMPLES: usize = 40;

/// key 0 = A0 … 87 = C8: warm in the bass to cool in the treble (0 … 1)
fn hue(key: usize) -> [f32; 3] {
    const STOPS: [[f32; 3]; 5] = [[1.0, 0.35, 0.16], [1.0, 0.75, 0.24], [0.47, 0.9, 0.55], [0.27, 0.75, 1.0], [0.67, 0.43, 1.0]];
    let x = (key as f32 / 87.0).clamp(0.0, 1.0) * 4.0;
    let i = (x as usize).min(3);
    let f = x - i as f32;
    std::array::from_fn(|c| STOPS[i][c] * (1.0 - f) + STOPS[i + 1][c] * f)
}

fn color(rgb: [f32; 3], scale: f32, alpha: f32) -> Color {
    Color::from_rgba((rgb[0] * scale).clamp(0.0, 1.0), (rgb[1] * scale).clamp(0.0, 1.0), (rgb[2] * scale).clamp(0.0, 1.0), alpha.clamp(0.0, 1.0)).unwrap()
}

fn gray(v: u8) -> Color {
    Color::from_rgba8(v, v, v, 255)
}

fn solid(c: Color) -> Paint<'static> {
    let mut p = Paint::default();
    p.set_color(c);
    p.anti_alias = true;
    p
}

/// Light added onto the picture. `light`: over a transparent background, its alpha as bright as its
/// brightest channel (premultiplied (r, g, b, max)): it adds where it lies on something and composites
/// as light where it lies on nothing.
fn additive(c: Color, light: bool) -> Paint<'static> {
    let mut p = solid(if light { as_light(c) } else { c });
    p.blend_mode = BlendMode::Plus;
    p
}

/// `c` (its alpha a weight) as light: premultiplied (r·a, g·a, b·a) with the alpha max(r, g, b)·a
fn as_light(c: Color) -> Color {
    let m = c.red().max(c.green()).max(c.blue());
    if m <= 0.0 {
        return Color::TRANSPARENT;
    }
    Color::from_rgba(c.red() / m, c.green() / m, c.blue() / m, m * c.alpha()).unwrap_or(Color::TRANSPARENT)
}

fn rounded(x: f32, y: f32, w: f32, h: f32, r: f32) -> Option<Path> {
    let r = r.min(w / 2.0).min(h / 2.0);
    let mut pb = PathBuilder::new();
    pb.move_to(x + r, y);
    pb.line_to(x + w - r, y);
    pb.quad_to(x + w, y, x + w, y + r);
    pb.line_to(x + w, y + h - r);
    pb.quad_to(x + w, y + h, x + w - r, y + h);
    pb.line_to(x + r, y + h);
    pb.quad_to(x, y + h, x, y + h - r);
    pb.line_to(x, y + r);
    pb.quad_to(x, y, x + r, y);
    pb.close();
    pb.finish()
}

/// the displacement (m) as drawn: a half width in key pitches, compressed (as the plugin)
fn pitches(metres: f32) -> f32 {
    (0.45 * (metres.abs() / 1.2e-3).sqrt()).min(0.75)
}

/// the drawn length of a string, compressed (as the plugin)
fn string_px(length: f32, longest: f32, px: f32) -> f32 {
    if length > 0.0 && longest > 0.0 { px * (length / longest).min(1.0).powf(0.6) } else { px * 0.5 }
}

/// sin(kπu) for partial k + 1 at the points along a string
type Sines = [[f32; VIZ_PARTIALS]; SAMPLES + 1];

fn sines() -> Box<Sines> {
    Box::new(std::array::from_fn(|i| std::array::from_fn(|k| (std::f32::consts::PI * (k + 1) as f32 * i as f32 / SAMPLES as f32).sin())))
}

/// sqrt(Σ a_k² sin²(kπu)) and Σ a_k cos φ_k sin(kπu) at point `row` (its sines)
fn envelope(amp: &[f32], row: &[f32; VIZ_PARTIALS]) -> f32 {
    amp.iter().zip(row).map(|(a, s)| (a * s) * (a * s)).sum::<f32>().sqrt()
}

fn snapshot(amp_cos: &[f32], row: &[f32; VIZ_PARTIALS]) -> f32 {
    amp_cos.iter().zip(row).map(|(a, s)| a * s).sum()
}

/// Adds `glow` (premultiplied RGBA, a fraction of the size), scaled up bilinearly and times
/// `gain`, onto `out`; `light`: its alpha raised by the brightest channel added (a transparent
/// background), else left as it is (an opaque one).
fn add_glow(out: &mut Pixmap, glow: &Pixmap, gain: f32, light: bool) {
    let (w, h) = (out.width() as usize, out.height() as usize);
    let (gw, gh) = (glow.width() as usize, glow.height() as usize);
    let src = glow.data();
    // per output column: the two source columns and the weight of the second
    let cols: Vec<(usize, usize, f32)> = (0..w)
        .map(|x| {
            let f = ((x as f32 + 0.5) * gw as f32 / w as f32 - 0.5).max(0.0);
            let i = (f as usize).min(gw - 1);
            (i, (i + 1).min(gw - 1), f - i as f32)
        })
        .collect();
    let mut a = vec![0.0f32; gw * 3];
    let data = out.data_mut();
    for y in 0..h {
        let f = ((y as f32 + 0.5) * gh as f32 / h as f32 - 0.5).max(0.0);
        let j = (f as usize).min(gh - 1);
        let (j1, fy) = ((j + 1).min(gh - 1), f - j as f32);
        // the source row between the two, color only (the output stays opaque)
        for x in 0..gw {
            for c in 0..3 {
                a[x * 3 + c] = gain * (src[(j * gw + x) * 4 + c] as f32 * (1.0 - fy) + src[(j1 * gw + x) * 4 + c] as f32 * fy);
            }
        }
        let row = &mut data[y * w * 4..(y + 1) * w * 4];
        for (x, &(i0, i1, fx)) in cols.iter().enumerate() {
            let mut most = 0.0f32;
            for c in 0..3 {
                let v = a[i0 * 3 + c] * (1.0 - fx) + a[i1 * 3 + c] * fx;
                most = most.max(v);
                let o = &mut row[x * 4 + c];
                *o = (*o as f32 + v).min(255.0) as u8;
            }
            if light {
                let o = &mut row[x * 4 + 3];
                *o = (*o as f32 + most).min(255.0) as u8;
            }
        }
    }
}

/// A box blur of a premultiplied RGBA image, `passes` times each way (three: near a Gaussian).
fn blur(px: &mut Pixmap, radius: usize, passes: usize) {
    let (w, h) = (px.width() as usize, px.height() as usize);
    let data = px.data_mut();
    let mut line = vec![0u32; w.max(h) * 4];
    let n = (2 * radius + 1) as u32;
    for _ in 0..passes {
        for (len, count, stride, step) in [(w, h, w * 4, 4), (h, w, 4, w * 4)] {
            for row in 0..count {
                let base = row * stride;
                for i in 0..len {
                    for c in 0..4 {
                        line[i * 4 + c] = data[base + i * step + c] as u32;
                    }
                }
                let mut sum = [0u32; 4];
                for i in 0..=radius.min(len - 1) {
                    for c in 0..4 {
                        sum[c] += line[i * 4 + c];
                    }
                }
                for i in 0..len {
                    for c in 0..4 {
                        data[base + i * step + c] = (sum[c] / n) as u8;
                    }
                    let (add, sub) = (i + radius + 1, i as isize - radius as isize);
                    for c in 0..4 {
                        if add < len {
                            sum[c] += line[add * 4 + c];
                        }
                        if sub >= 0 {
                            sum[c] -= line[sub as usize * 4 + c];
                        }
                    }
                }
            }
        }
    }
}

/// The picture's geometry and what stays the same through the piece.
struct Painter {
    w: f32,
    h: f32,
    s: f32,
    pitch: f32,
    margin: f32,
    keys_y: f32,
    keys_bottom: f32,
    /// the pedals with a bar under the keys: soft, sostenuto, sustain
    shown: [bool; 3],
    front: f32,
    top: f32,
    bars: bool,
    hammers_shown: bool,
    longest: f32,
    /// no background (`--transparent`)
    transparent: bool,
    background: Pixmap,
    sines: Box<Sines>,
    pal: Palette,
}

/// a pedal's bar under the keys and the gap between two, at 1080p
const PEDAL_BAR: f32 = 150.0;
const PEDAL_GAP: f32 = 40.0;

/// The pedals that act (soft, sostenuto, sustain), the same rule as the plugin's strip
/// (`engine/tests/no_effect.rs` checks it against renders).
pub fn shown_pedals(i: &Info) -> [bool; 3] {
    [i.soft_pedal, i.dampers, i.dampers]
}

impl Painter {
    #[cfg_attr(not(test), allow(dead_code))] // the tests' classic painter
    fn new(width: u32, height: u32, bars: bool, transparent: bool, shown: [bool; 3], first: &State) -> Self {
        Self::themed(width, height, bars, transparent, shown, first, Theme::Classic)
    }

    #[allow(clippy::too_many_arguments)]
    fn themed(width: u32, height: u32, bars: bool, transparent: bool, shown: [bool; 3], first: &State, theme: Theme) -> Self {
        let pal = Palette::of(theme);
        let (w, h) = (width as f32, height as f32);
        let s = (h / 1080.0).min(w / 1920.0);
        let margin = 40.0 * s;
        let pitch = (w - 2.0 * margin) / 88.0;
        let keys_bottom = h - if shown.contains(&true) { 50.0 } else { 10.0 } * s;
        let keys_y = keys_bottom - 120.0 * s;
        let hammers_shown = !bars && first.keys.iter().any(|k| k.flags & KEY_HAMMER != 0);
        let front = if hammers_shown { keys_y - 110.0 * s } else { keys_y - 30.0 * s };
        let longest = first.keys.iter().map(|k| k.length).fold(0.0, f32::max);
        let mut background = Pixmap::new(width, height).unwrap();
        let shader = LinearGradient::new(
            Point::from_xy(0.0, 0.0),
            Point::from_xy(0.0, h),
            vec![GradientStop::new(0.0, pal.bg[0]), GradientStop::new(1.0, pal.bg[1])],
            SpreadMode::Pad,
            Transform::identity(),
        );
        if transparent {
            // nothing: the pixmap starts transparent
        } else if let Some(shader) = shader {
            let paint = Paint { shader, ..Paint::default() };
            background.fill_rect(Rect::from_xywh(0.0, 0.0, w, h).unwrap(), &paint, Transform::identity(), None);
        } else {
            background.fill(pal.bg[1]);
        }
        if !transparent && theme == Theme::Deco {
            deco_background(&mut background, w, h, s, margin, keys_y);
        }
        Painter { w, h, s, pitch, margin, keys_y, keys_bottom, shown, front, top: 60.0 * s, bars, hammers_shown, longest, transparent, background, sines: sines(), pal }
    }

    fn x(&self, k: usize) -> f32 {
        self.margin + self.pitch * (k as f32 + 0.5)
    }

    /// Frame `index` at time `t` (s) into `out` (its glow drawn in `glow`, a quarter the size).
    #[allow(clippy::too_many_arguments)]
    fn draw(&self, st: &State, hammers: &Hammers, t: f64, fps: f64, index: u64, out: &mut Pixmap, glow: &mut Pixmap) {
        out.data_mut().copy_from_slice(self.background.data());
        glow.fill(Color::TRANSPARENT);
        let quarter = Transform::from_scale(glow.width() as f32 / self.w, glow.height() as f32 / self.h);
        let mut seed = index.wrapping_mul(0x9E37_79B9_7F4A_7C15) | 1;
        let mut rand = move || {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            (seed >> 40) as f32 / (1u64 << 24) as f32
        };
        let s = self.s;
        let (field_top, field_bottom) = if self.bars { (self.top, self.keys_y - 30.0 * s) } else { (self.top, self.front) };
        let room = field_bottom - field_top;
        for (k, key) in st.keys.iter().enumerate() {
            let x = self.x(k);
            let len = string_px(key.length, self.longest, room);
            let (a, b) = if self.bars { let mid = (field_top + field_bottom) / 2.0; (mid + len / 2.0, mid - len / 2.0) } else { (self.front, self.front - len) };
            let amp: [f32; VIZ_PARTIALS] = std::array::from_fn(|i| key.amp[i].hypot(RING_GAIN * key.ring[i]));
            let level = (0.5 * amp.iter().map(|x| x * x).sum::<f32>()).sqrt();
            let c = self.pal.hue(k);
            if self.bars {
                let bw = self.pitch * 0.72;
                if let Some(p) = rounded(x - bw / 2.0, b, bw, len, 3.0 * s) {
                    out.fill_path(&p, &solid(self.pal.bar), FillRule::Winding, Transform::identity(), None);
                }
            } else {
                // the idle string, where it does not ring (a ringing one is its band alone)
                let mut pb = PathBuilder::new();
                pb.move_to(x, a);
                pb.line_to(x, b);
                if level <= SOUNDING
                    && let Some(p) = pb.finish()
                {
                    out.stroke_path(&p, &solid(self.pal.idle_string), &Stroke { width: s.max(1.0), ..Stroke::default() }, Transform::identity(), None);
                }
                if let Some(p) = PathBuilder::from_circle(x, b, 2.2 * s) {
                    out.fill_path(&p, &solid(self.pal.pin), FillRule::Winding, Transform::identity(), None);
                }
            }
            if level > SOUNDING {
                let bright = (level / 6e-4).powf(0.35).clamp(0.08, 1.0);
                let along = |i: usize| i as f32 / SAMPLES as f32;
                let y = |u: f32| a + (b - a) * u;
                let width: [f32; SAMPLES + 1] = std::array::from_fn(|i| self.pitch * pitches(envelope(&amp, &self.sines[i])) + 0.6 * s);
                let mut pb = PathBuilder::new();
                pb.move_to(x - width[0], y(0.0));
                for (i, w) in width.iter().enumerate().skip(1) {
                    pb.line_to(x - w, y(along(i)));
                }
                for (i, w) in width.iter().enumerate().rev() {
                    pb.line_to(x + w, y(along(i)));
                }
                pb.close();
                if let Some(band) = pb.finish() {
                    glow.fill_path(&band, &additive(color(c, bright, 1.0), false), FillRule::Winding, quarter, None);
                    out.fill_path(&band, &additive(color(c, bright * 0.55, 1.0), self.transparent), FillRule::Winding, Transform::identity(), None);
                }
                let amp_cos: [f32; VIZ_PARTIALS] = std::array::from_fn(|k| amp[k] * (rand() * std::f32::consts::TAU).cos());
                let mut pb = PathBuilder::new();
                for i in 0..=SAMPLES {
                    let u = along(i);
                    let d = snapshot(&amp_cos, &self.sines[i]);
                    let dx = d.signum() * self.pitch * pitches(d) * if self.bars { 0.6 } else { 1.0 };
                    if i == 0 { pb.move_to(x + dx, y(u)) } else { pb.line_to(x + dx, y(u)) }
                }
                if let Some(p) = pb.finish() {
                    let line = color(c, 0.45 + 1.1 * bright, 1.0);
                    out.stroke_path(&p, &additive(line, self.transparent), &Stroke { width: 2.0 * s, ..Stroke::default() }, Transform::identity(), None);
                }
            }
            // a strike's or pluck's flash at its point
            if let Some(hit) = hammers.struck(k, t, 0.15) {
                let fade = (1.0 - (t - hit.at) / 0.15) as f32;
                let fy = if key.flags & KEY_HAMMER != 0 && !self.bars { self.front } else if self.bars { (a + b) / 2.0 } else { a + (b - a) * key.point.clamp(0.0, 1.0) };
                let r = 22.0 * s;
                if let (Some(shader), Some(p)) = (radial(x, fy, r, color(c, 1.2, fade), self.transparent), PathBuilder::from_circle(x, fy, r)) {
                    let paint = Paint { shader, blend_mode: BlendMode::Plus, anti_alias: true, ..Paint::default() };
                    out.fill_path(&p, &paint, FillRule::Winding, Transform::identity(), None);
                }
            }
            // the dampers: on the strings near the front (a bar's damper under its end), lifted ones raised and faint
            if key.flags & KEY_DAMPER != 0 {
                let e = key.engage.clamp(0.0, 1.0);
                let shade = (40.0 + 50.0 * e) as u8;
                let fill = Color::from_rgba8(shade, shade.saturating_sub(8), shade.saturating_sub(16), (255.0 * (0.45 + 0.55 * e)) as u8);
                let (dy, dh, dw) = if self.bars { (a - 8.0 * s + (1.0 - e) * 6.0 * s, 6.0 * s, self.pitch * 0.72) } else { (a - (0.16 * len).max(12.0 * s) - (1.0 - e) * 10.0 * s - 7.0 * s, 14.0 * s, self.pitch * 0.76) };
                if let Some(p) = rounded(x - dw / 2.0, dy, dw, dh, 2.0 * s) {
                    out.fill_path(&p, &solid(fill), FillRule::Winding, Transform::identity(), None);
                }
            }
        }
        blur(glow, 3, 3);
        add_glow(out, glow, 2.0, self.transparent);
        if !self.bars {
            let mut pb = PathBuilder::new();
            pb.move_to(self.margin, self.front);
            pb.line_to(self.w - self.margin, self.front);
            if let Some(p) = pb.finish() {
                out.stroke_path(&p, &solid(self.pal.front), &Stroke { width: s.max(1.0), ..Stroke::default() }, Transform::identity(), None);
            }
        }
        if self.hammers_shown {
            self.draw_hammers(hammers, t, fps, out);
        }
        self.draw_keys(hammers, t, out);
        self.draw_pedals(st.pedals, out);
    }

    fn draw_hammers(&self, hammers: &Hammers, t: f64, fps: f64, out: &mut Pixmap) {
        let s = self.s;
        let travel = self.keys_y - self.front - 34.0 * s;
        for k in 0..88 {
            let x = self.x(k);
            let now = hammers.height(k, t);
            // a few exposures over the frame for the motion, then the hammer on its shank
            for (back, weight) in [(0.75, 0.3), (0.5, 0.45), (0.25, 0.65), (0.0, 1.0)] {
                let h = if back == 0.0 { now } else { hammers.height(k, t - back / fps) };
                if back > 0.0 && (h - now).abs() < 0.01 {
                    continue;
                }
                let y = self.front + 4.0 * s + (1.0 - h as f32) * travel;
                if back == 0.0 {
                    let mut pb = PathBuilder::new();
                    pb.move_to(x, y + 14.0 * s);
                    pb.line_to(x, self.keys_y - 8.0 * s);
                    if let Some(p) = pb.finish() {
                        out.stroke_path(&p, &solid(self.pal.shank), &Stroke { width: 2.0 * s, ..Stroke::default() }, Transform::identity(), None);
                    }
                }
                // the earlier exposures see-through, fainter the older
                if let Some(p) = rounded(x - self.pitch * 0.4, y, self.pitch * 0.8, 16.0 * s, 5.0 * s) {
                    out.fill_path(&p, &solid(Color::from_rgba8(self.pal.hammer[0], self.pal.hammer[1], self.pal.hammer[2], (255.0 * weight) as u8)), FillRule::Winding, Transform::identity(), None);
                }
            }
        }
    }

    fn draw_keys(&self, hammers: &Hammers, t: f64, out: &mut Pixmap) {
        let s = self.s;
        let black = |n: usize| matches!(n % 12, 1 | 3 | 6 | 8 | 10);
        let whites: Vec<usize> = (21..=108).filter(|&n| !black(n)).collect();
        let ww = (self.w - 2.0 * self.margin) / whites.len() as f32;
        let bottom = self.keys_bottom;
        let mut wx = [0.0f32; 128];
        for (i, &n) in whites.iter().enumerate() {
            let x0 = self.margin + i as f32 * ww;
            wx[n] = x0;
            let held = hammers.held(n - 21, t);
            let fill = match held {
                Some(v) => {
                    let f = 0.55 + 0.4 * (v as f32 / 5.0).min(1.0);
                    let c = self.pal.hue(n - 21);
                    let b = self.pal.white_held_base;
                    Color::from_rgba((b[0] * (1.0 - f) + c[0] * f).min(1.0), (b[1] * (1.0 - f) + c[1] * f).min(1.0), (b[2] * (1.0 - f) + c[2] * f).min(1.0), 1.0).unwrap()
                }
                None => self.pal.white,
            };
            let y0 = self.keys_y + if held.is_some() { 3.0 * s } else { 0.0 };
            if let Some(r) = Rect::from_xywh(x0 + 0.5 * s, y0, ww - 1.5 * s, bottom - y0) {
                out.fill_rect(r, &solid(fill), Transform::identity(), None);
            }
        }
        for n in (21..=108).filter(|&n| black(n)) {
            let x0 = wx[n - 1] + ww * 0.68;
            let held = hammers.held(n - 21, t).is_some();
            let fill = if held { color(self.pal.hue(n - 21), 0.75, 1.0) } else { self.pal.black };
            if let Some(r) = Rect::from_xywh(x0, self.keys_y, ww * 0.62, 75.0 * s) {
                out.fill_rect(r, &solid(fill), Transform::identity(), None);
            }
            if self.pal.theme == Theme::Deco && !held {
                // ebony with a gold lip at its front edge
                stroke_line(out, &[(x0 + 1.5 * s, self.keys_y + 73.0 * s), (x0 + ww * 0.62 - 1.5 * s, self.keys_y + 73.0 * s)], gold(0.55), 1.2 * s);
            }
        }
    }

    /// each shown pedal's index in `State::pedals` and its bar's left edge: in their order, centered
    fn pedal_bars(&self) -> Vec<(usize, f32)> {
        let (w, gap) = (PEDAL_BAR * self.s, PEDAL_GAP * self.s);
        let which: Vec<usize> = (0..3).filter(|&i| self.shown[i]).collect();
        let n = which.len() as f32;
        let left = self.w / 2.0 - (n * w + (n - 1.0) * gap) / 2.0;
        which.into_iter().enumerate().map(|(j, i)| (i, left + j as f32 * (w + gap))).collect()
    }

    /// the pedals that act, under the middle of the keys, as on a piano: each a bar filled to its depth
    fn draw_pedals(&self, depth: [f32; 3], out: &mut Pixmap) {
        let s = self.s;
        let (w, h) = (PEDAL_BAR * s, 10.0 * s);
        let y = self.keys_bottom + 20.0 * s;
        for (i, x) in self.pedal_bars() {
            let d = depth[i];
            if let Some(p) = rounded(x, y, w, h, h / 2.0) {
                out.fill_path(&p, &solid(self.pal.pedal_track), FillRule::Winding, Transform::identity(), None);
            }
            if d > 0.01
                && let Some(p) = rounded(x, y, w * d.clamp(0.0, 1.0), h, h / 2.0)
            {
                out.fill_path(&p, &solid(self.pal.pedal), FillRule::Winding, Transform::identity(), None);
            }
        }
    }
}

/// gold as drawn on the deco background (its alpha a weight 0 … 1)
fn gold(alpha: f32) -> Color {
    Color::from_rgba(0.83, 0.68, 0.33, alpha.clamp(0.0, 1.0)).unwrap()
}

fn stroke_line(px: &mut Pixmap, pts: &[(f32, f32)], c: Color, width: f32) {
    let mut pb = PathBuilder::new();
    for (i, &(x, y)) in pts.iter().enumerate() {
        if i == 0 { pb.move_to(x, y) } else { pb.line_to(x, y) }
    }
    if let Some(p) = pb.finish() {
        px.stroke_path(&p, &solid(c), &Stroke { width, ..Stroke::default() }, Transform::identity(), None);
    }
}

/// The deco theme's still background: a warm glow, a sunburst rising behind the strings, a double gold
/// frame with stepped corners and a small crest at the top.
fn deco_background(px: &mut Pixmap, w: f32, h: f32, s: f32, margin: f32, keys_y: f32) {
    // the warm glow behind the strings
    let (cx, cy) = (w / 2.0, keys_y * 0.55);
    if let (Some(shader), Some(rect)) = (
        RadialGradient::new(
            Point::from_xy(cx, cy),
            0.0,
            Point::from_xy(cx, cy),
            0.75 * w,
            vec![GradientStop::new(0.0, Color::from_rgba8(78, 50, 16, 150)), GradientStop::new(1.0, Color::from_rgba8(78, 50, 16, 0))],
            SpreadMode::Pad,
            Transform::identity(),
        ),
        Rect::from_xywh(0.0, 0.0, w, h),
    ) {
        px.fill_rect(rect, &Paint { shader, anti_alias: true, ..Paint::default() }, Transform::identity(), None);
    }
    // the sunburst: thin rays fanning up from under the middle of the keys
    let (ox, oy) = (w / 2.0, keys_y + 40.0 * s);
    let reach = h * 1.3;
    let rays = 36;
    for i in 0..rays {
        if i % 2 == 1 {
            continue;
        }
        let a0 = std::f32::consts::PI * (1.0 + i as f32 / rays as f32);
        let a1 = std::f32::consts::PI * (1.0 + (i as f32 + 1.0) / rays as f32);
        let mut pb = PathBuilder::new();
        pb.move_to(ox, oy);
        pb.line_to(ox + reach * a0.cos(), oy + reach * a0.sin());
        pb.line_to(ox + reach * a1.cos(), oy + reach * a1.sin());
        pb.close();
        if let (Some(p), Some(shader)) = (
            pb.finish(),
            RadialGradient::new(
                Point::from_xy(ox, oy),
                0.0,
                Point::from_xy(ox, oy),
                reach,
                vec![GradientStop::new(0.0, gold(0.08)), GradientStop::new(0.6, gold(0.025)), GradientStop::new(1.0, gold(0.0))],
                SpreadMode::Pad,
                Transform::identity(),
            ),
        ) {
            px.fill_path(&p, &Paint { shader, anti_alias: true, ..Paint::default() }, FillRule::Winding, Transform::identity(), None);
        }
    }
    // the double frame
    for (inset, width, alpha) in [(6.0, 1.6, 0.85), (11.0, 0.8, 0.55)] {
        let i = inset * s;
        stroke_line(px, &[(i, i), (w - i, i), (w - i, h - i), (i, h - i), (i, i)], gold(alpha), width * s);
    }
    // stepped corners, as on a deco doorway
    // (the bottom ones short: the keys and pedals come close there)
    for (sx, sy, dx, dy, k) in [(0.0, 0.0, 1.0, 1.0, 1.0), (w, 0.0, -1.0, 1.0, 1.0), (0.0, h, 1.0, -1.0, 0.4), (w, h, -1.0, -1.0, 0.4)] {
        for (j, len) in [60.0f32 * k, 46.0 * k, 32.0 * k].iter().enumerate() {
            let o = (17.0 + 5.0 * j as f32) * s;
            let l = len * s;
            stroke_line(px, &[(sx + dx * o, sy + dy * (o + l)), (sx + dx * o, sy + dy * o), (sx + dx * (o + l), sy + dy * o)], gold(0.75 - 0.15 * j as f32), 1.1 * s);
        }
    }
    // the crest at the top: two rules and a diamond
    let ty = 34.0 * s;
    stroke_line(px, &[(w / 2.0 - 170.0 * s, ty), (w / 2.0 - 18.0 * s, ty)], gold(0.7), 1.0 * s);
    stroke_line(px, &[(w / 2.0 + 18.0 * s, ty), (w / 2.0 + 170.0 * s, ty)], gold(0.7), 1.0 * s);
    let d = 7.0 * s;
    let mut pb = PathBuilder::new();
    pb.move_to(w / 2.0, ty - d);
    pb.line_to(w / 2.0 + d, ty);
    pb.line_to(w / 2.0, ty + d);
    pb.line_to(w / 2.0 - d, ty);
    pb.close();
    if let Some(p) = pb.finish() {
        px.fill_path(&p, &solid(gold(0.9)), FillRule::Winding, Transform::identity(), None);
    }
    // the fallboard over the keys: a dark band edged in gold
    if let Some(r) = Rect::from_xywh(margin - 6.0 * s, keys_y - 12.0 * s, w - 2.0 * margin + 12.0 * s, 12.0 * s) {
        px.fill_rect(r, &solid(Color::from_rgba8(22, 15, 8, 255)), Transform::identity(), None);
    }
    stroke_line(px, &[(margin - 6.0 * s, keys_y - 12.0 * s), (w - margin + 6.0 * s, keys_y - 12.0 * s)], gold(0.9), 1.4 * s);
    stroke_line(px, &[(margin - 6.0 * s, keys_y - 1.0 * s), (w - margin + 6.0 * s, keys_y - 1.0 * s)], gold(0.5), 0.8 * s);
}

fn radial(x: f32, y: f32, r: f32, c: Color, light: bool) -> Option<Shader<'static>> {
    let c = if light { as_light(c) } else { c };
    let mut clear = c;
    clear.set_alpha(0.0);
    RadialGradient::new(Point::from_xy(x, y), 0.0, Point::from_xy(x, y), r, vec![GradientStop::new(0.0, c), GradientStop::new(1.0, clear)], SpreadMode::Pad, Transform::identity())
}

/// Render the video of `events` (`total` output samples at `sr`) with `wav` as its sound.
#[allow(clippy::too_many_arguments)]
pub fn render(v: &VideoOpts, inst: &Instrument, shown: [bool; 3], tuning: Tuning, params: Params, sr: f64, events: &[(u64, Event)], total: u64, wav: &str) -> Result<()> {
    let t0 = Instant::now();
    let mut engine = Engine::with_cache(inst.clone(), sr, tuning, &mut RadiationCache::default())?;
    engine.set_params(params);
    engine.set_mix(Mix::ALL);
    for &(t, ev) in events {
        engine.queue(t, ev);
    }
    let lat = engine.latency() as u64;
    let frames = (total as f64 * v.fps / sr).ceil() as u64;
    let hammers = Hammers::new(events, sr);

    let options = v.ffmpeg.as_deref().unwrap_or(if v.transparent { TRANSPARENT_FFMPEG } else { DEFAULT_FFMPEG });
    let mut ff = Command::new("ffmpeg")
        .args(["-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", &format!("{}x{}", v.width, v.height), "-r", &v.fps.to_string(), "-i", "-", "-i", wav, "-map", "0:v:0", "-map", "1:a:0"])
        .args(options.split_whitespace())
        .args(["-shortest", &v.path])
        .stdin(Stdio::piped())
        .spawn()
        .map_err(|e| format!("--video needs ffmpeg on the PATH ({e})"))?;
    let mut pipe = ff.stdin.take().ok_or("ffmpeg's input")?;

    // the state at each frame's moment (the output lags the engine by its latency)
    let mut pos = 0u64;
    let (mut l, mut r) = (Vec::new(), Vec::new());
    let mut capture = |i: u64, engine: &mut Engine| {
        let target = (i as f64 * sr / v.fps).round() as u64 + lat;
        let n = (target - pos.min(target)) as usize;
        if n > 0 {
            l.resize(n, 0.0);
            r.resize(n, 0.0);
            engine.process(&mut l, &mut r);
            pos += n as u64;
        }
        let mut keys = empty_keys();
        engine.viz_keys(&mut keys);
        State { keys, pedals: [engine.soft_depth(), if engine.sostenuto_down() { 1.0 } else { 0.0 }, engine.pedal_depth()] }
    };
    let first = capture(0, &mut engine);
    let painter = Painter::themed(v.width, v.height, inst.body().is_bars(), v.transparent, shown, &first, v.theme);
    let cores = std::thread::available_parallelism().map_or(1, |n| n.get());
    let mut pixmaps: Vec<(Pixmap, Pixmap)> = (0..cores).map(|_| (Pixmap::new(v.width, v.height).unwrap(), Pixmap::new(v.width.div_ceil(4), v.height.div_ceil(4)).unwrap())).collect();
    // the writer feeds ffmpeg while the next batch is drawn
    let (send, recv) = std::sync::mpsc::sync_channel::<Vec<Vec<u8>>>(1);
    let writer = std::thread::spawn(move || {
        for batch in recv {
            for frame in &batch {
                if pipe.write_all(frame).is_err() {
                    return; // ffmpeg stopped: its status says why
                }
            }
        }
    });
    let mut pending = Some(first);
    let (mut i, report) = (0u64, (frames / 10).max(1));
    while i < frames {
        // a batch of frames, one per core, drawn at once and written in order
        let batch: Vec<(u64, State)> = (i..(i + cores as u64).min(frames)).map(|j| (j, if j == 0 { pending.take().unwrap() } else { capture(j, &mut engine) })).collect();
        let drawn: Vec<Vec<u8>> = std::thread::scope(|s| {
            let handles: Vec<_> = batch
                .iter()
                .zip(pixmaps.iter_mut())
                .map(|((j, st), (out, glow))| {
                    let (painter, hammers) = (&painter, &hammers);
                    s.spawn(move || {
                        painter.draw(st, hammers, *j as f64 / v.fps, v.fps, *j, out, glow);
                        if v.transparent {
                            // ffmpeg reads straight alpha (tiny-skia's is premultiplied; opaque, both are the same)
                            out.pixels().iter().flat_map(|p| {
                                let c = p.demultiply();
                                [c.red(), c.green(), c.blue(), c.alpha()]
                            }).collect()
                        } else {
                            out.data().to_vec()
                        }
                    })
                })
                .collect();
            handles.into_iter().map(|h| h.join().expect("drawing thread")).collect()
        });
        if send.send(drawn).is_err() {
            break;
        }
        let before = i;
        i += batch.len() as u64;
        if i / report != before / report && i < frames {
            eprintln!("  video: {i}/{frames} frames");
        }
    }
    drop(send);
    writer.join().expect("writer thread");
    let status = ff.wait()?;
    if !status.success() {
        return Err(format!("ffmpeg failed ({status}) with the options: {options}").into());
    }
    let secs = t0.elapsed().as_secs_f64();
    println!("  video: {frames} frames, {}×{} at {} fps → {} in {secs:.1} s ({:.1} frames/s)", v.width, v.height, v.fps, v.path, frames as f64 / secs);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hammers(events: &[(f64, Event)]) -> Hammers {
        let sr = 48000.0;
        let timed: Vec<(u64, Event)> = events.iter().map(|&(t, e)| ((t * sr) as u64, e)).collect();
        Hammers::new(&timed, sr)
    }

    #[test]
    fn a_hammer_rises_strikes_and_is_caught() {
        let h = hammers(&[(1.0, Event::NoteOn { key: 60, speed: 2.0 }), (2.0, Event::KeyUp { key: 60 })]);
        let k = 60 - 21;
        assert_eq!(h.height(k, 0.5), 0.0);
        assert!(h.height(k, 0.99) > 0.0 && h.height(k, 0.99) < 1.0, "rising before its note");
        assert!(h.height(k, 0.97) < h.height(k, 0.99));
        assert_eq!(h.height(k, 1.0), 1.0);
        assert_eq!(h.height(k, 1.01), 1.0, "at the string for a moment");
        assert_eq!(h.height(k, 1.5), CHECK, "caught while held");
        assert!(h.height(k, 2.03) < CHECK && h.height(k, 2.03) > 0.0);
        assert_eq!(h.height(k, 3.0), 0.0);
    }

    #[test]
    fn quick_repeats_never_go_through_the_strings() {
        // the spike's glitch: a rise begun before the previous strike read that hammer above the string
        let mut ev = Vec::new();
        for (i, speed) in [0.4, 5.0, 0.3, 3.0, 0.5, 0.3, 4.0].into_iter().enumerate() {
            let t = 1.0 + 0.03 * i as f64;
            ev.push((t, Event::NoteOn { key: 50, speed }));
            if i % 2 == 0 {
                ev.push((t + 0.01, Event::KeyUp { key: 50 }));
            }
        }
        let h = hammers(&ev);
        let k = 50 - 21;
        let mut t = 0.5;
        while t < 2.0 {
            let y = h.height(k, t);
            assert!((0.0..=1.0).contains(&y), "{y} at {t}");
            t += 0.0005;
        }
        // every strike reaches the string at its moment
        for (t, e) in &ev {
            if let Event::NoteOn { .. } = e {
                let at = ((t * 48000.0) as u64) as f64 / 48000.0;
                assert_eq!(h.height(k, at), 1.0, "at {t}");
            }
        }
    }

    #[test]
    fn the_report_picks_the_bars() {
        // what the strip shows (`plugin/tests/visibility.txt`); an export that is not here is skipped
        for (name, want) in [("grand_a", [true; 3]), ("harpsichord", [false, true, true]), ("marimba", [false; 3])] {
            let dir = format!("{}/../instruments/{name}", env!("CARGO_MANIFEST_DIR"));
            let Ok(i) = crate::info::info(&dir) else { continue };
            assert_eq!(shown_pedals(&i), want, "{name}");
        }
    }

    #[test]
    fn the_bars_that_act_stand_centered_under_the_keys() {
        let st = State { keys: empty_keys(), pedals: [1.0; 3] };
        let painter = |shown| Painter::new(1920, 1080, false, false, shown, &st);
        // all three: today's places, to the digit
        assert_eq!(painter([true; 3]).pedal_bars(), vec![(0, 960.0 - 225.0 - 40.0), (1, 960.0 - 75.0), (2, 960.0 + 75.0 + 40.0)]);
        // two: their order kept, around the middle
        assert_eq!(painter([false, true, true]).pedal_bars(), vec![(1, 960.0 - 150.0 - 20.0), (2, 960.0 + 20.0)]);
        // none: no bars, and the keys take the room under them
        let none = painter([false; 3]);
        assert!(none.pedal_bars().is_empty());
        assert_eq!(none.keys_bottom, 1080.0 - 10.0);
        assert_eq!(painter([false, true, true]).keys_bottom, 1080.0 - 50.0);
    }

    #[test]
    fn a_frame_draws_only_the_bars_that_act() {
        // the soft pedal down: its old place stays background, the sustain's bar is lit
        let st = State { keys: empty_keys(), pedals: [1.0, 0.0, 1.0] };
        let h = hammers(&[]);
        let p = Painter::new(320, 180, false, false, [false, true, true], &st);
        let (mut out, mut glow) = (Pixmap::new(320, 180).unwrap(), Pixmap::new(80, 45).unwrap());
        p.draw(&st, &h, 1.0, 30.0, 30, &mut out, &mut glow);
        let y = p.keys_bottom + 25.0 * p.s;
        let bg = p.background.pixel(10, y as u32).unwrap();
        let old_soft = p.w / 2.0 - (150.0 + 40.0) * p.s; // the middle of the left bar of three
        assert_eq!(out.pixel(old_soft as u32, y as u32).unwrap(), bg);
        let (_, sustain) = p.pedal_bars()[1];
        assert_ne!(out.pixel((sustain + 75.0 * p.s) as u32, y as u32).unwrap(), bg);
    }

    #[test]
    fn a_frame_draws_its_strings_and_keys() {
        let mut st = State { keys: empty_keys(), pedals: [0.0, 1.0, 0.5] };
        for (k, key) in st.keys.iter_mut().enumerate() {
            key.length = 2.0 - 1.9 * k as f32 / 87.0;
            key.flags = KEY_HAMMER | KEY_DAMPER;
            key.engage = 1.0;
        }
        st.keys[39].amp[0] = 2e-3;
        st.keys[39].engage = 0.0;
        let h = hammers(&[(0.9, Event::NoteOn { key: 60, speed: 3.0 })]);
        let p = Painter::new(320, 180, false, false, [true; 3], &st);
        let (mut out, mut glow) = (Pixmap::new(320, 180).unwrap(), Pixmap::new(80, 45).unwrap());
        p.draw(&st, &h, 1.0, 30.0, 30, &mut out, &mut glow);
        let at = |x: f32, y: f32| {
            let px = out.pixel(x as u32, y as u32).unwrap();
            px.red() as u32 + px.green() as u32 + px.blue() as u32
        };
        let y = (p.front + p.top) / 2.0;
        assert!(at(p.x(39), y) > 3 * at(p.x(20) + p.pitch * 0.5, y), "the sounding string glows");
        assert!(out.pixels().iter().all(|c| c.alpha() == 255), "opaque");
        // transparent: nothing where nothing is drawn, the sounding string as light, the keys solid
        let p = Painter::new(320, 180, false, true, [true; 3], &st);
        p.draw(&st, &h, 1.0, 30.0, 30, &mut out, &mut glow);
        let alpha = |x: f32, y: f32| out.pixel(x as u32, y as u32).unwrap().alpha();
        assert_eq!(alpha(p.w - 2.0, 2.0), 0, "the corner is clear");
        let lit = out.pixel(p.x(39) as u32, y as u32).unwrap();
        assert!(lit.alpha() > 40 && lit.red().max(lit.green()).max(lit.blue()) >= lit.alpha() - 1, "light: as bright as it is opaque");
        assert_eq!(alpha(p.margin + 2.0, p.keys_bottom - 2.0), 255, "a white key is solid");
    }

    #[test]
    fn the_deco_theme_draws_gold_on_black() {
        let mut st = State { keys: empty_keys(), pedals: [0.0, 1.0, 0.0] };
        for (k, key) in st.keys.iter_mut().enumerate() {
            key.length = 2.0 - 1.9 * k as f32 / 87.0;
            key.flags = KEY_HAMMER | KEY_DAMPER;
            key.engage = 1.0;
        }
        st.keys[39].amp[0] = 2e-3;
        let h = hammers(&[(0.9, Event::NoteOn { key: 60, speed: 3.0 })]);
        let p = Painter::themed(640, 360, false, false, [true; 3], &st, Theme::Deco);
        let (mut out, mut glow) = (Pixmap::new(640, 360).unwrap(), Pixmap::new(160, 90).unwrap());
        p.draw(&st, &h, 1.0, 30.0, 30, &mut out, &mut glow);
        let px = |x: f32, y: f32| out.pixel(x as u32, y as u32).unwrap();
        let frame = px(p.w / 2.0, 6.0 * p.s);
        assert!(frame.red() > frame.blue() + 40, "the frame is gold");
        let string = px(p.x(39), (p.front + p.top) / 2.0);
        assert!(string.red() >= string.green() && string.green() > string.blue(), "a sounding string glows gold");
        let white = px(p.margin + 2.0, p.keys_bottom - 2.0);
        assert!(white.red() > 200 && white.blue() < white.red(), "the white keys are ivory");
        assert!(out.pixels().iter().all(|c| c.alpha() == 255), "opaque");
        assert!("deco".parse::<Theme>() == Ok(Theme::Deco) && "Classic".parse::<Theme>() == Ok(Theme::Classic) && "x".parse::<Theme>().is_err());
    }
}

#[cfg(test)]
/// the drawing's cost per 1080p frame, by hand: `cargo test --release --bin midi2wav frame_cost -- --ignored --nocapture`
mod bench {
    use super::*;
    #[test]
    #[ignore]
    fn frame_cost() {
        let mut st = State { keys: empty_keys(), pedals: [0.0, 1.0, 0.5] };
        for (k, key) in st.keys.iter_mut().enumerate() {
            key.length = 2.0 - 1.9 * k as f32 / 87.0;
            key.flags = KEY_HAMMER | KEY_DAMPER;
            key.engage = 0.0;
            for i in 0..VIZ_PARTIALS { key.amp[i] = 3e-4 / (i + 1) as f32; }
        }
        let h = Hammers::new(&[], 48000.0);
        let p = Painter::new(1920, 1080, false, false, [true; 3], &st);
        let (mut out, mut glow) = (Pixmap::new(1920, 1080).unwrap(), Pixmap::new(480, 270).unwrap());
        let t = Instant::now();
        for i in 0..20 { p.draw(&st, &h, 1.0, 60.0, i, &mut out, &mut glow); }
        eprintln!("frame {:.1} ms", t.elapsed().as_secs_f64() * 50.0);
        let t = Instant::now();
        for _ in 0..20 { blur(&mut glow, 3, 3); }
        eprintln!("blur {:.1} ms", t.elapsed().as_secs_f64() * 50.0);
        let t = Instant::now();
        for _ in 0..20 { add_glow(&mut out, &glow, 2.0, false); }
        eprintln!("upscale {:.1} ms", t.elapsed().as_secs_f64() * 50.0);
        let t = Instant::now();
        for _ in 0..20 { out.data_mut().copy_from_slice(p.background.data()); }
        eprintln!("copy {:.1} ms", t.elapsed().as_secs_f64() * 50.0);
        let t = Instant::now();
        for _ in 0..20 { p.draw_keys(&h, 1.0, &mut out); p.draw_hammers(&h, 1.0, 60.0, &mut out); }
        eprintln!("keys+hammers {:.1} ms", t.elapsed().as_secs_f64() * 50.0);
    }
}
