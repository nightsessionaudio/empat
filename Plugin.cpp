#include <juce_audio_utils/juce_audio_utils.h>
#include <climits>
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
    Feel f; f.nost = nost.load(); f.res = res.load(); f.air = air.load(); f.tear = tear.load(); f.human = human.load();
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
  const juce::Colour bg(0xff0e0d0b), fg(0xffebe5d6), mu(0xff7f7867), chip(0xff1a1813), chipHi(0xff23201a),
                     line(0xff2f2b22), gold(0xffb79a5b), goldHi(0xffd9bf86), ink(0xff0e0d0b), tag(0xffe0d8c4);
}

struct NSALook : juce::LookAndFeel_V4 {
  NSALook() {
    setColour(juce::ComboBox::textColourId, ui::fg);
    setColour(juce::PopupMenu::backgroundColourId, ui::chip);
    setColour(juce::PopupMenu::textColourId, ui::fg);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ui::gold);
    setColour(juce::PopupMenu::highlightedTextColourId, ui::ink);
  }
  juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(juce::FontOptions(12.0f)); }
  juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions(13.0f)); }
  juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::Font(juce::FontOptions(13.0f)); }

  void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
    auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(0.5f);
    g.setColour(box.isMouseOver() ? ui::chipHi : ui::chip); g.fillRoundedRectangle(r, 10.0f);
    g.setColour(ui::line); g.drawRoundedRectangle(r, 10.0f, 1.0f);
    const float cx = (float) w - 10.0f, cy = (float) h * 0.5f;
    juce::Path p; p.startNewSubPath(cx - 3.5f, cy - 1.5f); p.lineTo(cx, cy + 2.0f); p.lineTo(cx + 3.5f, cy - 1.5f);
    g.setColour(ui::gold); g.strokePath(p, juce::PathStrokeType(1.3f));
  }
  void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
    label.setBounds(2, 1, box.getWidth() - 18, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box)); label.setJustificationType(juce::Justification::centred);
  }
  void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool hl, bool down) override {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    auto col = c; if (down) col = col.darker(0.15f); else if (hl) col = col.brighter(0.08f);
    g.setColour(col); g.fillRoundedRectangle(r, 10.0f);
    if (c != ui::gold) { g.setColour(ui::line); g.drawRoundedRectangle(r, 10.0f, 1.0f); }
  }
  void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hl, bool) override {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(hl ? ui::chipHi : ui::chip); g.fillRoundedRectangle(r, 10.0f);
    g.setColour(ui::line); g.drawRoundedRectangle(r, 10.0f, 1.0f);
    g.setColour(b.getToggleState() ? ui::gold : ui::line);
    g.fillEllipse(r.getX() + 14.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour(ui::fg); g.setFont(13.0f);
    g.drawText(b.getButtonText(), r.toNearestInt().withTrimmedLeft(28), juce::Justification::centredLeft);
  }
  void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override {
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour(ui::line); g.fillRect((float) x, cy - 1.0f, (float) w, 2.0f);
    g.setColour(ui::gold); g.fillRect((float) x, cy - 1.0f, pos - (float) x, 2.0f);
    g.fillEllipse(pos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
  }
};

struct DragBox : juce::Component {
  explicit DragBox(NSAProcessor& p) : proc(p) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
  void paint(juce::Graphics& g) override {
    auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(ui::gold); g.fillRoundedRectangle(b, 10.0f);
    g.setColour(ui::ink.withAlpha(0.65f));
    for (int i = 0; i < 2; ++i) for (int j = 0; j < 3; ++j)
      g.fillEllipse(b.getX() + 16.0f + (float) i * 5.0f, b.getCentreY() - 7.0f + (float) j * 5.0f, 2.5f, 2.5f);
    g.setColour(ui::ink); g.setFont(14.0f);
    g.drawText("Drag MIDI to track", getLocalBounds(), juce::Justification::centred);
  }
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
  NSAProcessor& proc; bool dragging = false;
};

struct HeartBtn : juce::Component, private juce::Timer {
  HeartBtn() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
  void setOn(bool v) { if (on != v) { on = v; repaint(); } }
  void pop() { pulse = 5; startTimerHz(40); }
  void paint(juce::Graphics& g) override {
    const auto r = getLocalBounds().toFloat().reduced(3.0f);
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
    g.setColour(on ? juce::Colour(0xffe0455c) : (hover ? juce::Colour(0xff8d8777) : juce::Colour(0xff625d50)));
    g.fillPath(p);
    if (on) { g.setColour(juce::Colour(0xffff8497).withAlpha(0.55f)); g.strokePath(p, juce::PathStrokeType(1.0f)); }
  }
  void mouseDown(const juce::MouseEvent& e) override {
    if (e.mods.isPopupMenu()) { if (onRight) onRight(); }
    else if (onClick) onClick();
  }
  void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
  void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
  void timerCallback() override { if (--pulse <= 0) { pulse = 0; stopTimer(); } repaint(); }
  std::function<void()> onClick, onRight;
  bool on = false, hover = false; int pulse = 0;
};

struct PadBox : juce::Component {
  explicit PadBox(NSAProcessor& p) : proc(p) {}
  juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(9.0f); }

  void resized() override {
    const int w = getWidth(), h = getHeight(); if (w <= 0 || h <= 0) return;
    const int S = 2;
    bgImg = juce::Image(juce::Image::ARGB, w * S, h * S, true);
    juce::Graphics g(bgImg); g.addTransform(juce::AffineTransform::scale((float) S));
    juce::Path clip; clip.addRoundedRectangle(0.0f, 0.0f, (float) w, (float) h, 10.0f);
    g.reduceClipRegion(clip, juce::AffineTransform());
    g.fillAll(juce::Colour(0xff0f0e0c));
    auto glow = [&](float cx, float cy, float r, juce::Colour c) {
      juce::ColourGradient gr(c, cx, cy, c.withAlpha(0.0f), cx + r, cy, true);
      gr.addColour(0.5, c.withMultipliedAlpha(0.45f));
      g.setGradientFill(gr); g.fillRect(0.0f, 0.0f, (float) w, (float) h);
    };
    const float fw = (float) w, fh = (float) h;
    glow(0.0f, 0.0f, fw * 0.55f, juce::Colour(84, 122, 152).withAlpha(0.40f));     // Ethereal: пыльно-голубой
    glow(0.0f, fh, fw * 0.55f, juce::Colour(146, 76, 56).withAlpha(0.40f));        // Candlelight: ember
    glow(fw, 0.0f, fw * 0.50f, juce::Colour(68, 116, 96).withAlpha(0.36f));        // Toxic: меланхоличный зелёный
    glow(fw, fh, fw * 0.55f, juce::Colour(26, 40, 88).withAlpha(0.62f));           // 4 AM: ночной синий
    glow(fw * 0.76f, fh * 0.24f, fw * 0.22f, juce::Colour(120, 185, 150).withAlpha(0.13f)); // блик-сияние
    juce::Random rnd(7);
    const int n = (w * h) / 160;
    for (int i = 0; i < n; ++i) {
      const float z = rnd.nextFloat() < 0.8f ? 1.0f : 1.6f, o = 0.15f + rnd.nextFloat() * 0.5f;
      const bool gl = rnd.nextFloat() < 0.6f;
      const float x = rnd.nextFloat() * fw, y = rnd.nextFloat() * fh;
      g.setColour((gl ? ui::goldHi : ui::fg).withAlpha(o)); g.fillEllipse(x, y, z, z);
    }
  }

  void paint(juce::Graphics& g) override {
    if (bgImg.isValid()) g.drawImage(bgImg, getLocalBounds().toFloat());
    const int w = getWidth(), h = getHeight();
    g.setColour(ui::tag); g.setFont(11.0f);
    g.drawText("Ethereal", 10, 6, 100, 16, juce::Justification::left);
    g.drawText("Toxic", w - 110, 6, 100, 16, juce::Justification::right);
    g.drawText("Candlelight", 10, h - 22, 100, 16, juce::Justification::left);
    g.drawText("4 AM", w - 110, h - 22, 100, 16, juce::Justification::right);
    const auto a = area();
    const float x = a.getX() + proc.dark.load() * a.getWidth();
    const float y = a.getBottom() - proc.tense.load() * a.getHeight();
    g.setColour(ui::fg); g.fillEllipse(x - 6.5f, y - 6.5f, 13.0f, 13.0f);
    g.setColour(ui::gold); g.drawEllipse(x - 6.5f, y - 6.5f, 13.0f, 13.0f, 2.0f);
  }
  void mouseDown(const juce::MouseEvent& e) override { move(e); }
  void mouseDrag(const juce::MouseEvent& e) override { move(e); }
  void move(const juce::MouseEvent& e) {
    const auto a = area();
    proc.dark = juce::jlimit(0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth());
    proc.tense = juce::jlimit(0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    proc.padChanged();
    repaint();
  }
  NSAProcessor& proc; juce::Image bgImg;
};

class NSAEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit NSAEditor(NSAProcessor& p) : AudioProcessorEditor(&p), proc(p), pad(p), drag(p) {
    setLookAndFeel(&look);
    setSize(400, 538);
    keyBox.addItemList(juce::StringArray{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B","Free key"}, 1);
    voiBox.addItemList(juce::StringArray{"Close","Open"}, 1);
    octBox.addItemList(juce::StringArray{"Oct: -1","Oct: 0","Oct: +1"}, 1);

    keyBox.setSelectedItemIndex(proc.key, juce::dontSendNotification);
    voiBox.setSelectedItemIndex(proc.open, juce::dontSendNotification);
    octBox.setSelectedItemIndex(proc.octaveShift + 1, juce::dontSendNotification);

    keyBox.onChange = [this] { proc.setKey(keyBox.getSelectedItemIndex()); };
    voiBox.onChange = [this] { proc.open = voiBox.getSelectedItemIndex(); proc.revoice(); };
    octBox.onChange = [this] { proc.octaveShift = octBox.getSelectedItemIndex() - 1; };

    logoImg = juce::ImageFileFormat::loadFrom(logo::png, (size_t) logo::pngSize);
    resBox.addItemList(juce::StringArray{"End: Home","End: Hang","End: Twist"}, 1);
    resBox.setSelectedItemIndex(proc.res.load(), juce::dontSendNotification);
    resBox.onChange = [this] { proc.res = resBox.getSelectedItemIndex(); proc.padChanged(); };
    initCtl(cNost, "Nostalgia", proc.nost.load(), [this](float v) { proc.nost = v; proc.padChanged(); });
    initCtl(cAir, "Air", proc.air.load(), [this](float v) { proc.air = v; proc.padChanged(); });
    initCtl(cTear, "Tear", proc.tear.load(), [this](float v) { proc.tear = v; proc.padChanged(); });
    initCtl(cHuman, "Human", proc.human.load(), [this](float v) { proc.human = v; });
    initCtl(cTaste, "Taste", proc.tasteAmt.load(), [this](float v) { proc.tasteAmt = v; }); // влияет на следующие Generate/Evolve
    tasteInfo.setFont(13.0f); tasteInfo.setJustificationType(juce::Justification::centredLeft);
    tasteInfo.setColour(juce::Label::textColourId, ui::mu);
    heart.onClick = [this] { proc.toggleLike(); syncHeart(); if (proc.liked.load()) heart.pop(); };
    heart.onRight = [this] { showTasteMenu(); };

    genBtn.setButtonText("Generate"); genBtn.onClick = [this] { proc.generate(); };
    contBtn.setButtonText("Evolve"); contBtn.onClick = [this] { proc.evolve(); };
    genBtn.setColour(juce::TextButton::buttonColourId, ui::gold);
    genBtn.setColour(juce::TextButton::textColourOffId, ui::ink);
    contBtn.setColour(juce::TextButton::buttonColourId, ui::chip);
    contBtn.setColour(juce::TextButton::textColourOffId, ui::fg);

    nobassBtn.setButtonText("No bass");
    nobassBtn.setToggleState(proc.noBass.load(), juce::dontSendNotification);
    nobassBtn.onClick = [this] { proc.noBass = nobassBtn.getToggleState(); };

    strumBtn.setButtonText("Strum");
    strumBtn.setToggleState(proc.strum.load(), juce::dontSendNotification);
    strumBtn.onClick = [this] { proc.strum = strumBtn.getToggleState(); };

    label.setJustificationType(juce::Justification::centred); label.setFont(13.0f);
    label.setMinimumHorizontalScale(0.7f);
    label.setColour(juce::Label::textColourId, ui::fg);

    for (juce::Component* c : std::initializer_list<juce::Component*>{&keyBox, &voiBox, &resBox, &octBox, &pad, &genBtn, &contBtn, &nobassBtn, &strumBtn, &label, &drag, &heart, &tasteInfo})
      addAndMakeVisible(c);
    startTimerHz(4); timerCallback();
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
  }
  void resized() override {
    auto r = getLocalBounds().reduced(18); r.removeFromTop(34);
    auto row = r.removeFromTop(28); const int w4 = row.getWidth() / 4;
    keyBox.setBounds(row.removeFromLeft(w4).reduced(2, 0));
    voiBox.setBounds(row.removeFromLeft(w4).reduced(2, 0));
    resBox.setBounds(row.removeFromLeft(w4).reduced(2, 0));
    octBox.setBounds(row.reduced(2, 0));

    r.removeFromTop(8); pad.setBounds(r.removeFromTop(150).reduced(3, 0));

    r.removeFromTop(8); row = r.removeFromTop(30); const int wt = row.getWidth() / 2;
    nobassBtn.setBounds(row.removeFromLeft(wt).reduced(3, 0));
    strumBtn.setBounds(row.reduced(3, 0));

    r.removeFromTop(8); row = r.removeFromTop(34); placeCtl(cNost, row.removeFromLeft(row.getWidth() / 2)); placeCtl(cAir, row);
    r.removeFromTop(6); row = r.removeFromTop(34); placeCtl(cTear, row.removeFromLeft(row.getWidth() / 2)); placeCtl(cHuman, row);
    r.removeFromTop(6); row = r.removeFromTop(34); placeCtl(cTaste, row);

    r.removeFromTop(8); row = r.removeFromTop(34); const int bw = row.getWidth() / 2;
    genBtn.setBounds(row.removeFromLeft(bw).reduced(3, 0));
    contBtn.setBounds(row.reduced(3, 0));

    r.removeFromTop(8); row = r.removeFromTop(26);
    auto hb = row.removeFromRight(70);
    heart.setBounds(hb.removeFromLeft(26)); tasteInfo.setBounds(hb.withTrimmedLeft(2));
    label.setBounds(row);
    r.removeFromTop(8); drag.setBounds(r.reduced(3, 0));
  }
  struct Ctl { juce::Label name, val; juce::Slider s; };
  static void setValText(Ctl& c) { c.val.setText(juce::String((int) std::lround(c.s.getValue())), juce::dontSendNotification); }
  void initCtl(Ctl& c, const char* nm, float v, std::function<void(float)> cb) {
    c.name.setText(nm, juce::dontSendNotification); c.name.setFont(13.0f);
    c.name.setColour(juce::Label::textColourId, ui::fg);
    c.val.setFont(13.0f); c.val.setJustificationType(juce::Justification::centredRight);
    c.val.setColour(juce::Label::textColourId, ui::mu);
    c.s.setSliderStyle(juce::Slider::LinearHorizontal);
    c.s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    c.s.setRange(0.0, 100.0, 1.0);
    c.s.setValue((double) v * 100.0, juce::dontSendNotification); setValText(c);
    c.s.onValueChange = [this, &c, cb] { setValText(c); cb((float) (c.s.getValue() / 100.0)); };
    addAndMakeVisible(c.name); addAndMakeVisible(c.val); addAndMakeVisible(c.s);
  }
  static void placeCtl(Ctl& c, juce::Rectangle<int> a) {
    a.reduce(4, 0);
    auto top = a.removeFromTop(16);
    c.val.setBounds(top.removeFromRight(34)); c.name.setBounds(top);
    c.s.setBounds(a);
  }
  void setCtl(Ctl& c, float v) { c.s.setValue((double) v * 100.0, juce::dontSendNotification); setValText(c); }
  void syncFromProc() {
    setCtl(cNost, proc.nost.load()); setCtl(cAir, proc.air.load());
    setCtl(cTear, proc.tear.load()); setCtl(cHuman, proc.human.load());
    resBox.setSelectedItemIndex(proc.res.load(), juce::dontSendNotification);
    strumBtn.setToggleState(proc.strum.load(), juce::dontSendNotification);
    pad.repaint();
  }
  void syncHeart() {
    heart.setOn(proc.liked.load());
    tasteInfo.setText("(" + juce::String(proc.tasteCount.load()) + ")", juce::dontSendNotification);
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
  void timerCallback() override { syncHeart(); label.setText(proc.getNames().replace("   |   ", juce::String::fromUTF8("  \xc2\xb7  ")), juce::dontSendNotification); }
private:
  NSALook look;
  NSAProcessor& proc;
  juce::ComboBox keyBox, voiBox, octBox, resBox;
  Ctl cNost, cAir, cTear, cHuman, cTaste;
  juce::TextButton genBtn, contBtn;
  juce::ToggleButton nobassBtn, strumBtn;
  juce::Label label, tasteInfo; PadBox pad; DragBox drag; HeartBtn heart; juce::Image logoImg;
};

juce::AudioProcessorEditor* NSAProcessor::createEditor() { return new NSAEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NSAProcessor(); }
