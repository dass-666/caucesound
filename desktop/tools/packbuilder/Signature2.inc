// Cauce Signature Sounds 2.0 (misión 0.10). Se incluye dentro del espacio de nombres anónimo de PackBuilder.cpp.
// Paquetes NUEVOS (los 1.0.0 no se tocan: los proyectos guardados suenan igual):
//   cauce.drums.two  — cinco kits con carácter propio por género
//   cauce.bass.two   — 808 Punch, 808 Short y Warm Sub
//   cauce.keys.two   — Warm Electric Piano, Night Pad y Glass Pluck (síntesis original muestreada)
// Todo lo sintetizado usa parámetros y semillas fijas: el resultado es idéntico bit a bit en cada generación.

// --- Utilidades nuevas ---------------------------------------------------------------------------------------------
// Cambio de afinación por remuestreo (Hermite). ratio > 1: más agudo y más corto.
Buf repitch (const Buf& b, double ratio)
{
    if (b.size() < 4) return b;
    Buf out ((size_t) ((double) b.size() / ratio));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double x = (double) i * ratio;
        const long k = (long) x;
        const double f = x - (double) k;
        auto at = [&] (long j) { return j < 0 || j >= (long) b.size() ? 0.0 : (double) b[(size_t) j]; };
        const double y0 = at (k - 1), y1 = at (k), y2 = at (k + 1), y3 = at (k + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        out[i] = (float) (((c3 * f + c2) * f + c1) * f + y1);
    }
    return out;
}
// Saturación asimétrica: añade armónicos pares (calidez) además de impares. bias 0 = simétrica.
Buf warm (Buf b, double drive, double bias)
{
    const double off = std::tanh (bias * drive), n = std::tanh (drive * (1.0 + std::fabs (bias))) ;
    for (float& v : b) v = (float) ((std::tanh ((v + bias) * drive) - off) / n);
    return highpass (b, 18);
}
// Realce o suavizado del ataque: multiplica los primeros 'ms' por una ganancia que vuelve a 1 de forma exponencial.
Buf shapeAttack (Buf b, double gainDb, double ms)
{
    const double g = std::pow (10.0, gainDb / 20.0) - 1.0, tau = ms * 0.001 * sr;
    for (size_t i = 0; i < b.size() && (double) i < tau * 6; ++i) b[i] *= (float) (1.0 + g * std::exp (-(double) i / tau));
    return b;
}
Buf mixed (Buf a, const Buf& b, float g, double delaySec = 0.0) { mixInto (a, b, g, (size_t) (delaySec * sr)); return a; }

// Bombo en tres partes: sub con caída de tono en dos tiempos, cuerpo con 2.º armónico y golpe de maza (tono corto +
// ruido filtrado). Separar las partes es lo que permite que cada género tenga un bombo distinto y no solo otra afinación.
struct KickSpec
{
    double f0, f1;            // Hz al inicio y al final
    double tauFast, tauSlow;  // caída de tono rápida (pegada) y lenta (cuerpo)
    double slowMix;           // parte de la caída que es lenta (0..1)
    double ampTau, hold;      // caída de amplitud y sostén inicial
    double h2;                // 2.º armónico (audible en altavoces pequeños)
    double drive;
    double beaterHz, beaterTau, beaterAmt; // «tok» de la maza
    double noiseAmt, noiseHz;              // ruido del parche
    double length;
    // «Golpe» de medios: seno corto entre 150 y 300 Hz. Es lo que se oye del bombo en el altavoz de un móvil, que
    // apenas reproduce por debajo de 150-200 Hz (medido: sin él, menos del 1 % de la energía quedaba sobre 150 Hz).
    double knockHz = 0, knockTau = 0.04, knockAmt = 0;
};
Buf kick2 (const KickSpec& k, uint32_t seed, double pitchMul = 1.0, double decayMul = 1.0)
{
    Buf b = len (k.length);
    Rng rng (seed);
    Biquad nlp; nlp.set (Biquad::LowPass, sr, k.noiseHz, 0.7);
    Biquad nhp; nhp.set (Biquad::HighPass, sr, 700, 0.7);
    double ph = 0;
    const double dn = std::tanh (k.drive);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double env = (1.0 - k.slowMix) * std::exp (-t / k.tauFast) + k.slowMix * std::exp (-t / k.tauSlow);
        const double f = (k.f1 + (k.f0 - k.f1) * env) * pitchMul;
        ph += f / sr;
        const double amp = (t < k.hold ? 1.0 : std::exp (-(t - k.hold) / (k.ampTau * decayMul))) * std::min (1.0, t / 0.0004);
        double x = (std::sin (2 * kPi * ph) + k.h2 * std::sin (4 * kPi * ph + 0.4)) * amp;
        x = std::tanh (x * k.drive) / dn;
        const double beater = std::sin (2 * kPi * k.beaterHz * t) * std::exp (-t / k.beaterTau) * k.beaterAmt;
        const double noise = nhp.process (nlp.process (rng.next())) * std::exp (-t / 0.006) * k.noiseAmt;
        const double knock = k.knockAmt > 0 ? std::sin (2 * kPi * k.knockHz * pitchMul * (1.0 + 0.5 * std::exp (-t / 0.008)) * t) * std::exp (-t / k.knockTau) * k.knockAmt * std::min (1.0, t / 0.0004) : 0.0;
        b[i] = (float) (x + beater + noise + knock);
    }
    b = highpass (b, 20); // la saturación asimétrica deja continua: se quita
    fadeOut (b, 0.012);
    return b;
}
// Caja sintetizada con dos modos de parche, bordones (ruido en banda con caída en dos tiempos) y golpe de baqueta.
struct SnareSpec { double f1, f2, toneTau, toneMix, wireHp, wireLp, wireTauFast, wireTauSlow, wireSlowMix, stick, length; };
Buf snare2 (const SnareSpec& s, uint32_t seed, double pitchMul = 1.0, double decayMul = 1.0)
{
    Buf b = len (s.length);
    Rng rng (seed);
    Biquad hp, lp, tk; hp.set (Biquad::HighPass, sr, s.wireHp, 0.7); lp.set (Biquad::LowPass, sr, s.wireLp, 0.7); tk.set (Biquad::BandPass, sr, 3800, 1.0);
    double p1 = 0, p2 = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double bend = 1.0 + 0.35 * std::exp (-t / 0.012);
        p1 += s.f1 * pitchMul * bend / sr; p2 += s.f2 * pitchMul * bend / sr;
        const double tone = (std::sin (2 * kPi * p1) + 0.55 * std::sin (2 * kPi * p2)) * std::exp (-t / (s.toneTau * decayMul));
        const double wenv = (1.0 - s.wireSlowMix) * std::exp (-t / (s.wireTauFast * decayMul)) + s.wireSlowMix * std::exp (-t / (s.wireTauSlow * decayMul));
        const double wires = lp.process (hp.process (rng.next())) * wenv;
        const double stick = t < 0.003 ? tk.process (rng.next()) * s.stick * (1.0 - t / 0.003) : tk.process (0.0f) * s.stick;
        b[i] = (float) ((tone * s.toneMix + wires * (1.0 - s.toneMix) * 2.0 + stick) * std::min (1.0, t / 0.0004));
    }
    fadeOut (b, 0.01);
    return b;
}
// Chasquido de dedos: dos ráfagas muy cortas de ruido en banda y un «pop» tonal.
Buf fingerSnap (uint32_t seed, double hz = 2300)
{
    Buf b = len (0.22);
    Rng rng (seed);
    Biquad bp; bp.set (Biquad::BandPass, sr, hz, 2.2);
    Biquad hp; hp.set (Biquad::HighPass, sr, 900, 0.7);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double e1 = std::exp (-t / 0.0016) * 0.5;
        const double t2 = t - 0.011;
        const double e2 = t2 >= 0 ? std::exp (-t2 / 0.016) : 0.0;
        const double pop = t2 >= 0 ? std::sin (2 * kPi * hz * 0.52 * t2) * std::exp (-t2 / 0.006) * 0.5 : 0.0;
        b[i] = (float) (hp.process (bp.process (rng.next())) * (e1 + e2) * 2.2 + pop);
    }
    fadeOut (b, 0.01);
    return b;
}
// Percusión tonal corta (timbal, tom de 808, bloque): seno con caída de tono + ataque.
Buf tonePerc (double hz, double bendSt, double bendTau, double tau, double noise, double length, uint32_t seed)
{
    Buf b = len (length);
    Rng rng (seed);
    Biquad bp; bp.set (Biquad::BandPass, sr, hz * 2.4, 1.5);
    double ph = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        ph += hz * std::pow (2.0, bendSt / 12.0 * std::exp (-t / bendTau)) / sr;
        const double x = (std::sin (2 * kPi * ph) + 0.3 * std::sin (4 * kPi * ph * 1.48)) * std::exp (-t / tau);
        const double n = bp.process (rng.next()) * std::exp (-t / 0.004) * noise;
        b[i] = (float) ((x + n) * std::min (1.0, t / 0.0003));
    }
    fadeOut (b, 0.01);
    return b;
}

const char* kLicense2 =
    "1) Sonidos sintetizados por CAUCESOUND (ver PROVENANCE.md): contenido propio de CAUCESOUND. Se distribuye dentro de la app\n"
    "   para crear música; las canciones creadas con ellos son del usuario sin regalías. No se pueden redistribuir las\n"
    "   muestras sueltas como biblioteca de sonidos sin permiso. (Texto provisional: revisión jurídica pendiente antes de\n"
    "   publicar.)\n"
    "2) Muestras derivadas de VCSL (Versilian Studios LLC): CC0 1.0 Universal (dominio público).\n"
    "   https://creativecommons.org/publicdomain/zero/1.0/  —  https://github.com/sgossner/VCSL\n";

const std::vector<std::string> kSnareV = { "Snare2_HitSN_v3_rr1_Mid.wav", "Snare2_HitSN_v3_rr2_Mid.wav" };
const std::vector<std::string> kSnareM = { "Snare2_HitSN_v6_rr1_Mid.wav", "Snare2_HitSN_v6_rr2_Mid.wav" };
const std::vector<std::string> kSnareF = { "Snare2_HitSN_v9_rr1_Mid.wav", "Snare2_HitSN_v9_rr2_Mid.wav" };
const std::vector<std::string> kXstick = { "Snare4_Xstick_v2_rr1_Mid.wav", "Snare4_Xstick_v2_rr2_Mid.wav" };
const std::vector<std::string> kHatSoft = { "HiHat_HitC_v1_rr1_Mid.wav", "HiHat_HitC_v1_rr2_Mid.wav" };
const std::vector<std::string> kHatMid = { "HiHat_HitC_v2_rr1_Mid.wav", "HiHat_HitC_v2_rr2_Mid.wav", "HiHat_HitC_v3_rr1_Mid.wav", "HiHat_HitC_v3_rr2_Mid.wav" };
const std::vector<std::string> kHatHard = { "HiHat_HitC_v4_rr1_Mid.wav", "HiHat_HitC_v4_rr2_Mid.wav" };
const std::vector<std::string> kHatOpen = { "HiHat_HitO_rr1_Mid.wav", "HiHat_HitO_rr2_Mid.wav" };
const std::vector<std::string> kClapAll = { "Clap_rr1.wav", "Clap_rr2.wav", "Clap_rr3.wav", "Clap_rr4.wav", "Clap_rr5.wav", "Clap_rr6.wav" };

// Tres capas de fuerza de un bombo sintetizado: más fuerte = más pegada, más saturación y más maza (no solo volumen).
void kickLayers (Piece& p, KickSpec base, uint32_t seed, const std::function<Buf (Buf, int)>& post, const char* what)
{
    for (int l = 0; l < 3; ++l)
    {
        KickSpec k = base;
        k.drive *= 0.75 + 0.25 * l;
        k.beaterAmt *= 0.45 + 0.4 * l;
        k.noiseAmt *= 0.4 + 0.45 * l;
        k.knockAmt *= 0.6 + 0.3 * l;
        k.f0 = base.f1 + (base.f0 - base.f1) * (0.7 + 0.2 * l);
        addSynthLayer (p, 3, [&, k, l] (int t) { return post (kick2 (k, seed + 10 * l + t, rrPitch (t), rrDecay (t)), l); }, what);
    }
}

// ==================================================================================================================
// 1) Cauce Crate Boom Bap — bombo gordo y polvoriento, caja real afinada abajo con sala, charles oscuros
// ==================================================================================================================
std::string crateBoomBap (PackWriter& w)
{
    const char* kit = "crate";
    std::string sfz = "// Cauce Crate Boom Bap — CAUCESOUND. Síntesis propia + VCSL (CC0) procesado. Ver PROVENANCE.md\n";
    {
        Piece p { "kick", 36, 0.95f, kL3 };
        kickLayers (p, { 150, 57, 0.012, 0.07, 0.35, 0.21, 0.02, 0.45, 2.9, 1500, 0.004, 0.5, 0.5, 3500, 0.7, 185, 0.05, 0.75 }, 5100,
                    [] (Buf b, int) { b = lowpass (b, 4200); b = grit (b, 12); addRoom (b, 0.16, 0.05, 91); return b; },
                    "bombo en tres partes 150→57 Hz, 2.º armónico, paso bajo 4,2 kHz, grano 12 bits, sala corta");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "snare", 37, 0.8f, kL3 };
        auto proc = [] (Buf b) { b = repitch (b, 0.86); b = highpass (b, 95); b = peakEq (b, 210, 1.0, 4); b = peakEq (b, 2400, 1.2, 3); b = warm (b, 2.4, 0.15); b = lowpass (b, 7800); b = grit (b, 12); addRoom (b, 0.3, 0.09, 17); return trim (b, 0.5, 0.16); };
        const char* txt = "afinada −2,6 st, +4 dB a 210 Hz, +3 dB a 2,4 kHz, saturación cálida, paso bajo 7,8 kHz, grano 12 bits, sala";
        addVcslLayer (p, kSnare1, kSnareV, proc, txt); addVcslLayer (p, kSnare1, kSnareM, proc, txt); addVcslLayer (p, kSnare1, kSnareF, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "clap", 38, 0.7f, kL1 };
        std::vector<Buf> takes; std::vector<std::string> o;
        for (int t = 0; t < 3; ++t)
        {
            Buf a = repitch (loadMono (std::string (kClaps) + kClapAll[(size_t) t]), 0.9), c = repitch (loadMono (std::string (kClaps) + kClapAll[(size_t) t + 3]), 0.94);
            Buf m = mixed (a, c, 0.8f, 0.009);
            m = highpass (m, 260); m = warm (m, 2.0, 0.1); m = lowpass (m, 8200); m = grit (m, 12); addRoom (m, 0.25, 0.08, 23 + (uint32_t) t);
            takes.push_back (trim (m, 0.4, 0.12));
            o.push_back (vcsl (std::string (kClaps) + kClapAll[(size_t) t] + " + " + kClapAll[(size_t) t + 3], "dos palmas apiladas con 9 ms, afinadas abajo, saturación, grano 12 bits, sala"));
        }
        p.takes.push_back (takes); p.origins.push_back (o); p.ampVeltrack = 70;
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_closed", 39, 0.4f, kL3 }; p.group = 1;
        auto proc = [] (Buf b) { b = repitch (b, 0.93); b = highpass (b, 500); b = lowpass (b, 8600); b = warm (b, 1.6, 0.0); return trim (b, 0.18, 0.07); };
        const char* txt = "afinado −1,3 st, paso alto 500 Hz, paso bajo 8,6 kHz (oscuro), saturación";
        addVcslLayer (p, kHat, kHatSoft, proc, txt); addVcslLayer (p, kHat, kHatMid, proc, txt); addVcslLayer (p, kHat, kHatHard, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_open", 40, 0.36f, kL1 }; p.offBy = 1; p.ampVeltrack = 60;
        addVcslLayer (p, kHat, kHatOpen, [] (Buf b) { b = repitch (b, 0.93); b = highpass (b, 450); b = lowpass (b, 8200); return trim (b, 0.8, 0.3); }, "afinado −1,3 st, paso bajo 8,2 kHz, recorte 0,8 s");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "rim", 41, 0.55f, kL1 }; p.ampVeltrack = 70;
        addVcslLayer (p, kSnare3, kXstick, [] (Buf b) { b = repitch (b, 0.9); b = highpass (b, 220); b = warm (b, 1.9, 0.1); b = lowpass (b, 8000); addRoom (b, 0.2, 0.06, 5); return trim (b, 0.25, 0.08); }, "afinado −1,8 st, saturación, paso bajo 8 kHz, sala corta");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "shaker", 42, 0.42f, kL1 }; p.ampVeltrack = 70;
        addVcslLayer (p, kShaker, { "Mid_ShakerDouble_Down_rr1.wav", "Mid_ShakerDouble_Down_rr2.wav", "Mid_ShakerDouble_Up_rr1.wav" }, [] (Buf b) { b = highpass (b, 1800); b = lowpass (b, 9500); return trim (b, 0.2, 0.06); }, "paso alto 1,8 kHz, paso bajo 9,5 kHz");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "crash", 43, 0.55f, kL2 };
        auto proc = [] (Buf b) { b = repitch (b, 0.9); b = highpass (b, 350); b = lowpass (b, 7000); return trim (b, 1.8, 0.8); };
        addVcslLayer (p, kCymbal, { "susCymb1_hit_mp1.wav" }, proc, "afinado −1,8 st, paso bajo 7 kHz, recorte 1,8 s"); addVcslLayer (p, kCymbal, { "susCymb1_hit_f1.wav" }, proc, "ídem");
        sfz += writePiece (w, kit, p);
    }
    return sfz;
}

// ==================================================================================================================
// 2) Cauce Midnight Trap — bombo corto con mucha pegada (deja el grave al 808), caja con chasquido, charles finos
// ==================================================================================================================
std::string midnightTrap (PackWriter& w)
{
    const char* kit = "midnight";
    std::string sfz = "// Cauce Midnight Trap — CAUCESOUND. Síntesis propia + palmas VCSL (CC0). Ver PROVENANCE.md\n";
    {
        Piece p { "kick", 36, 0.95f, kL3 };
        kickLayers (p, { 230, 48, 0.007, 0.04, 0.25, 0.115, 0.012, 0.25, 3.4, 3200, 0.0018, 0.8, 0.9, 7000, 0.42, 260, 0.03, 0.85 }, 5200,
                    [] (Buf b, int) { return shapeAttack (b, 2.0, 3.0); }, "bombo corto 230→48 Hz, maza a 3,2 kHz, ataque realzado; caída 115 ms para no tapar el 808");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "snare", 37, 0.8f, kL3 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 3, [&] (int t)
            {
                Buf s = snare2 ({ 205, 338, 0.07, 0.34 - 0.05 * l, 1300, 11500, 0.045, 0.19, 0.3, 0.5 + 0.2 * l, 0.5 }, 5300 + 10 * (uint32_t) l + (uint32_t) t, rrPitch (t), rrDecay (t));
                Buf c = trim (highpass (loadMono (std::string (kClaps) + kClapAll[(size_t) (t + l) % 6]), 500), 0.25, 0.08);
                s = mixed (s, c, 0.35f, 0.002);
                return shapeAttack (warm (s, 1.8 + 0.4 * l, 0.05), 2.5, 2.0);
            }, "caja sintetizada 205/338 Hz con bordones brillantes + palma VCSL debajo, ataque realzado");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "clap", 38, 0.72f, kL2 };
        for (int l = 0; l < 2; ++l)
            addSynthLayer (p, 3, [&] (int t)
            {
                Buf s = clapSynth (1500 + 150 * t, 0.11 + 0.03 * l, 0.5, 5400 + 10 * (uint32_t) l + (uint32_t) t, 0.0085 + 0.001 * t);
                Buf c = trim (highpass (loadMono (std::string (kClaps) + kClapAll[(size_t) (t + 2 * l) % 6]), 400), 0.3, 0.1);
                return lowpass (mixed (s, c, 0.6f), 12000);
            }, "palma sintetizada con cola + palma VCSL");
        p.ampVeltrack = 60;
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_closed", 39, 0.4f, kL3 }; p.group = 1;
        const double tau[3] = { 0.012, 0.017, 0.024 }, hp[3] = { 7500, 6800, 6200 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 4, [&] (int t) { return hat (tau[l] * rrDecay (t), hp[l], 0.12, 5500 + 10 * (uint32_t) l + (uint32_t) t, 0.62, 1.78 * rrPitch (t)); }, "charles metálico fino, 4 tomas (para redobles sin efecto ametralladora)");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_open", 40, 0.36f, kL1 }; p.offBy = 1; p.ampVeltrack = 60;
        addSynthLayer (p, 2, [&] (int t) { return hat (0.16 * rrDecay (t), 6000, 0.6, 5600 + (uint32_t) t, 0.6, 1.78); }, "charles abierto metálico");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "rim", 41, 0.5f, kL1 }; p.ampVeltrack = 70;
        addSynthLayer (p, 3, [&] (int t) { return rimSynth (1750 * rrPitch (t), 0.012, 5700 + (uint32_t) t); }, "golpe de aro agudo y seco");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "perc", 42, 0.55f, kL2 };
        for (int l = 0; l < 2; ++l)
            addSynthLayer (p, 2, [&] (int t) { return lowpass (tonePerc (146.8 * rrPitch (t), 5 + 3 * l, 0.02, 0.16, 0.25 + 0.2 * l, 0.5, 5800 + 10 * (uint32_t) l + (uint32_t) t), 5000); }, "tom oscuro de 808 afinado en re");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "crash", 43, 0.5f, kL1 };
        addSynthLayer (p, 1, [&] (int) { return lowpass (crashSynth (0.55, 1.8, 5900), 9500); }, "platillo sintetizado oscuro");
        sfz += writePiece (w, kit, p);
    }
    return sfz;
}

// ==================================================================================================================
// 3) Cauce Calle Reggaetón — bombo seco de dembow, caja-timbal aguda, timbal y conga grave
// ==================================================================================================================
std::string calleReggaeton (PackWriter& w)
{
    const char* kit = "calle";
    std::string sfz = "// Cauce Calle Reggaetón — CAUCESOUND. Síntesis propia + percusión VCSL (CC0). Ver PROVENANCE.md\n";
    {
        Piece p { "kick", 36, 0.95f, kL3 };
        kickLayers (p, { 195, 53, 0.009, 0.05, 0.3, 0.14, 0.015, 0.35, 2.6, 2400, 0.0025, 0.6, 0.6, 5000, 0.5, 215, 0.04, 0.85 }, 6100,
                    [] (Buf b, int) { return shapeAttack (b, 1.5, 4.0); }, "bombo seco 195→53 Hz con 2.º armónico marcado (se oye en el móvil)");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "snare", 37, 0.8f, kL3 };
        auto proc = [] (Buf b)
        {
            b = repitch (b, 1.16); b = highpass (b, 180); b = peakEq (b, 950, 1.4, 4); b = peakEq (b, 5200, 1.0, 3); b = warm (b, 2.0, 0.08);
            return shapeAttack (trim (b, 0.22, 0.07), 2.0, 2.0);
        };
        const char* txt = "afinada +2,6 st (timbre de timbal), +4 dB a 950 Hz, +3 dB a 5,2 kHz, corta (0,22 s), ataque realzado";
        addVcslLayer (p, kSnare1, kSnareV, proc, txt); addVcslLayer (p, kSnare1, kSnareM, proc, txt); addVcslLayer (p, kSnare1, kSnareF, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "clap", 38, 0.7f, kL1 }; p.ampVeltrack = 70;
        addVcslLayer (p, kClaps, { "Clap_rr2.wav", "Clap_rr4.wav", "Clap_rr5.wav", "Clap_rr6.wav" }, [] (Buf b) { b = highpass (b, 350); b = peakEq (b, 1500, 1.0, 3); b = warm (b, 1.6, 0.0); return trim (b, 0.3, 0.1); }, "paso alto 350 Hz, +3 dB a 1,5 kHz, saturación suave");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_closed", 39, 0.4f, kL3 }; p.group = 1;
        auto proc = [] (Buf b) { b = repitch (b, 1.08); b = highpass (b, 900); b = shelf (b, true, 7000, 3); return trim (b, 0.12, 0.05); };
        const char* txt = "afinado +1,3 st, paso alto 900 Hz, +3 dB de brillo, muy corto";
        addVcslLayer (p, kHat, kHatSoft, proc, txt); addVcslLayer (p, kHat, kHatMid, proc, txt); addVcslLayer (p, kHat, kHatHard, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_open", 40, 0.36f, kL1 }; p.offBy = 1; p.ampVeltrack = 60;
        addVcslLayer (p, kHat, kHatOpen, [] (Buf b) { b = repitch (b, 1.08); b = highpass (b, 800); return trim (b, 0.45, 0.2); }, "afinado +1,3 st, recorte 0,45 s");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "timbal", 41, 0.62f, kL2 };
        for (int l = 0; l < 2; ++l)
        {
            std::vector<Buf> takes; std::vector<std::string> o;
            for (int t = 0; t < 2; ++t)
            {
                Buf x = trim (highpass (repitch (loadMono (std::string (kSnare3) + kXstick[(size_t) t]), 1.12), 300), 0.15, 0.05);
                Buf tone = tonePerc (392 * rrPitch (t), 2.0 + l, 0.015, 0.11 + 0.03 * l, 0.3, 0.4, 6200 + 10 * (uint32_t) l + (uint32_t) t);
                takes.push_back (mixed (tone, x, 0.7f));
                o.push_back (vcsl (std::string (kSnare3) + kXstick[(size_t) t], "golpe de aro afinado +2 st + cuerpo tonal sintetizado en sol (timbal)"));
            }
            p.takes.push_back (takes); p.origins.push_back (o);
        }
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "conga_low", 42, 0.6f, kL3 };
        auto proc = [] (Buf b) { b = repitch (b, 0.8); b = highpass (b, 70); b = peakEq (b, 180, 1.0, 3); return trim (b, 0.5, 0.15); };
        const char* txt = "afinada −3,9 st (tumbadora grave), +3 dB a 180 Hz";
        for (int v = 1; v <= 3; ++v) addVcslLayer (p, kConga, { "Conga_HitN_v" + std::to_string (v) + "_rr1_Sum.wav", "Conga_HitN_v" + std::to_string (v) + "_rr2_Sum.wav" }, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "shaker", 43, 0.42f, kL1 }; p.ampVeltrack = 70;
        addVcslLayer (p, kShaker, { "Mid_ShakerDouble_Down_rr1.wav", "Mid_ShakerDouble_Down_rr2.wav", "Mid_ShakerDouble_Up_rr1.wav" }, [] (Buf b) { b = highpass (b, 2200); return trim (b, 0.18, 0.06); }, "paso alto 2,2 kHz, corto");
        sfz += writePiece (w, kit, p);
    }
    return sfz;
}

// ==================================================================================================================
// 4) Cauce Organic Hip-Hop — batería acústica casi sin procesar: bombo de fieltro, caja, charles y aro reales, sala
// ==================================================================================================================
std::string organicHipHop (PackWriter& w)
{
    const char* kit = "organic";
    std::string sfz = "// Cauce Organic Hip-Hop — CAUCESOUND. Síntesis propia (bombo) + VCSL (CC0) casi natural. Ver PROVENANCE.md\n";
    {
        Piece p { "kick", 36, 0.92f, kL3 };
        kickLayers (p, { 118, 54, 0.016, 0.1, 0.45, 0.27, 0.025, 0.3, 1.7, 650, 0.007, 0.55, 0.35, 2400, 0.8, 165, 0.06, 0.5 }, 7100,
                    [] (Buf b, int) { b = lowpass (b, 5200); addRoom (b, 0.22, 0.07, 71); return b; }, "bombo de maza de fieltro 118→54 Hz, poca saturación, sala natural");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "snare", 37, 0.8f, kL3 };
        auto proc = [] (Buf b) { b = highpass (b, 90); b = warm (b, 1.25, 0.05); addRoom (b, 0.2, 0.08, 31); return trim (b, 0.6, 0.2); };
        const char* txt = "casi natural: paso alto 90 Hz, saturación muy suave, sala corta";
        addVcslLayer (p, kSnare1, kSnareV, proc, txt); addVcslLayer (p, kSnare1, kSnareM, proc, txt); addVcslLayer (p, kSnare1, kSnareF, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "clap", 38, 0.68f, kL1 }; p.ampVeltrack = 75;
        addVcslLayer (p, kClaps, kClapAll, [] (Buf b) { b = highpass (b, 200); addRoom (b, 0.15, 0.06, 37); return trim (b, 0.4, 0.12); }, "natural con sala corta, 6 tomas");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_closed", 39, 0.42f, kL3 }; p.group = 1;
        auto proc = [] (Buf b) { b = highpass (b, 300); return trim (b, 0.25, 0.09); };
        addVcslLayer (p, kHat, kHatSoft, proc, "natural, paso alto 300 Hz"); addVcslLayer (p, kHat, kHatMid, proc, "ídem"); addVcslLayer (p, kHat, kHatHard, proc, "ídem");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_open", 40, 0.38f, kL1 }; p.offBy = 1; p.ampVeltrack = 65;
        addVcslLayer (p, kHat, kHatOpen, [] (Buf b) { b = highpass (b, 300); return trim (b, 1.0, 0.4); }, "natural, recorte 1 s");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "rim", 41, 0.55f, kL1 }; p.ampVeltrack = 75;
        addVcslLayer (p, kSnare3, kXstick, [] (Buf b) { b = highpass (b, 180); addRoom (b, 0.15, 0.06, 41); return trim (b, 0.3, 0.1); }, "golpe de aro natural con sala");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "shaker", 42, 0.45f, kL1 }; p.ampVeltrack = 75;
        addVcslLayer (p, kShaker, { "Mid_ShakerDouble_Down_rr1.wav", "Mid_ShakerDouble_Down_rr2.wav", "Mid_ShakerDouble_Up_rr1.wav" }, [] (Buf b) { b = highpass (b, 1200); return trim (b, 0.22, 0.07); }, "natural");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "crash", 43, 0.6f, kL2 };
        auto proc = [] (Buf b) { b = highpass (b, 250); return trim (b, 2.4, 1.0); };
        addVcslLayer (p, kCymbal, { "susCymb1_hit_mp1.wav" }, proc, "natural, recorte 2,4 s"); addVcslLayer (p, kCymbal, { "susCymb1_hit_f1.wav" }, proc, "ídem");
        sfz += writePiece (w, kit, p);
    }
    return sfz;
}

// ==================================================================================================================
// 5) Cauce Soul Pocket — R&B y soul: bombo profundo y redondo, caja suave con sala, chasquido de dedos, aro cálido
// ==================================================================================================================
std::string soulPocket (PackWriter& w)
{
    const char* kit = "soul";
    std::string sfz = "// Cauce Soul Pocket — CAUCESOUND. Síntesis propia + VCSL (CC0) procesado. Ver PROVENANCE.md\n";
    {
        Piece p { "kick", 36, 0.92f, kL3 };
        kickLayers (p, { 105, 46, 0.02, 0.12, 0.5, 0.33, 0.03, 0.4, 1.5, 420, 0.009, 0.4, 0.2, 1800, 0.9, 150, 0.07, 0.45 }, 8100,
                    [] (Buf b, int) { return lowpass (b, 3200); }, "bombo profundo 105→46 Hz, redondo, 2.º armónico al 40 % para altavoces pequeños");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "snare", 37, 0.75f, kL3 };
        auto proc = [] (Buf b) { b = repitch (b, 0.94); b = highpass (b, 120); b = peakEq (b, 250, 1.0, 2.5); b = lowpass (b, 7200); b = warm (b, 1.4, 0.1); addRoom (b, 0.45, 0.16, 53); return trim (b, 0.7, 0.3); };
        const char* txt = "afinada −1 st, paso bajo 7,2 kHz (suave), +2,5 dB a 250 Hz, sala larga tipo placa";
        addVcslLayer (p, kSnare1, kSnareV, proc, txt); addVcslLayer (p, kSnare1, kSnareM, proc, txt); addVcslLayer (p, kSnare1, kSnareF, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "snap", 38, 0.62f, kL2 };
        for (int l = 0; l < 2; ++l)
            addSynthLayer (p, 3, [&] (int t) { Buf b = fingerSnap (8200 + 10 * (uint32_t) l + (uint32_t) t, 2150 * rrPitch (t) * (1.0 + 0.08 * l)); addRoom (b, 0.3, 0.1, 59 + (uint32_t) t); return trim (b, 0.35, 0.12); }, "chasquido de dedos sintetizado con sala");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_closed", 39, 0.36f, kL3 }; p.group = 1;
        auto proc = [] (Buf b) { b = highpass (b, 600); b = lowpass (b, 9500); return trim (b, 0.16, 0.07); };
        const char* txt = "suave: paso bajo 9,5 kHz, corto";
        addVcslLayer (p, kHat, kHatSoft, proc, txt); addVcslLayer (p, kHat, { "HiHat_HitC_v2_rr1_Mid.wav", "HiHat_HitC_v2_rr2_Mid.wav" }, proc, txt); addVcslLayer (p, kHat, { "HiHat_HitC_v3_rr1_Mid.wav", "HiHat_HitC_v3_rr2_Mid.wav" }, proc, txt);
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "hat_open", 40, 0.32f, kL1 }; p.offBy = 1; p.ampVeltrack = 60;
        addVcslLayer (p, kHat, kHatOpen, [] (Buf b) { b = highpass (b, 600); b = lowpass (b, 9000); return trim (b, 0.7, 0.3); }, "suave, paso bajo 9 kHz");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "rim", 41, 0.6f, kL1 }; p.ampVeltrack = 70;
        addVcslLayer (p, kSnare3, kXstick, [] (Buf b) { b = highpass (b, 200); b = peakEq (b, 800, 1.2, 3); b = warm (b, 1.5, 0.1); addRoom (b, 0.4, 0.14, 61); return trim (b, 0.5, 0.2); }, "aro cálido con sala larga (el clásico de R&B)");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "shaker", 42, 0.4f, kL1 }; p.ampVeltrack = 75;
        addVcslLayer (p, kShaker, { "Mid_ShakerDouble_Down_rr1.wav", "Mid_ShakerDouble_Down_rr2.wav", "Mid_ShakerDouble_Up_rr1.wav" }, [] (Buf b) { b = highpass (b, 1500); b = lowpass (b, 10000); return trim (b, 0.2, 0.07); }, "suave");
        sfz += writePiece (w, kit, p);
    }
    {
        Piece p { "crash", 43, 0.5f, kL1 };
        addVcslLayer (p, kCymbal, { "susCymb1_hit_mp1.wav" }, [] (Buf b) { b = highpass (b, 400); b = lowpass (b, 8500); return trim (b, 2.0, 1.0); }, "platillo suave, paso bajo 8,5 kHz");
        sfz += writePiece (w, kit, p);
    }
    return sfz;
}

void buildDrums2 (const std::string& out)
{
    gTwo = true;
    PackWriter w (out + "/cauce.drums.two");
    w.text ("instruments/crate-boombap.sfz", crateBoomBap (w));
    w.text ("instruments/midnight-trap.sfz", midnightTrap (w));
    w.text ("instruments/calle-reggaeton.sfz", calleReggaeton (w));
    w.text ("instruments/organic-hiphop.sfz", organicHipHop (w));
    w.text ("instruments/soul-pocket.sfz", soulPocket (w));
    PackManifest m;
    m.id = "cauce.drums.two";
    m.version = "1.0.0";
    m.name = "Cauce Signature Drums 2";
    m.author = "CAUCESOUND";
    m.description = "Cinco kits con carácter propio: Crate Boom Bap, Midnight Trap, Calle Reggaetón, Organic Hip-Hop y Soul Pocket.";
    m.license = "LicenseRef-CauceSound-Content AND CC0-1.0";
    m.licenseFile = "LICENSE.txt";
    m.minEngine = "0.9.0";
    m.sources.push_back ({ "Versilian Community Sample Library (VCSL)", "Versilian Studios LLC", "https://github.com/sgossner/VCSL", kVcslCommit, "CC0-1.0", kRetrieved,
                           "Licencia CC0 1.0 (archivo LICENSE del repositorio). Uso comercial y redistribución permitidos sin atribución. Muestras procesadas aquí (ver PROVENANCE.md)." });
    m.sources.push_back ({ "Síntesis CAUCESOUND", "CAUCESOUND", "desktop/tools/packbuilder/Signature2.inc", "0.10", "LicenseRef-CauceSound-Content", kRetrieved,
                           "Sonidos originales generados por síntesis con parámetros y semillas fijas." });
    m.instruments = {
        { "crate-boombap", "Cauce Crate Boom Bap", "drums", "drumkit", "instruments/crate-boombap.sfz", "Bombo gordo y polvoriento, caja real afinada abajo con sala, palmas apiladas, charles oscuros.", "original + cc0",
          { "Boom Bap", "Dusty", "Fat", "Vintage" }, { "boombap", "jazzrap", "hiphop" }, { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "Shaker", "Crash" }, 24 },
        { "midnight-trap", "Cauce Midnight Trap", "drums", "drumkit", "instruments/midnight-trap.sfz", "Bombo corto que deja el grave al 808, caja con chasquido, charles finos de 4 tomas, tom oscuro.", "original + cc0",
          { "Trap", "Dark", "Tight", "Modern" }, { "trap", "darkhiphop", "drill" }, { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "808 Tom", "Crash" }, 24 },
        { "calle-reggaeton", "Cauce Calle Reggaetón", "drums", "drumkit", "instruments/calle-reggaeton.sfz", "Bombo seco de dembow, caja aguda tipo timbal, timbal afinado, tumbadora grave y shaker.", "original + cc0",
          { "Reggaetón", "Dembow", "Latin", "Punchy" }, { "reggaeton", "latin", "dembow" }, { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Timbal", "Conga", "Shaker" }, 24 },
        { "organic-hiphop", "Cauce Organic Hip-Hop", "drums", "drumkit", "instruments/organic-hiphop.sfz", "Batería acústica casi sin procesar: bombo de fieltro, caja, charles y aro reales con sala natural.", "original + cc0",
          { "Hip-Hop", "Organic", "Live", "Natural" }, { "hiphop", "boombap", "jazzrap", "pop" }, { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "Shaker", "Crash" }, 24 },
        { "soul-pocket", "Cauce Soul Pocket", "drums", "drumkit", "instruments/soul-pocket.sfz", "Bombo profundo y redondo, caja suave con sala, chasquido de dedos y aro cálido para R&B y soul.", "original + cc0",
          { "R&B", "Soul", "Smooth", "Warm" }, { "rnb", "soul", "pop", "jazzrap" }, { "Kick", "Snare", "Snap", "Closed Hat", "Open Hat", "Rim", "Shaker", "Crash" }, 24 } };
    w.finish (m, std::string ("Cauce Signature Drums 2 1.0.0\n\n") + kLicense2,
              "# Procedencia — Cauce Signature Drums 2 1.0.0\n\nGenerado por `cauce_packs` (desktop/tools/packbuilder). VCSL en el commit " + std::string (kVcslCommit) + " (consultado " + kRetrieved + ").\n"
              "En las tomas reales el inicio se recorta al ataque y las capas de fuerza quedan escalonadas 5 dB.\n");
    gTwo = false;
}

// ==================================================================================================================
// Cauce Bass Lab II: 808 Punch, 808 Short y Warm Sub
// ==================================================================================================================
// 808 con ataque definido y armónicos pensados para altavoces pequeños: la saturación asimétrica crea 2.º y 3.er
// armónico (60-250 Hz → 120-750 Hz), que es lo que un móvil sí reproduce. 'tail' controla la duración.
Buf render808v2 (double hz, double drive, double bias, double click, double tail, double length, double harm, uint32_t seed)
{
    Buf b = len (length), h = len (length);
    Rng rng (seed);
    Biquad hp; hp.set (Biquad::HighPass, sr, 2500, 0.7);
    Biquad lp; lp.set (Biquad::LowPass, sr, 9000, 0.7);
    double ph = 0;
    const double g = 1.0 + drive * 4.0;
    const double off = std::tanh (bias * g), n = std::tanh (g * (1.0 + bias));
    std::vector<double> ampv (b.size());
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        // Golpe de tono en dos tiempos: +12 st que caen en 12 ms (pegada) y +3 st que caen en 60 ms (cuerpo).
        const double st = 12.0 * std::exp (-t / 0.012) + 3.0 * std::exp (-t / 0.06);
        ph += hz * std::pow (2.0, st / 12.0) / sr;
        const double amp = (0.25 * std::exp (-t / 0.09) + 0.75 * std::exp (-t / tail)) * std::min (1.0, t / 0.0008);
        ampv[i] = amp;
        const double sn = std::sin (2 * kPi * ph);
        double x = sn * amp;
        x = (std::tanh ((x + bias * amp) * g) - off * amp) / n;
        const double c = t < 0.005 ? lp.process (hp.process (rng.next())) * click * (1.0 - t / 0.005) : 0.0;
        b[i] = (float) (x * 0.9 + c);
        // Capa de armónicos aditiva: parciales enteros entre 150 y 900 Hz, enganchados a la fase (siguen el golpe de tono
        // y el glide). Una saturación suave no sirve para esto: medido, deja el 5.º armónico a −39 dB y se apaga al caer
        // la nota. Aquí el nivel se decide (harm) y dura lo que dura la nota.
        double hs = 0, wsum = 0;
        for (int k = 2; k <= 20; ++k)
        {
            const double fk = hz * k;
            if (fk < 150 || fk > 900) continue;
            const double wk = 1.0 / std::pow (fk / 150.0, 0.9);
            hs += wk * std::sin (2 * kPi * ph * k + 1.7 * k); // fases repartidas: menos pico para la misma energía
            wsum += wk * wk;
        }
        h[i] = wsum > 0 ? (float) (hs / std::sqrt (wsum)) : 0.0f;
    }
    for (size_t i = 0; i < b.size(); ++i) b[i] += (float) (h[i] * ampv[i] * harm * 0.6);
    b = highpass (b, 20);
    fadeOut (b, std::min (0.4, length * 0.3));
    return b;
}
Buf renderSubV2 (double hz, int cycles)
{
    const size_t n = (size_t) std::lround (cycles * sr / hz);
    const double f = cycles * sr / (double) n;
    Buf b (n);
    // Seno + armónicos 2 a 5 en proporción decreciente y una saturación suave: sigue siendo un sub, pero deja rastro
    // entre 100 y 400 Hz. Las fases fijas mantienen el bucle exacto.
    static const double h[5] = { 1.0, 0.34, 0.2, 0.08, 0.04 };
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = f * (double) i / sr;
        double x = 0;
        for (int k = 0; k < 5; ++k) x += h[k] * std::sin (2 * kPi * ph * (k + 1) + 0.3 * k);
        b[i] = (float) (0.78 * std::tanh (x * 0.9) / std::tanh (1.5));
    }
    double mean = 0;
    for (float v : b) mean += v;
    mean /= (double) n;
    for (float& v : b) v -= (float) mean; // sin continua (un bucle con continua da un chasquido al soltar la nota)
    return b;
}

void buildBass2 (const std::string& out)
{
    PackWriter w (out + "/cauce.bass.two");
    // Muestreado cada tritono (6 st) en lugar de cada quinta (7 st), con tres capas de fuerza en lugar de dos.
    const int keys[4] = { 24, 30, 36, 42 };
    auto zones = [&] (std::ostringstream& s, const char* folder, const char* name, double tail, double length, double clickMul)
    {
        for (int k = 0; k < 4; ++k)
        {
            const double hz = 440.0 * std::pow (2.0, (keys[k] - 69) / 12.0);
            const int lo = k == 0 ? 0 : keys[k] - 2, hi = k == 3 ? 127 : keys[k] + 3;
            const double drive[3] = { 0.25, 0.5, 0.85 }, bias[3] = { 0.12, 0.2, 0.3 }, click[3] = { 0.05, 0.1, 0.18 }, harm[3] = { 0.45, 0.7, 1.0 };
            const int vlo[3] = { 1, 71, 106 }, vhi[3] = { 70, 105, 127 };
            for (int v = 0; v < 3; ++v)
            {
                Buf b = render808v2 (hz, drive[v], bias[v], click[v] * clickMul, tail, length, harm[v], 9000 + 7 * (uint32_t) k + (uint32_t) v);
                gain (b, 0.92f / peakOf (b));
                const std::string rel = std::string ("samples/") + folder + "/" + name + "_k" + std::to_string (keys[k]) + "_v" + std::to_string (v + 1) + ".wav";
                w.writeBuf (rel, b, orig (v == 0 ? "808: golpe de tono en dos tiempos, saturación asimétrica suave" : v == 1 ? "808: saturación asimétrica media" : "808: saturación asimétrica fuerte y más clic"));
                s << "<region> lokey=" << lo << " hikey=" << hi << " pitch_keycenter=" << keys[k] << " lovel=" << vlo[v] << " hivel=" << vhi[v] << " sample=../" << rel << "\n";
            }
        }
    };
    std::ostringstream punch, shrt, sub;
    punch << "// Cauce 808 Punch — síntesis CAUCESOUND, cada tritono, tres capas de saturación. Mono con glide (manifiesto).\n<group> ampeg_release=0.14 amp_veltrack=45\n";
    zones (punch, "punch", "808p", 1.25, 2.0, 1.0);
    w.text ("instruments/808-punch.sfz", punch.str());
    shrt << "// Cauce 808 Short — 808 corto para patrones rápidos y para dejar sitio al bombo.\n<group> ampeg_release=0.08 amp_veltrack=45\n";
    zones (shrt, "short", "808s", 0.3, 0.8, 1.3);
    w.text ("instruments/808-short.sfz", shrt.str());
    sub << "// Cauce Warm Sub — sub sostenido con armónicos 2-5 y saturación suave; bucle de ciclos enteros.\n"
           "<group> loop_mode=loop_continuous ampeg_attack=0.006 ampeg_release=0.18 amp_veltrack=50\n";
    const int skeys[4] = { 24, 31, 38, 45 };
    for (int k = 0; k < 4; ++k)
    {
        const double hz = 440.0 * std::pow (2.0, (skeys[k] - 69) / 12.0);
        Buf b = renderSubV2 (hz, (int) std::ceil (hz * 0.5));
        const std::string rel = "samples/sub/wsub_k" + std::to_string (skeys[k]) + ".wav";
        w.writeBuf (rel, b, orig ("sub: seno + armónicos 2-5 (34/20/8/4 %), saturación suave, bucle exacto"));
        sub << "<region> lokey=" << (k == 0 ? 0 : skeys[k] - 3) << " hikey=" << (k == 3 ? 127 : skeys[k] + 3) << " pitch_keycenter=" << skeys[k] << " loop_start=0 loop_end=" << (b.size() - 1) << " sample=../" << rel << "\n";
    }
    w.text ("instruments/warm-sub.sfz", sub.str());
    PackManifest m;
    m.id = "cauce.bass.two";
    m.version = "1.0.0";
    m.name = "Cauce Bass Lab II";
    m.author = "CAUCESOUND";
    m.description = "808 Punch (ataque definido, tres capas de saturación), 808 Short (corto, para patrones rápidos) y Warm Sub (sub con armónicos audibles en el móvil).";
    m.license = "LicenseRef-CauceSound-Content";
    m.licenseFile = "LICENSE.txt";
    m.minEngine = "0.9.0";
    m.sources.push_back ({ "Síntesis CAUCESOUND", "CAUCESOUND", "desktop/tools/packbuilder/Signature2.inc", "0.10", "LicenseRef-CauceSound-Content", kRetrieved, "Sonidos originales." });
    m.instruments = {
        { "808-punch", "Cauce 808 Punch", "bass", "melodic", "instruments/808-punch.sfz", "808 con ataque definido y glide. Tocar fuerte añade armónicos: se oye también en altavoces pequeños.", "original",
          { "808", "Trap", "Punchy", "Glide" }, { "trap", "darkhiphop", "reggaeton", "drill" }, {}, 4, true, 0.09f, -3.0f, 36, 12, 60 },
        { "808-short", "Cauce 808 Short", "bass", "melodic", "instruments/808-short.sfz", "808 corto y seco para líneas rápidas; no se monta sobre el siguiente bombo.", "original",
          { "808", "Short", "Tight" }, { "trap", "reggaeton", "boombap", "hiphop" }, {}, 4, true, 0.05f, -3.0f, 36, 12, 60 },
        { "warm-sub", "Cauce Warm Sub", "bass", "melodic", "instruments/warm-sub.sfz", "Subgrave sostenido, cálido, con armónicos que el móvil sí reproduce.", "original",
          { "Sub", "Warm", "Smooth" }, { "rnb", "soul", "trap", "pop", "electronic" }, {}, 4, true, 0.04f, -4.0f, 36, 12, 60 } };
    w.finish (m, "Cauce Bass Lab II 1.0.0\n\nSonidos sintetizados por CAUCESOUND (ver PROVENANCE.md). Contenido propio; texto de licencia provisional,\n"
                 "revisión jurídica pendiente antes de publicar. Las canciones creadas con ellos son del usuario.\n",
              "# Procedencia — Cauce Bass Lab II 1.0.0\n\nGenerado por `cauce_packs`. Todo es síntesis original.\n");
}

// ==================================================================================================================
// Cauce Keys II: Warm Electric Piano, Night Pad y Glass Pluck (síntesis original muestreada)
// ==================================================================================================================
// Piano eléctrico por FM (lámina + barra): la portadora lleva el tono; un modulador a la misma frecuencia da el
// «ladrido» que crece con la fuerza y se apaga pronto; un parcial inarmónico muy corto da el ataque de campana. Una
// pastilla asimétrica añade calidez. No imita un instrumento concreto ni usa grabaciones de ninguno.
Buf renderEp (int key, int layer, double length)
{
    const double hz = 440.0 * std::pow (2.0, (key - 69) / 12.0);
    Buf b = len (length);
    const double track = std::clamp ((84.0 - key) / 48.0, 0.0, 1.0);            // 1 en graves, 0 en agudos
    const double index = (layer ? 2.3 : 0.9) * (0.55 + 0.6 * track);            // más ladrido fuerte y en graves
    const double decay = 0.9 + 2.2 * track;                                     // los graves duran más
    const double bell = layer ? 0.3 : 0.14;
    double pc = 0, pm = 0, pb = 0, pb2 = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        pm += hz / sr; pb += hz * 14.0 / sr; pb2 += hz * 14.0 * 1.003 / sr;
        const double mod = std::sin (2 * kPi * pm) * index * (0.25 + 0.75 * std::exp (-t / (0.18 + 0.25 * track)));
        pc += hz / sr;
        const double body = std::sin (2 * kPi * pc + mod) * (0.3 * std::exp (-t / (decay * 0.25)) + 0.7 * std::exp (-t / decay));
        const double tine = (std::sin (2 * kPi * pb) + std::sin (2 * kPi * pb2)) * 0.5 * bell * std::exp (-t / (0.05 + 0.05 * track)) * (hz * 14.0 < 16000 ? 1.0 : 0.0);
        const double second = std::sin (2 * kPi * (2.0 * pc) + 0.5 * mod) * 0.12 * std::exp (-t / (decay * 0.6));
        b[i] = (float) ((body + tine + second) * std::min (1.0, t / 0.0012));
    }
    b = warm (b, layer ? 1.5 : 1.1, 0.18);
    b = lowpass (b, layer ? 7500 : 5200);
    b = shelf (b, false, 220, -1.5);
    fadeOut (b, std::min (0.25, length * 0.2));
    return b;
}
// Pad: armónicos con tres voces desafinadas a ±1 ciclo por bucle (batido lento de 0,5 Hz) y fases aleatorias fijas.
// Todas las componentes tienen ciclos enteros en el bucle, así que el empalme es exacto sin fundido cruzado.
Buf renderPad (int key, double seconds, uint32_t seed)
{
    const double hz0 = 440.0 * std::pow (2.0, (key - 69) / 12.0);
    const size_t n = (size_t) std::lround (seconds * sr);
    const long base = std::lround (hz0 * (double) n / sr); // ciclos de la fundamental en el bucle
    Rng rng (seed);
    Buf b (n, 0.0f);
    for (int h = 1; h <= 14; ++h)
    {
        const double fh = (double) (base * h) * sr / (double) n;
        if (fh > 9000) break;
        const double amp = std::pow ((double) h, -1.25) * (h % 2 ? 1.0 : 0.6) / (1.0 + std::pow (fh / 3200.0, 4.0)); // paso bajo suave
        for (int d = -1; d <= 1; ++d)
        {
            const long cyc = base * h + d * ((h + 1) / 2);
            const double phase = (rng.next() * 0.5 + 0.5) * 2 * kPi, a = amp * (d == 0 ? 1.0 : 0.75);
            const double w0 = 2 * kPi * (double) cyc / (double) n;
            for (size_t i = 0; i < n; ++i) b[i] += (float) (a * std::sin (w0 * (double) i + phase));
        }
    }
    gain (b, 0.7f / peakOf (b));
    return b;
}
// Pluck «de cristal»: FM de índice alto que se apaga muy rápido + cuerda (seno con caída) — brillante y corto.
Buf renderPluck (int key, int layer, double length)
{
    const double hz = 440.0 * std::pow (2.0, (key - 69) / 12.0);
    Buf b = len (length);
    double pc = 0, pm = 0;
    const double index = layer ? 5.0 : 2.6;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        pm += hz * 3.0 / sr; pc += hz / sr;
        const double mod = std::sin (2 * kPi * pm) * index * std::exp (-t / 0.045);
        const double x = std::sin (2 * kPi * pc + mod) * (0.5 * std::exp (-t / 0.12) + 0.5 * std::exp (-t / (length * 0.33)));
        const double oct = std::sin (4 * kPi * pc) * 0.2 * std::exp (-t / 0.2);
        b[i] = (float) ((x + oct) * std::min (1.0, t / 0.0006));
    }
    b = lowpass (b, 9000);
    fadeOut (b, length * 0.25);
    return b;
}

void buildKeys2 (const std::string& out)
{
    PackWriter w (out + "/cauce.keys.two");
    std::ostringstream ep, pad, pluck;
    ep << "// Cauce Warm Electric Piano — FM original muestreada cada tercera mayor, dos capas de fuerza.\n<group> ampeg_release=0.22 amp_veltrack=70\n";
    const int e0 = 36, eStep = 4, eCount = 13; // 36..84
    for (int k = 0; k < eCount; ++k)
    {
        const int key = e0 + k * eStep;
        const double length = 2.4 - 1.1 * (double) k / (eCount - 1);
        for (int v = 0; v < 2; ++v)
        {
            Buf b = renderEp (key, v, length);
            gain (b, (v ? 0.9f : 0.62f) / peakOf (b));
            const std::string rel = "samples/ep/ep_k" + std::to_string (key) + "_v" + std::to_string (v + 1) + ".wav";
            w.writeBuf (rel, b, orig (v ? "piano eléctrico FM, capa fuerte (más ladrido y campana)" : "piano eléctrico FM, capa suave"));
            ep << "<region> lokey=" << (k == 0 ? 0 : key - 1) << " hikey=" << (k == eCount - 1 ? 127 : key + 2) << " pitch_keycenter=" << key << " lovel=" << (v ? 86 : 1) << " hivel=" << (v ? 127 : 85) << " sample=../" << rel << "\n";
        }
    }
    w.text ("instruments/warm-ep.sfz", ep.str());
    pad << "// Cauce Night Pad — armónicos con batido lento, bucle exacto de 2 s, cada tritono.\n<group> loop_mode=loop_continuous ampeg_attack=0.35 ampeg_release=0.9 amp_veltrack=40\n";
    const int pCount = 8; // 36..78
    for (int k = 0; k < pCount; ++k)
    {
        const int key = 36 + 6 * k;
        Buf b = renderPad (key, 2.0, 9700 + (uint32_t) k);
        const std::string rel = "samples/pad/pad_k" + std::to_string (key) + ".wav";
        w.writeBuf (rel, b, orig ("pad: 14 armónicos × 3 voces desafinadas, bucle de ciclos enteros"));
        pad << "<region> lokey=" << (k == 0 ? 0 : key - 2) << " hikey=" << (k == pCount - 1 ? 127 : key + 3) << " pitch_keycenter=" << key << " loop_start=0 loop_end=" << (b.size() - 1) << " sample=../" << rel << "\n";
    }
    w.text ("instruments/night-pad.sfz", pad.str());
    pluck << "// Cauce Glass Pluck — FM corta y brillante, cada cuarta, dos capas.\n<group> ampeg_release=0.25 amp_veltrack=65 loop_mode=one_shot\n";
    const int qCount = 8; // 48..83
    for (int k = 0; k < qCount; ++k)
    {
        const int key = 48 + 5 * k;
        const double length = 1.3 - 0.5 * (double) k / (qCount - 1);
        for (int v = 0; v < 2; ++v)
        {
            Buf b = renderPluck (key, v, length);
            gain (b, (v ? 0.88f : 0.6f) / peakOf (b));
            const std::string rel = "samples/pluck/pluck_k" + std::to_string (key) + "_v" + std::to_string (v + 1) + ".wav";
            w.writeBuf (rel, b, orig (v ? "pluck FM, capa fuerte (más brillo)" : "pluck FM, capa suave"));
            pluck << "<region> lokey=" << (k == 0 ? 0 : key - 2) << " hikey=" << (k == qCount - 1 ? 127 : key + 2) << " pitch_keycenter=" << key << " lovel=" << (v ? 91 : 1) << " hivel=" << (v ? 127 : 90) << " sample=../" << rel << "\n";
        }
    }
    w.text ("instruments/glass-pluck.sfz", pluck.str());
    PackManifest m;
    m.id = "cauce.keys.two";
    m.version = "1.0.0";
    m.name = "Cauce Keys II";
    m.author = "CAUCESOUND";
    m.description = "Warm Electric Piano, Night Pad y Glass Pluck: síntesis original muestreada con capas de fuerza.";
    m.license = "LicenseRef-CauceSound-Content";
    m.licenseFile = "LICENSE.txt";
    m.minEngine = "0.9.0";
    m.sources.push_back ({ "Síntesis CAUCESOUND", "CAUCESOUND", "desktop/tools/packbuilder/Signature2.inc", "0.10", "LicenseRef-CauceSound-Content", kRetrieved, "Sonidos originales; no se usan grabaciones de ningún instrumento." });
    m.instruments = {
        { "warm-ep", "Cauce Warm Electric Piano", "keys", "melodic", "instruments/warm-ep.sfz", "Piano eléctrico cálido: tocar suave es redondo, tocar fuerte «ladra».", "original",
          { "Electric Piano", "Warm", "Soul", "Keys" }, { "rnb", "soul", "jazzrap", "boombap", "pop", "hiphop" }, {}, 24, false, 0.0f, -2.0f, 60, 36, 88 },
        { "night-pad", "Cauce Night Pad", "keys", "melodic", "instruments/night-pad.sfz", "Pad atmosférico suave con movimiento lento; entra y sale despacio.", "original",
          { "Pad", "Atmospheric", "Dark", "Soft" }, { "trap", "darkhiphop", "rnb", "electronic", "pop" }, {}, 16, false, 0.0f, -6.0f, 60, 36, 84 },
        { "glass-pluck", "Cauce Glass Pluck", "keys", "melodic", "instruments/glass-pluck.sfz", "Pluck brillante y corto para melodías y arpegios.", "original",
          { "Pluck", "Bright", "Melody" }, { "trap", "reggaeton", "pop", "electronic" }, {}, 16, false, 0.0f, -4.0f, 72, 48, 96 } };
    w.finish (m, "Cauce Keys II 1.0.0\n\nSonidos sintetizados por CAUCESOUND (ver PROVENANCE.md). Contenido propio; texto de licencia provisional,\n"
                 "revisión jurídica pendiente antes de publicar. Las canciones creadas con ellos son del usuario.\n",
              "# Procedencia — Cauce Keys II 1.0.0\n\nGenerado por `cauce_packs`. Todo es síntesis original.\n");
}
