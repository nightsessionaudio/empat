#include <juce_audio_utils/juce_audio_utils.h>
#include <climits>
#include <array>
#include "ChordGen.h"
#include "LogoData.h"

class NSAProcessor : public juce::AudioProcessor {
public:
  NSAProcessor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    loadTaste();
    generate();
  }
  const juce::String getName() const override { return "NSA Chords"; }
  void prepareToPlay(double sr, int) override { sampleRate = sr; active.reserve(64); pending.reserve(64); }
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}
  bool hasEditor() const override { return true; }
  juce::AudioProcessorEditor* createEditor() override;

  void getStateInformation(juce::MemoryBlock& d) override {
    juce::MemoryOutputStream o(d, true);
    o.writeInt(key); o.writeInt(open); o.writeFloat(dark); o.writeFloat(tense);
    o.writeInt(octaveShift); o.writeBool(noBass); o.writeBool(strum);
    o.writeFloat(nost.load()); o.writeFloat(air.load()); o.writeFloat(tear.load()); o.writeFloat(human.load());
    o.writeInt(res.load()); o.writeInt(scene.load());
    o.writeFloat(tasteAmt.load());
  }
  void setStateInformation(const void* data, int size) override {
    if (size < 20) return;
    juce::MemoryInputStream in(data, (size_t) size, false);
    key = juce::jlimit(0, 12, in.readInt()); open = juce::jlimit(0, 1, in.readInt());
    dark = juce::jlimit(0.0f, 1.0f, in.readFloat()); tense = juce::jlimit(0.0f, 1.0f, in.readFloat());
    octaveShift = juce::jlimit(-1, 1, in.readInt()); noBass = in.readBool(); strum = in.readBool();
    if (in.getNumBytesRemaining() >= 24) {
      nost = juce::jlimit(0.0f, 1.0f, in.readFloat()); air = juce::jlimit(0.0f, 1.0f, in.readFloat());
      tear = juce::jlimit(0.0f, 1.0f, in.readFloat()); human = juce::jlimit(0.0f, 1.0f, in.readFloat());
      res = juce::jlimit(0, 2, in.readInt()); scene = juce::jlimit(0, 8, in.readInt());
    }
    if (in.getNumBytesRemaining() >= 4) tasteAmt = juce::jlimit(0.0f, 1.0f, in.readFloat());
    lastKey = key;
    generate();
  }

  double getCurrentBpm() const {
    if (auto* ph = const_cast<NSAProcessor*>(this)->getPlayHead())
      if (auto pos = ph->getPosition())
        if (auto b = pos->getBpm())
          if (*b >= 20.0 && *b <= 300.0)
            return *b;
    return 120.0;
  }

  void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
    juce::ScopedNoDenormals nd;
    buffer.clear(); midi.clear();
    const int n = buffer.getNumSamples();
    bool hostPlaying = false, havePpq = false; double ppq = 0, bpm = 120.0;
    if (auto* ph = getPlayHead())
      if (auto pos = ph->getPosition()) {
        hostPlaying = pos->getIsPlaying();
        if (auto p = pos->getPpqPosition()) { ppq = *p; havePpq = true; }
        if (auto b = pos->getBpm()) bpm = *b;
      }
    if (bpm < 20.0) bpm = 120.0;
    const bool run = hostPlaying && havePpq;

    if (version.load() != snapVersion) {
      juce::SpinLock::ScopedTryLockType tl(lock);
      if (tl.isLocked()) { snap = state; snapVersion = version.load(); }
    }

    juce::MidiBuffer ev;
    if (run) {
      const double pps = bpm / 60.0 / sampleRate;
      const double p0 = ppq, p1 = p0 + (double) n * pps;
      auto off = [&](double t) { return juce::jlimit(0, n - 1, (int) std::floor((t - p0) / pps)); };
      auto release = [&](double upTo) {
        for (size_t i = 0; i < active.size();) {
          if (active[i].end <= upTo) {
            ev.addEvent(juce::MidiMessage::noteOff(1, active[i].note), off(active[i].end));
            active.erase(active.begin() + (long) i);
          } else ++i;
        }
      };
      auto cutAll = [&](int at) {
        for (auto& a : active) ev.addEvent(juce::MidiMessage::noteOff(1, a.note), at);
        active.clear(); pending.clear();
      };
      auto play = [&](const Hit& h, double t, int at) {
        if (h.chord < 0 || h.chord > 3) return;
        if (!h.topOnly) cutAll(at);
        const auto& c = snap.voiced[h.chord];
        const int sz = (int) c.size();
        if (sz < 2) return;
        const int octOffset = octaveShift.load() * 12;
        const bool skipB = noBass.load();
        const bool doStrum = strum.load();
        const float hm = human.load();
        const double msToPpq = bpm / 60000.0;

        for (int i = 0; i < sz; ++i) {
          const bool isTop = (i == sz - 1);
          if (skipB && i == 0) continue;
          if (h.topOnly && !isTop) continue;
          int note = c[(size_t) i];
          double endT = t + h.len;
          if (isTop && h.sus >= 0) { note = h.sus; endT = t + h.susLen; } // «слеза»: подвешенная нота
          else if (i > 0 && !isTop && !h.topOnly) endT -= (double) hm * 0.1 * audioRng.nextFloat(); // голоса отпускаются не синхронно
          note += octOffset;
          if (note < 0 || note > 127) continue;

          // ЖИВОСТЬ: ролл аккорда (Strum) + микро-тайминг в миллисекундах
          double ms = h.topOnly ? (double) hm * 4.0 * audioRng.nextFloat()
                                : (doStrum ? 22.0 * i : 0.0) + (double) hm * (6.0 * i + 8.0 * audioRng.nextFloat());
          const int vel = calcVel(i, isTop, note, h.chord, hm, audioRng.nextFloat(), h.topOnly);

          active.push_back(Act{note, endT});
          if (ms < 0.5) ev.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) vel), at);
          else pending.push_back(Pend{t + ms * msToPpq, note, (juce::uint8) vel});
        }
      };

      const bool jump = !wasRunning || std::abs(p0 - expectedPpq) > 0.25;
      const double base = std::floor(p0 / 16.0) * 16.0;
      if (jump) {
        cutAll(0);
        for (const auto& h : snap.hits) {
          const double t = base + h.start;
          if (t < p0 - 1e-9 && t + h.len > p0 + 1e-9) play(h, t, 0);
        }
      }
      const double w0 = jump ? p0 : expectedPpq;
      for (int cyc = 0; cyc < 2; ++cyc)
        for (const auto& h : snap.hits) {
          const double t = base + cyc * 16.0 + h.start;
          if (t >= w0 && t < p1) { release(t); play(h, t, off(t)); }
        }
      for (size_t i = 0; i < pending.size();) {
        if (pending[i].t < p1) {
          ev.addEvent(juce::MidiMessage::noteOn(1, pending[i].note, pending[i].vel), off(pending[i].t));
          pending.erase(pending.begin() + (long) i);
        } else ++i;
      }
      release(p1);
      expectedPpq = p1; wasRunning = true;
    } else if (wasRunning) {
      for (auto& a : active) ev.addEvent(juce::MidiMessage::noteOff(1, a.note), 0);
      active.clear(); pending.clear(); wasRunning = false;
    }
    midi.swapWith(ev);
  }

  void generate(bool newSeed = true) {
    if (newSeed) seed = uiRng.nextInt();
    juce::Random r(seed);
    juce::SpinLock::ScopedLockType l(lock);
    state.feel = currentFeel();
    state.generate(r, key, dark, tense, open != 0, getCurrentBpm(), newSeed, &taste, tasteAmt.load()); ++version;
    liked = false; // новая прогрессия — сердечко снова серое
  }
  void padChanged() { generate(false); }
  Feel currentFeel() const {
    Feel f; f.nost = nost.load(); f.res = res.load(); f.air = air.load(); f.tear = 0.0f; /* Tear отключён в интерфейсе */ f.human = human.load();
    return f;
  }
  // 0 = Free, 1..8 = сцена: выставляет точку на пэде и ВСЕ краски под эмоцию
  void setScene(int idx) {
    scene = idx;
    if (idx < 1 || idx > 8) return;
    const Scene& sc = SCENES[idx - 1];
    dark = sc.dk; tense = sc.tn; nost = sc.f.nost; res = sc.f.res;
    air = sc.f.air; tear = sc.f.tear; human = sc.f.human; strum = sc.strum;
    generate();
  }
  static int calcVel(int i, bool isTop, int note, int chord, float hm, float rnd, bool soft) {
    static const int arc[4] = {0, 3, 7, -3}; // динамическая дуга: к 3-му аккорду громче, к концу тише
    int v = (i == 0) ? 76 : ((isTop ? 52 : 60) + (note % 7));
    v += (int) std::lround(arc[chord] * hm) + (int) std::lround((rnd - 0.5f) * 10.0f * hm);
    if (soft) v -= 6;
    return juce::jlimit(36, 100, v);
  }
  void evolve() {
    juce::SpinLock::ScopedLockType l(lock);
    state.feel = currentFeel();
    state.evolve(uiRng, key, tense, open != 0, getCurrentBpm(), &taste, tasteAmt.load()); ++version;
    liked = false;
  }
  void setKey(int nk) {
    lastKey = key; key = nk;
    if (nk == 12) {
      generate(true);
    } else {
      juce::SpinLock::ScopedLockType l(lock);
      const int d = (((nk - state.keyUsed) % 12) + 12) % 12;
      state.transpose(d, open != 0, uiRng); ++version;
    }
  }
  void revoice() { juce::SpinLock::ScopedLockType l(lock); state.revoice(open != 0, uiRng); ++version; }
  juce::String getNames() { juce::SpinLock::ScopedLockType l(lock); return state.names; }

  // ---------- ВКУС: сердечко, хранение в общем файле ----------
  static juce::File tasteFile() {
    auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    d = d.getChildFile("Application Support");
#endif
    return d.getChildFile("NSA Chords").getChildFile("taste.json");
  }
  void loadTaste() {
    juce::var v;
    const auto f = tasteFile();
    if (f.existsAsFile()) v = juce::JSON::parse(f);
    juce::SpinLock::ScopedLockType l(lock);
    taste.entries.clear();
    const juce::var list = v["entries"];
    if (auto* arr = list.getArray())
      for (const auto& e : *arr) {
        const juce::var cv = e["c"];
        auto* cs = cv.getArray();
        if (cs == nullptr || cs->size() != 4) continue;
        Taste::Entry en; bool ok = true;
        for (int i = 0; i < 4; ++i) {
          const juce::var& c = (*cs)[i];
          en.c[i].r = (int) c[0]; en.c[i].t = (int) c[1]; en.c[i].b = (int) c[2];
          if (en.c[i].r < 0 || en.c[i].r > 11 || en.c[i].t < 0 || en.c[i].t > 13 || en.c[i].b < -1 || en.c[i].b > 11) ok = false;
          else en.c[i] = normCh(en.c[i]);
        }
        if (!ok) continue;
        en.dk = juce::jlimit(0.0f, 1.0f, (float) e["dk"]); en.tn = juce::jlimit(0.0f, 1.0f, (float) e["tn"]);
        en.w = juce::jlimit(0.1f, 5.0f, (float) e["w"]); en.tw = (bool) e["tw"]; en.src = (int) e["src"];
        en.f.nost = (float) e["nost"]; en.f.air = (float) e["air"]; en.f.tear = (float) e["tear"]; en.f.human = (float) e["human"];
        en.f.res = juce::jlimit(0, 2, (int) e["res"]);
        en.sig = sigOf(en.c);
        bool dup = false; for (const auto& o : taste.entries) if (o.sig == en.sig) dup = true;
        if (!dup) taste.entries.push_back(en);
      }
    taste.rebuild(); tasteCount = (int) taste.entries.size();
  }
  void saveTaste() {
    juce::String txt;
    {
      juce::SpinLock::ScopedLockType l(lock);
      auto* root = new juce::DynamicObject();
      juce::Array<juce::var> list;
      for (const auto& e : taste.entries) {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> cs;
        for (int i = 0; i < 4; ++i) {
          juce::Array<juce::var> c; c.add(e.c[i].r); c.add(e.c[i].t); c.add(e.c[i].b);
          cs.add(juce::var(c));
        }
        o->setProperty("c", juce::var(cs));
        o->setProperty("dk", (double) e.dk); o->setProperty("tn", (double) e.tn); o->setProperty("w", (double) e.w);
        o->setProperty("tw", e.tw); o->setProperty("src", e.src);
        o->setProperty("nost", (double) e.f.nost); o->setProperty("air", (double) e.f.air);
        o->setProperty("tear", (double) e.f.tear); o->setProperty("human", (double) e.f.human); o->setProperty("res", e.f.res);
        list.add(juce::var(o));
      }
      root->setProperty("version", 1);
      root->setProperty("entries", juce::var(list));
      txt = juce::JSON::toString(juce::var(root));
    }
    const auto f = tasteFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(txt);
  }
  // сердечко: запоминает ту прогрессию, что играет сейчас (повторное нажатие — убирает)
  void toggleLike() {
    {
      juce::SpinLock::ScopedLockType l(lock);
      if (!liked.load()) {
        Ch c[4]; for (int i = 0; i < 4; ++i) c[i] = state.relCh(i);
        taste.addEntry(c, state.padDk, state.padTn, state.feel, 1.0f, 0, state.feel.res == RES_TWIST);
        liked = true;
      } else {
        taste.removeBySig(state.signature(), 0);
        liked = false;
      }
      tasteCount = (int) taste.entries.size();
    }
    saveTaste();
  }
  void resetTaste() {
    {
      juce::SpinLock::ScopedLockType l(lock);
      taste.clear(); liked = false; tasteCount = 0;
    }
    saveTaste();
  }

  juce::File writeMidiFile() {
    ChordGen copy; { juce::SpinLock::ScopedLockType l(lock); copy = state; }
    juce::MidiMessageSequence seq;
    const int octOffset = octaveShift.load() * 12;
    const bool skipB = noBass.load();
    const bool doStrum = strum.load();
    const float hm = human.load();

    for (const auto& h : copy.hits) {
      if (h.chord < 0 || h.chord > 3) continue;
      const auto& c = copy.voiced[h.chord];
      const int sz = (int) c.size();
      for (int j = 0; j < sz; ++j) {
        const bool isTop = (j == sz - 1);
        if (skipB && j == 0) continue;
        if (h.topOnly && !isTop) continue;
        int note = c[(size_t) j];
        double endBeat = h.start + h.len;
        if (isTop && h.sus >= 0) { note = h.sus; endBeat = h.start + h.susLen; }
        note += octOffset;
        if (note < 0 || note > 127) continue;
        const int tickOffset = (!h.topOnly && doStrum) ? j * 15 : 0;
        const int vel = calcVel(j, isTop, note, h.chord, hm, 0.5f, h.topOnly);
        seq.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) vel), h.start * 480.0 + tickOffset);
        seq.addEvent(juce::MidiMessage::noteOff(1, note), endBeat * 480.0 - 10.0);
      }
    }
    seq.addEvent(juce::MidiMessage::endOfTrack(), 7680.0);
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote(480); mf.addTrack(seq);
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("NSA_Chords_" + juce::String(++fileCounter) + ".mid");
    f.deleteFile();
    juce::FileOutputStream out(f);
    if (out.openedOk()) mf.writeTo(out);
    return f;
  }

  std::atomic<int> key{0}, open{0}, octaveShift{0};
  std::atomic<float> dark{0.6f}, tense{0.4f};
  std::atomic<bool> noBass{false}, strum{true};
  std::atomic<float> nost{0.0f}, air{0.0f}, tear{0.0f}, human{0.5f};
  std::atomic<int> res{RES_HOME}, scene{0};
  std::atomic<bool> liked{false};
  std::atomic<float> tasteAmt{0.7f};
  std::atomic<int> tasteCount{0};

private:
  struct Act { int note; double end; };
  struct Pend { double t; int note; juce::uint8 vel; };
  std::vector<Pend> pending;
  juce::SpinLock lock;
  ChordGen state, snap;
  Taste taste; // под lock
  std::atomic<int> version{0};
  int snapVersion = -1, seed = 1, lastKey = 0;
  juce::Random uiRng, audioRng;
  std::vector<Act> active;
  double sampleRate = 44100.0, expectedPpq = 0.0;
  bool wasRunning = false;
  int fileCounter = 0;
};

namespace ui {
  const juce::Colour bg(0xff0d0c0b), fg(0xffe8e3d8), mu(0xff8d877a), tag(0xffe0d8c4),
                     accent(0xff8cd6e4), accentHi(0xffa8e3ee), g1(0xffb8eaf2), g2(0xff6fb8cc), onAcc(0xff0c232a);

  // «стекло»: полупрозрачная заливка, верхний блик, тонкая рамка
  inline void glass(juce::Graphics& g, juce::Rectangle<float> r, float rad, float a = 0.07f) {
    g.setColour(juce::Colours::white.withAlpha(a)); g.fillRoundedRectangle(r, rad);
    juce::ColourGradient sh(juce::Colours::white.withAlpha(0.10f), r.getX(), r.getY(),
                            juce::Colours::white.withAlpha(0.0f), r.getX(), r.getY() + r.getHeight() * 0.6f, false);
    g.setGradientFill(sh); g.fillRoundedRectangle(r, rad);
    g.setColour(juce::Colours::white.withAlpha(0.13f)); g.drawRoundedRectangle(r, rad, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.fillRoundedRectangle(r.getX() + rad * 0.6f, r.getY() + 1.0f, juce::jmax(0.0f, r.getWidth() - rad * 1.2f), 1.0f, 0.5f);
  }
  // акцентная «капля» (выбранный пункт / ручка ключа)
  inline void pill(juce::Graphics& g, juce::Rectangle<float> r, float rad) {
    g.setColour(accent.withAlpha(0.10f)); g.fillRoundedRectangle(r.expanded(2.5f), rad + 2.5f);
    juce::ColourGradient gr(accent.withAlpha(0.45f), r.getX(), r.getY(), accent.withAlpha(0.18f), r.getX(), r.getBottom(), false);
    g.setGradientFill(gr); g.fillRoundedRectangle(r, rad);
    g.setColour(juce::Colours::white.withAlpha(0.28f)); g.drawRoundedRectangle(r, rad, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.40f));
    g.fillRoundedRectangle(r.getX() + rad * 0.6f, r.getY() + 1.0f, juce::jmax(0.0f, r.getWidth() - rad * 1.2f), 1.0f, 0.5f);
  }
}

struct NSALook : juce::LookAndFeel_V4 {
  NSALook() {
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff17191b));
    setColour(juce::PopupMenu::textColourId, ui::fg);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ui::accent);
    setColour(juce::PopupMenu::highlightedTextColourId, ui::onAcc);
  }
  juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions(13.0f)); }
  int getSliderThumbRadius(juce::Slider&) override { return 12; }

  void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hl, bool) override {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    ui::glass(g, r, 13.0f, hl ? 0.10f : 0.07f);
    const float dx = r.getX() + 17.0f, dy = r.getCentreY();
    if (b.getToggleState()) { g.setColour(ui::accentHi.withAlpha(0.30f)); g.fillEllipse(dx - 7.0f, dy - 7.0f, 14.0f, 14.0f); }
    g.setColour(b.getToggleState() ? ui::accentHi : juce::Colour(0xff3a3732));
    g.fillEllipse(dx - 3.5f, dy - 3.5f, 7.0f, 7.0f);
    g.setColour(ui::fg); g.setFont(13.0f);
    g.drawText(b.getButtonText(), r.toNearestInt().withTrimmedLeft(32), juce::Justification::centredLeft);
  }

  // ползунок параметров: фиксированный размер, ручка не меняется при нажатии
  void drawLinearSlider(juce::Graphics& g, int, int y, int, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override {
    const float W = (float) s.getWidth(), cy = (float) y + (float) h * 0.5f;
    const float tx = juce::jlimit(12.0f, W - 12.0f, pos);
    const bool act = s.isMouseButtonDown();
    g.setColour(juce::Colours::white.withAlpha(0.10f)); g.fillRoundedRectangle(0.0f, cy - 2.0f, W, 4.0f, 2.0f);
    if (act) { g.setColour(ui::accent.withAlpha(0.25f)); g.fillRoundedRectangle(0.0f, cy - 4.0f, tx, 8.0f, 4.0f); }
    juce::ColourGradient gr(ui::accent.withAlpha(0.40f), 0.0f, cy, ui::accentHi, tx, cy, false);
    g.setGradientFill(gr); g.fillRoundedRectangle(0.0f, cy - 2.0f, juce::jmax(4.0f, tx), 4.0f, 2.0f);
    const auto th = juce::Rectangle<float>(24.0f, 24.0f).withCentre({ tx, cy });
    ui::glass(g, th, 12.0f, 0.09f);
    g.setColour(ui::accentHi.withAlpha(act ? 0.40f : 0.22f)); g.fillEllipse(tx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    g.setColour(ui::accentHi); g.fillEllipse(tx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
  }
};

// стеклянный переключатель: «капля» плавно (с лёгкой пружиной) переезжает между пунктами
struct GlassSeg : juce::Component, private juce::Timer {
  void setItems(const juce::StringArray& s, int i) {
    items = s; sel = juce::jlimit(0, juce::jmax(0, s.size() - 1), i); pos = (float) sel; vel = 0.0f; repaint();
  }
  void setIndex(int i, bool notify) {
    i = juce::jlimit(0, juce::jmax(0, items.size() - 1), i);
    if (i == sel) return;
    sel = i; startTimerHz(60);
    if (notify && onChange) onChange(sel);
  }
  juce::Rectangle<float> cell(float p) const {
    auto r = getLocalBounds().toFloat().reduced(3.0f);
    const float w = r.getWidth() / (float) juce::jmax(1, items.size());
    return { r.getX() + p * w, r.getY(), w, r.getHeight() };
  }
  void paint(juce::Graphics& g) override {
    ui::glass(g, getLocalBounds().toFloat().reduced(0.5f), 13.0f);
    if (items.isEmpty()) return;
    ui::pill(g, cell(pos), 10.0f);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    for (int i = 0; i < items.size(); ++i) {
      const float t = juce::jlimit(0.0f, 1.0f, 1.0f - std::abs(pos - (float) i));
      g.setColour(ui::mu.interpolatedWith(juce::Colours::white, t));
      g.drawText(items[i], cell((float) i).toNearestInt(), juce::Justification::centred);
    }
  }
  void mouseDown(const juce::MouseEvent& e) override {
    const int n = items.size(); if (n <= 0) return;
    auto r = getLocalBounds().toFloat().reduced(3.0f);
    setIndex((int) ((e.position.x - r.getX()) / (r.getWidth() / (float) n)), true);
  }
  void timerCallback() override {
    vel = (vel + ((float) sel - pos) * 0.18f) * 0.68f; pos += vel;
    if (std::abs(vel) < 0.001f && std::abs((float) sel - pos) < 0.001f) { pos = (float) sel; stopTimer(); }
    repaint();
  }
  juce::StringArray items; int sel = 0; float pos = 0.0f, vel = 0.0f; std::function<void(int)> onChange;
};

// тональность: ручка свободно едет за курсором, при отпускании мягко «садится» на ноту; ключ меняется при отпускании
struct KeySlider : juce::Component, private juce::Timer {
  void setIndex(int i, bool notify) {
    i = juce::jlimit(0, 12, i); cur = tar = (float) i; vel = 0.0f; committed = i; repaint();
    if (notify && onChange) onChange(i);
  }
  juce::Rectangle<float> inner() const { return getLocalBounds().toFloat().reduced(26.0f, 0.0f); }
  static juce::String nameOf(int i) { return i >= 12 ? juce::String("Free") : juce::String(NOTE_NAMES[i]); }
  void paint(juce::Graphics& g) override {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    ui::glass(g, r, 14.0f);
    const auto in = inner();
    for (int j = 0; j < 13; ++j) {
      const float x = in.getX() + in.getWidth() * (float) j / 12.0f;
      const float a = juce::jlimit(0.0f, 1.0f, std::abs((float) j - cur) / 1.3f);
      g.setColour(juce::Colours::white.withAlpha(0.30f * a)); g.fillEllipse(x - 1.5f, r.getCentreY() - 1.5f, 3.0f, 3.0f);
    }
    const float tx = in.getX() + in.getWidth() * cur / 12.0f;
    const auto t = juce::Rectangle<float>(50.0f, r.getHeight() - 8.0f).withCentre({ tx, r.getCentreY() });
    ui::pill(g, t, 11.0f);
    g.setColour(juce::Colours::white); g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(nameOf(juce::jlimit(0, 12, (int) std::lround(cur))), t.toNearestInt(), juce::Justification::centred);
  }
  float fromX(float x) const { const auto in = inner(); return juce::jlimit(0.0f, 12.0f, (x - in.getX()) / in.getWidth() * 12.0f); }
  void mouseDown(const juce::MouseEvent& e) override { drag = true; tar = fromX(e.position.x); startTimerHz(60); }
  void mouseDrag(const juce::MouseEvent& e) override { tar = fromX(e.position.x); }
  void mouseUp(const juce::MouseEvent&) override {
    drag = false; tar = std::round(tar);
    const int idx = (int) tar;
    if (idx != committed) { committed = idx; if (onChange) onChange(idx); }
  }
  void timerCallback() override {
    if (drag) { cur += (tar - cur) * 0.3f; vel = 0.0f; }
    else {
      vel = (vel + (tar - cur) * 0.1f) * 0.74f; cur += vel;
      if (std::abs(vel) < 0.001f && std::abs(tar - cur) < 0.001f) { cur = tar; stopTimer(); }
    }
    repaint();
  }
  float cur = 0.0f, tar = 0.0f, vel = 0.0f; bool drag = false; int committed = 0; std::function<void(int)> onChange;
};

// Generate (акцентная, с бликом) и Evolve (стекло)
struct GlassBtn : juce::Component, private juce::Timer {
  GlassBtn() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
  void startShine() { shine = 0.0f; startTimerHz(60); }
  void paint(juce::Graphics& g) override {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    if (down) r = r.withSizeKeepingCentre(r.getWidth() * 0.97f, r.getHeight() * 0.94f);
    if (primary) {
      juce::ColourGradient gr(ui::g1, r.getX(), r.getY(), ui::g2, r.getX(), r.getBottom(), false);
      g.setGradientFill(gr); g.fillRoundedRectangle(r, 14.0f);
      if (shine >= 0.0f) {
        juce::Graphics::ScopedSaveState ss(g);
        juce::Path cp; cp.addRoundedRectangle(r, 14.0f); g.reduceClipRegion(cp, juce::AffineTransform());
        const float bw = r.getWidth() * 0.4f, x = r.getX() + (shine * 1.9f - 0.6f) * r.getWidth(), sk = 8.0f;
        juce::Path band; band.startNewSubPath(x + sk, r.getY()); band.lineTo(x + bw + sk, r.getY());
        band.lineTo(x + bw, r.getBottom()); band.lineTo(x, r.getBottom()); band.closeSubPath();
        juce::ColourGradient sg(juce::Colours::white.withAlpha(0.0f), x, 0.0f, juce::Colours::white.withAlpha(0.0f), x + bw, 0.0f, false);
        sg.addColour(0.5, juce::Colours::white.withAlpha(0.55f));
        g.setGradientFill(sg); g.fillPath(band);
      }
      g.setColour(ui::onAcc);
    } else {
      ui::glass(g, r, 14.0f, hover ? 0.10f : 0.07f);
      g.setColour(ui::fg);
    }
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(text, getLocalBounds(), juce::Justification::centred);
  }
  void mouseEnter(const juce::MouseEvent&) override { hover = true; if (primary) startShine(); repaint(); }
  void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
  void mouseDown(const juce::MouseEvent&) override { down = true; if (primary) startShine(); repaint(); }
  void mouseUp(const juce::MouseEvent& e) override { down = false; repaint(); if (contains(e.getPosition()) && onClick) onClick(); }
  void timerCallback() override { shine += 0.022f; if (shine > 1.0f) { shine = -1.0f; stopTimer(); } repaint(); }
  juce::String text; bool primary = false; std::function<void()> onClick;
  bool hover = false, down = false; float shine = -1.0f;
};

struct DragBox : juce::Component {
  explicit DragBox(NSAProcessor& p) : proc(p) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
  void paint(juce::Graphics& g) override {
    auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(ui::accent.withAlpha(hover ? 0.20f : 0.13f)); g.fillRoundedRectangle(b, 14.0f);
    g.setColour(ui::accent.withAlpha(0.40f)); g.drawRoundedRectangle(b, 14.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.30f)); g.fillRoundedRectangle(b.getX() + 10.0f, b.getY() + 1.0f, b.getWidth() - 20.0f, 1.0f, 0.5f);
    g.setColour(ui::accentHi);
    for (int i = 0; i < 2; ++i) for (int j = 0; j < 3; ++j)
      g.fillEllipse(b.getX() + 16.0f + (float) i * 5.0f, b.getCentreY() - 7.0f + (float) j * 5.0f, 2.5f, 2.5f);
    g.setFont(14.0f);
    g.drawText("Drag MIDI to track", getLocalBounds(), juce::Justification::centred);
  }
  void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
  void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
  void mouseDrag(const juce::MouseEvent& e) override {
    if (!dragging && e.getDistanceFromDragStart() > 4) {
      dragging = true;
      auto f = proc.writeMidiFile();
      if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles(juce::StringArray(f.getFullPathName()), false, this, [this] { dragging = false; });
      else dragging = false;
    }
  }
  void mouseUp(const juce::MouseEvent&) override { dragging = false; }
  NSAProcessor& proc; bool dragging = false, hover = false;
};

struct HeartBtn : juce::Component, private juce::Timer {
  HeartBtn() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
  void setOn(bool v) { if (on != v) { on = v; repaint(); } }
  void setCount(int n) { if (count != n) { count = n; repaint(); } }
  void pop() { pulse = 5; startTimerHz(40); }
  void paint(juce::Graphics& g) override {
    const auto body = getLocalBounds().toFloat().withTrimmedTop(6.0f).withTrimmedRight(6.0f).reduced(0.5f);
    ui::glass(g, body, 14.0f, hover ? 0.10f : 0.07f);
    const auto r = body.reduced(11.0f, 9.0f);
    const float s = std::min(r.getWidth() / 32.0f, r.getHeight() / 29.0f) * (1.0f + 0.06f * (float) pulse);
    const float cx = r.getCentreX(), cy = r.getCentreY();
    juce::Path p; const int N = 64;
    for (int i = 0; i <= N; ++i) {
      const float t = juce::MathConstants<float>::twoPi * (float) i / (float) N, st = std::sin(t);
      const float x = 16.0f * st * st * st;
      const float y = 13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t);
      const float px = cx + x * s, py = cy - (y + 2.5f) * s;
      if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
    }
    p.closeSubPath();
    if (on) { g.setColour(ui::accent.withAlpha(0.28f)); g.strokePath(p, juce::PathStrokeType(5.0f)); }
    g.setColour(on ? ui::accentHi : (hover ? juce::Colour(0xffa39d8e) : ui::mu));
    g.fillPath(p);
    // бейдж со счётчиком
    const juce::String t(count); const float bw = juce::jmax(16.0f, 9.0f + 6.0f * (float) t.length());
    const auto bd = juce::Rectangle<float>(bw, 14.0f).withCentre({ (float) getWidth() - 14.0f, 9.0f });
    g.setColour(ui::bg); g.fillRoundedRectangle(bd, 7.0f);
    g.setColour(juce::Colours::white.withAlpha(0.15f)); g.drawRoundedRectangle(bd, 7.0f, 1.0f);
    g.setColour(ui::mu); g.setFont(11.0f); g.drawText(t, bd.toNearestInt(), juce::Justification::centred);
  }
  void mouseDown(const juce::MouseEvent& e) override {
    if (e.mods.isPopupMenu()) { if (onRight) onRight(); }
    else if (onClick) onClick();
  }
  void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
  void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
  void timerCallback() override { if (--pulse <= 0) { pulse = 0; stopTimer(); } repaint(); }
  std::function<void()> onClick, onRight;
  bool on = false, hover = false; int pulse = 0, count = 0;
};

// ПЭД: северное сияние в углах (цвет настроения), мерцающие звёзды, редкая падающая звезда
struct PadBox : juce::Component, private juce::Timer {
  explicit PadBox(NSAProcessor& p) : proc(p) {
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    t0 = juce::Time::getMillisecondCounterHiRes();
    sprite = juce::Image(juce::Image::ARGB, 32, 32, true);
    {
      juce::Graphics sg(sprite);
      juce::ColourGradient gr(juce::Colour(0xfffff8e6), 16.0f, 16.0f, juce::Colour(0x00fff8e6), 32.0f, 16.0f, true);
      gr.addColour(0.25, juce::Colour(0xfffff8e6).withAlpha(0.45f));
      sg.setGradientFill(gr); sg.fillAll();
    }
    nextShoot = 2.5f;
    startTimerHz(30);
  }
  float now() const { return (float) ((juce::Time::getMillisecondCounterHiRes() - t0) * 0.001); }
  void flash() { flashAt = now(); }
  juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(14.0f); }

  void resized() override {
    stars.clear();
    const float W = (float) getWidth(), H = (float) getHeight(); if (W <= 0.0f || H <= 0.0f) return;
    juce::Random rg(7); const int n = (int) (W * H / 850.0f);
    for (int i = 0; i < n; ++i) {
      Star s; s.x = rg.nextFloat() * W; s.y = rg.nextFloat() * H;
      s.b = 0.3f + std::pow(rg.nextFloat(), 1.6f) * 0.7f; s.r = 0.7f + rg.nextFloat() * 1.3f;
      s.p = rg.nextFloat() * 6.0f; s.s = 0.35f + rg.nextFloat() * 1.1f;
      stars.push_back(s);
    }
  }
  void timerCallback() override {
    const float tt = now(), W = (float) getWidth(), H = (float) getHeight();
    if (!shoot && tt > nextShoot && W > 0.0f) {
      shoot = true; shT0 = tt; shX = W * (0.4f + 0.55f * rnd.nextFloat()); shY = H * rnd.nextFloat() * 0.3f;
      shVX = -W * 0.62f; shVY = H * 0.5f; nextShoot = tt + 9.0f + rnd.nextFloat() * 9.0f;
    }
    repaint();
  }

  void paint(juce::Graphics& g) override {
    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth(), H = bounds.getHeight(), tt = now();
    juce::Path clip; clip.addRoundedRectangle(bounds, 14.0f);
    g.reduceClipRegion(clip, juce::AffineTransform());
    g.setColour(juce::Colour(0xff0e0d10)); g.fillRect(bounds);

    // сияние: у каждого угла своё настроение — Ethereal / Toxic / Candlelight / 4 AM
    static const int CC[4][3] = { {110,170,215}, {90,170,130}, {205,125,85}, {75,95,215} };
    static const float QX[4] = {0.0f, 1.0f, 0.0f, 1.0f}, QY[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    for (int qi = 0; qi < 4; ++qi) {
      const juce::Colour c = juce::Colour::fromRGB((juce::uint8) CC[qi][0], (juce::uint8) CC[qi][1], (juce::uint8) CC[qi][2]);
      const float bx = QX[qi] * W, by = QY[qi] * H;
      const float th0 = std::atan2((0.5f - QY[qi]) * H, (0.5f - QX[qi]) * W), L = std::hypot(W, H) * 0.2f;
      const float br = 0.78f + 0.22f * std::sin(tt * 0.3f + (float) qi * 1.9f), R0 = W * 0.34f;
      juce::ColourGradient gr(c.withAlpha(0.20f * br), bx, by, c.withAlpha(0.0f), bx + R0, by, true);
      g.setGradientFill(gr); g.fillEllipse(bx - R0, by - R0, 2.0f * R0, 2.0f * R0);
      for (int k = 0; k < 3; ++k) {
        const float ph = tt * (0.1f + 0.04f * (float) k) + (float) qi * 1.7f + (float) k * 2.1f;
        const float an = th0 + (float) (k - 1) * 0.6f + std::sin(ph) * 0.22f;
        const float d = L * (0.7f + 0.45f * std::sin(ph * 1.3f + (float) k));
        const float cx = bx + std::cos(an) * d, cy = by + std::sin(an) * d, R = W * (0.3f + 0.05f * (float) k);
        const float a = 0.15f + 0.06f * std::sin(ph * 1.7f + (float) k);
        juce::Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::scale(1.0f, 0.26f).rotated(an).translated(cx, cy));
        juce::ColourGradient gg(c.withAlpha(a), 0.0f, 0.0f, c.withAlpha(0.0f), R, 0.0f, true);
        g.setGradientFill(gg); g.fillEllipse(-R, -R, 2.0f * R, 2.0f * R);
      }
    }

    // звёзды: всегда светятся, у каждой свой ритм
    const float boost = juce::jmax(0.0f, 1.0f - (tt - flashAt) / 1.2f);
    for (const auto& s : stars) {
      const float a = juce::jmin(1.0f, s.b * (0.72f + 0.28f * std::sin(tt * s.s + s.p)) + boost * 0.4f);
      const float gz = s.r * (s.b > 0.6f ? 7.0f : 4.5f);
      g.setOpacity(a * 0.55f);
      g.drawImage(sprite, juce::Rectangle<float>(s.x - gz * 0.5f, s.y - gz * 0.5f, gz, gz));
      g.setColour(juce::Colour(0xfffff6e2).withAlpha(a));
      g.fillRect(s.x - s.r * 0.35f, s.y - s.r * 0.35f, s.r * 0.7f, s.r * 0.7f);
    }
    // редкая падающая звезда
    if (shoot) {
      const float ag = tt - shT0;
      if (ag > 0.9f) shoot = false;
      else {
        const float hx = shX + shVX * ag, hy = shY + shVY * ag, tx = hx - shVX * 0.22f, ty = hy - shVY * 0.22f;
        const float al = std::sin(ag / 0.9f * juce::MathConstants<float>::pi);
        juce::ColourGradient lg(juce::Colour(0xfffff8e6).withAlpha(0.0f), tx, ty, juce::Colour(0xfffff8e6).withAlpha(al), hx, hy, false);
        g.setGradientFill(lg); g.drawLine(tx, ty, hx, hy, 1.6f);
        g.setOpacity(al); g.drawImage(sprite, juce::Rectangle<float>(hx - 6.0f, hy - 6.0f, 12.0f, 12.0f));
      }
    }

    const float dk = proc.dark.load(), tn = proc.tense.load();
    // подписи углов: ярче у того, к которому ближе точка
    g.setFont(11.0f);
    auto lab = [&](const char* s, float cxn, float cyn, juce::Rectangle<int> rc, juce::Justification j) {
      const float d = std::hypot(dk - cxn, tn - cyn);
      g.setColour(ui::tag.withAlpha(0.3f + 0.7f * juce::jmax(0.0f, 1.0f - d / 1.2f)));
      g.drawText(s, rc, j);
    };
    const int w = getWidth(), h = getHeight();
    lab("Ethereal", 0.0f, 1.0f, { 12, 8, 110, 16 }, juce::Justification::left);
    lab("Toxic", 1.0f, 1.0f, { w - 122, 8, 110, 16 }, juce::Justification::right);
    lab("Candlelight", 0.0f, 0.0f, { 12, h - 24, 110, 16 }, juce::Justification::left);
    lab("4 AM", 1.0f, 0.0f, { w - 122, h - 24, 110, 16 }, juce::Justification::right);

    // точка: дышит, цвет смешивается из цветов углов
    const auto a = area();
    const float ox = a.getX() + dk * a.getWidth(), oy = a.getBottom() - tn * a.getHeight();
    const float wTL = (1.0f - dk) * tn, wTR = dk * tn, wBL = (1.0f - dk) * (1.0f - tn), wBR = dk * (1.0f - tn);
    int mc[3];
    for (int i = 0; i < 3; ++i)
      mc[i] = juce::jmin(255, (int) std::lround(wTL * (float) CC[0][i] + wTR * (float) CC[1][i] + wBL * (float) CC[2][i] + wBR * (float) CC[3][i]) + 40);
    const juce::Colour oc = juce::Colour::fromRGB((juce::uint8) mc[0], (juce::uint8) mc[1], (juce::uint8) mc[2]);
    const float breathe = 1.0f + 0.06f * (std::sin(tt * juce::MathConstants<float>::twoPi / 3.4f) + 1.0f);
    const float fe = juce::jmax(0.0f, 1.0f - (tt - flashAt) / 0.8f);
    const float sc = (down ? 1.25f : breathe) * (1.0f + 0.2f * fe);
    juce::ColourGradient gl(oc.withAlpha(0.60f), ox, oy, oc.withAlpha(0.0f), ox + 40.0f * sc, oy, true);
    gl.addColour(0.35, oc.withAlpha(0.28f));
    g.setGradientFill(gl); g.fillEllipse(ox - 40.0f * sc, oy - 40.0f * sc, 80.0f * sc, 80.0f * sc);
    g.setColour(oc.withAlpha(0.20f)); g.fillEllipse(ox - 20.0f * sc, oy - 20.0f * sc, 40.0f * sc, 40.0f * sc);
    juce::ColourGradient cg(juce::Colours::white, ox - 4.0f * sc, oy - 5.0f * sc, juce::Colour(0xffe8e3d8), ox + 13.0f * sc, oy + 13.0f * sc, true);
    g.setGradientFill(cg); g.fillEllipse(ox - 13.0f * sc, oy - 13.0f * sc, 26.0f * sc, 26.0f * sc);
    g.setColour(ui::accentHi); g.drawEllipse(ox - 12.0f * sc, oy - 12.0f * sc, 24.0f * sc, 24.0f * sc, 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawRoundedRectangle(bounds.reduced(0.5f), 14.0f, 1.0f);
  }
  void mouseDown(const juce::MouseEvent& e) override { down = true; move(e); }
  void mouseDrag(const juce::MouseEvent& e) override { move(e); }
  void mouseUp(const juce::MouseEvent&) override { down = false; }
  void move(const juce::MouseEvent& e) {
    const auto a = area();
    proc.dark = juce::jlimit(0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth());
    proc.tense = juce::jlimit(0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    proc.padChanged();
  }
  struct Star { float x = 0, y = 0, b = 0.5f, r = 1.0f, p = 0, s = 1.0f; };
  NSAProcessor& proc; std::vector<Star> stars; juce::Image sprite; juce::Random rnd;
  double t0 = 0; float nextShoot = 2.5f, flashAt = -10.0f, shT0 = 0, shX = 0, shY = 0, shVX = 0, shVY = 0;
  bool shoot = false, down = false;
};

class NSAEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit NSAEditor(NSAProcessor& p) : AudioProcessorEditor(&p), proc(p), pad(p), drag(p) {
    setLookAndFeel(&look);
    setSize(480, 650);

    voiSeg.setItems(juce::StringArray{"Close", "Open"}, proc.open.load());
    resSeg.setItems(juce::StringArray{"Home", "Hang", "Twist"}, proc.res.load());
    octSeg.setItems(juce::StringArray{"-1", "0", "+1"}, proc.octaveShift.load() + 1);
    keySlider.setIndex(proc.key.load(), false);
    voiSeg.onChange = [this](int i) { proc.open = i; proc.revoice(); };
    resSeg.onChange = [this](int i) { proc.res = i; proc.padChanged(); };
    octSeg.onChange = [this](int i) { proc.octaveShift = i - 1; };
    keySlider.onChange = [this](int i) { proc.setKey(i); };

    logoImg = juce::ImageFileFormat::loadFrom(logo::png, (size_t) logo::pngSize);
    initCtl(cNost, "Nostalgia", proc.nost.load(), [this](float v) { proc.nost = v; proc.padChanged(); });
    initCtl(cAir, "Air", proc.air.load(), [this](float v) { proc.air = v; proc.padChanged(); });
    initCtl(cHuman, "Human", proc.human.load(), [this](float v) { proc.human = v; });
    initCtl(cTaste, "Taste", proc.tasteAmt.load(), [this](float v) { proc.tasteAmt = v; }); // влияет на следующие Generate/Evolve
    heart.onClick = [this] { proc.toggleLike(); syncHeart(); if (proc.liked.load()) heart.pop(); };
    heart.onRight = [this] { showTasteMenu(); };

    genBtn.text = "Generate"; genBtn.primary = true;
    genBtn.onClick = [this] { proc.generate(); pad.flash(); };
    contBtn.text = "Evolve";
    contBtn.onClick = [this] { proc.evolve(); pad.flash(); };

    nobassBtn.setButtonText("No bass");
    nobassBtn.setToggleState(proc.noBass.load(), juce::dontSendNotification);
    nobassBtn.onClick = [this] { proc.noBass = nobassBtn.getToggleState(); };
    strumBtn.setButtonText("Strum");
    strumBtn.setToggleState(proc.strum.load(), juce::dontSendNotification);
    strumBtn.onClick = [this] { proc.strum = strumBtn.getToggleState(); };

    for (juce::Component* c : std::initializer_list<juce::Component*>{&voiSeg, &resSeg, &octSeg, &nobassBtn, &strumBtn, &pad, &keySlider, &genBtn, &contBtn, &drag, &heart})
      addAndMakeVisible(c);
    startTimerHz(4); syncHeart();
  }
  ~NSAEditor() override { setLookAndFeel(nullptr); }

  void paint(juce::Graphics& g) override {
    g.fillAll(ui::bg);
    const float lh = 30.0f, lw = lh * (float) logoImg.getWidth() / (float) juce::jmax(1, logoImg.getHeight());
    if (logoImg.isValid())
      g.drawImage(logoImg, juce::Rectangle<float>(21.0f, 14.0f, lw, lh), juce::RectanglePlacement::stretchToFit);
    g.setColour(juce::Colour(0xffe6e6e6));
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)).withExtraKerningFactor(0.3f));
    g.drawText("NIGHT SESSION AUDIO", juce::Rectangle<int>(21 + (int) lw + 8, 14, 240, 30), juce::Justification::centredLeft);
    g.setColour(ui::mu); g.setFont(11.0f);
    g.drawText("Voicing", capVoi, juce::Justification::centredLeft);
    g.drawText("Ending", capRes, juce::Justification::centredLeft);
    g.drawText("Octave", capOct, juce::Justification::centredLeft);
    g.drawText("Key", capKey, juce::Justification::centredLeft);
  }
  void resized() override {
    auto r = getLocalBounds().reduced(18); r.removeFromTop(34);

    // три колонки 1 : 1.5 : 1.1 с зазором 10
    auto split3 = [](juce::Rectangle<int> row) {
      const int avail = row.getWidth() - 20, w1 = (int) ((float) avail / 3.6f), w2 = (int) ((float) avail * 1.5f / 3.6f);
      std::array<juce::Rectangle<int>, 3> c;
      c[0] = row.removeFromLeft(w1); row.removeFromLeft(10);
      c[1] = row.removeFromLeft(w2); row.removeFromLeft(10);
      c[2] = row; return c;
    };
    auto lab = split3(r.removeFromTop(14));
    capVoi = lab[0].withTrimmedLeft(2); capRes = lab[1].withTrimmedLeft(2); capOct = lab[2].withTrimmedLeft(2);
    r.removeFromTop(4);
    auto sg = split3(r.removeFromTop(32));
    voiSeg.setBounds(sg[0]); resSeg.setBounds(sg[1]); octSeg.setBounds(sg[2]);

    r.removeFromTop(10); auto row = r.removeFromTop(32); const int wt = (row.getWidth() - 10) / 2;
    nobassBtn.setBounds(row.removeFromLeft(wt)); row.removeFromLeft(10); strumBtn.setBounds(row);

    r.removeFromTop(14); pad.setBounds(r.removeFromTop(200));

    r.removeFromTop(12); capKey = r.removeFromTop(14).withTrimmedLeft(2);
    r.removeFromTop(4); keySlider.setBounds(r.removeFromTop(38));

    r.removeFromTop(14); row = r.removeFromTop(40); const int hw = (row.getWidth() - 22) / 2;
    placeCtl(cNost, row.removeFromLeft(hw)); row.removeFromLeft(22); placeCtl(cAir, row);
    r.removeFromTop(8); row = r.removeFromTop(40);
    placeCtl(cHuman, row.removeFromLeft(hw)); row.removeFromLeft(22); placeCtl(cTaste, row);

    r.removeFromTop(14); row = r.removeFromTop(42);
    auto hb = row.removeFromRight(52); row.removeFromRight(10);
    heart.setBounds(hb.withTop(hb.getY() - 6));
    const int bw = (row.getWidth() - 10) / 2;
    genBtn.setBounds(row.removeFromLeft(bw)); row.removeFromLeft(10); contBtn.setBounds(row);

    r.removeFromTop(10); drag.setBounds(r.removeFromTop(38));
  }
  struct Ctl { juce::Label name, val; juce::Slider s; };
  static void setValText(Ctl& c) { c.val.setText(juce::String((int) std::lround(c.s.getValue())), juce::dontSendNotification); }
  void initCtl(Ctl& c, const char* nm, float v, std::function<void(float)> cb) {
    c.name.setText(nm, juce::dontSendNotification); c.name.setFont(13.0f);
    c.name.setColour(juce::Label::textColourId, ui::fg);
    c.val.setFont(12.0f); c.val.setJustificationType(juce::Justification::centredRight);
    c.val.setColour(juce::Label::textColourId, ui::mu);
    c.s.setSliderStyle(juce::Slider::LinearHorizontal);
    c.s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    c.s.setRange(0.0, 100.0, 1.0);
    c.s.setValue((double) v * 100.0, juce::dontSendNotification); setValText(c);
    c.s.onValueChange = [this, &c, cb] { setValText(c); cb((float) (c.s.getValue() / 100.0)); };
    addAndMakeVisible(c.name); addAndMakeVisible(c.val); addAndMakeVisible(c.s);
  }
  static void placeCtl(Ctl& c, juce::Rectangle<int> a) {
    auto top = a.removeFromTop(16);
    c.val.setBounds(top.removeFromRight(34)); c.name.setBounds(top);
    c.s.setBounds(a);
  }
  void setCtl(Ctl& c, float v) { c.s.setValue((double) v * 100.0, juce::dontSendNotification); setValText(c); }
  void syncFromProc() {
    setCtl(cNost, proc.nost.load()); setCtl(cAir, proc.air.load()); setCtl(cHuman, proc.human.load());
    resSeg.setIndex(proc.res.load(), false);
    strumBtn.setToggleState(proc.strum.load(), juce::dontSendNotification);
    pad.repaint();
  }
  void syncHeart() {
    heart.setOn(proc.liked.load());
    heart.setCount(proc.tasteCount.load());
  }
  void showTasteMenu() {
    const int n = proc.tasteCount.load();
    juce::PopupMenu m;
    m.addItem(1, "Reset taste (" + juce::String(n) + " liked)", n > 0);
    juce::Component::SafePointer<NSAEditor> sp(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&heart), [sp](int r) {
      if (sp != nullptr && r == 1) { sp->proc.resetTaste(); sp->syncHeart(); }
    });
  }
  void timerCallback() override { syncHeart(); }
private:
  NSALook look;
  NSAProcessor& proc;
  GlassSeg voiSeg, resSeg, octSeg; KeySlider keySlider;
  Ctl cNost, cAir, cHuman, cTaste;
  GlassBtn genBtn, contBtn;
  juce::ToggleButton nobassBtn, strumBtn;
  PadBox pad; DragBox drag; HeartBtn heart; juce::Image logoImg;
  juce::Rectangle<int> capVoi, capRes, capOct, capKey;
};

juce::AudioProcessorEditor* NSAProcessor::createEditor() { return new NSAEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NSAProcessor(); }
