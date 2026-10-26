#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

enum { MAJ7, MAJ9, M7, M9, M11, D9, D13, MAJ7S11, D7B9, SUS9, MMAJ7, SIX9, ADD9, M7B5 };
inline const std::vector<int> IV[] = {
  {0,4,7,11},{0,4,7,11,14},{0,3,7,10},{0,3,7,10,14},{0,3,7,10,14,17},
  {0,4,7,10,14},{0,4,10,14,21},{0,4,7,11,18},{0,4,7,10,13},{0,5,7,10,14},
  {0,3,7,11},{0,4,7,9,14},{0,4,7,14},{0,3,6,10}};
inline const char* SUF[] = {"maj7","maj9","m7","m9","m11","9","13","maj7#11","7b9","9sus4","m(maj7)","6/9","add9","m7b5"};
inline const char* NOTE_NAMES[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

inline double tensOf(int t) {
  switch (t) {
    case MAJ7: case MAJ9: case ADD9: case SIX9: return 0.0;
    case M7: case M9: return 0.2;
    case M11: return 0.3;
    case MAJ7S11: case SUS9: return 0.5;
    case D9: return 0.6;
    case D13: return 0.7;
    case MMAJ7: case M7B5: return 0.8;
    case D7B9: return 1.0;
    default: return 0.3;
  }
}

struct Ch { int r = 0, t = 0, b = -1; };
struct Prog { Ch c[4]; float dk, tn; bool tw = false; }; // tw: прогрессия с заранее выверенным обманным финалом (только для End: Twist)
inline Ch mk(int r, int t, int b = -1) { return Ch{r, t, b}; }
inline Prog mkP(Ch a, Ch b, Ch c, Ch d, float dk, float tn) { return Prog{{a, b, c, d}, dk, tn}; }
inline Prog mkT(Ch a, Ch b, Ch c, Ch d, float dk, float tn) { Prog p = mkP(a, b, c, d, dk, tn); p.tw = true; return p; }

// ЭЛИТАРНЫЙ БАНК R&B / TRAPSOUL ПРОГРЕССИЙ (ТОЛЬКО ВЫСШИЙ КЛАСС, ИДЕАЛЬНО ВЫВЕРЕННЫЕ ЦЕПОЧКИ)
inline const std::vector<Prog> BANK = {
  // --- Candlelight & Dates (Левый нижний квадрант: Теплый соул, мажорные переливы, интимные 90-е) ---
  mkP(mk(0,MAJ9), mk(9,M9), mk(5,MAJ9), mk(7,SUS9),              0.15f, 0.15f),
  mkP(mk(0,SIX9), mk(5,MAJ9), mk(2,M9), mk(7,ADD9),              0.20f, 0.10f),
  mkP(mk(5,MAJ9), mk(4,M9), mk(9,M9), mk(0,MAJ9),               0.10f, 0.15f),
  mkP(mk(0,ADD9), mk(7,SUS9), mk(9,M9), mk(5,MAJ7S11),          0.25f, 0.20f),
  mkP(mk(2,M9), mk(7,SUS9), mk(0,MAJ9), mk(9,M9),               0.20f, 0.20f),
  mkP(mk(0,MAJ7), mk(3,MAJ9), mk(5,MAJ9), mk(7,SUS9),           0.20f, 0.25f),
  mkP(mk(9,M9), mk(5,MAJ9), mk(0,MAJ9), mk(7,SUS9),             0.30f, 0.15f),
  mkP(mk(5,MAJ7S11), mk(4,M7), mk(2,M9), mk(0,MAJ9),            0.25f, 0.25f),

  // --- 4 AM Intimacy (Правый нижний квадрант: Глубокие ночные миноры, бархат, m11, m9) ---
  mkP(mk(0,M11), mk(8,MAJ9), mk(5,M9), mk(7,SUS9),              0.80f, 0.35f),
  mkP(mk(0,M9), mk(8,MAJ9), mk(5,M9), mk(7,D9),                 0.75f, 0.40f),
  mkP(mk(9,M11), mk(2,M9), mk(7,D13), mk(0,MAJ9),               0.65f, 0.35f),
  mkP(mk(0,M9), mk(3,MAJ9), mk(8,MAJ9), mk(7,D9),                0.70f, 0.30f),
  mkP(mk(5,M9), mk(0,M11), mk(8,MAJ9), mk(7,D7B9),              0.75f, 0.45f),
  mkP(mk(0,M11), mk(5,M9), mk(10,D9), mk(8,MAJ9),               0.70f, 0.40f),
  mkP(mk(0,M9), mk(10,D13), mk(8,MAJ9), mk(7,D7B9),             0.70f, 0.50f),

  // --- Ethereal Drama (Левый верхний квадрант: Воздушная драма, мажорные надстройки #11, чувственный трепет) ---
  mkP(mk(0,MAJ9), mk(9,M9), mk(5,MAJ7S11), mk(5,M9),            0.30f, 0.55f),
  mkP(mk(0,SIX9), mk(8,MAJ7S11), mk(5,MAJ9), mk(7,SUS9),        0.25f, 0.60f),
  mkP(mk(5,MAJ7S11), mk(4,M9), mk(9,M11), mk(7,SUS9),           0.30f, 0.50f),
  mkP(mk(0,MAJ7S11), mk(2,D9), mk(4,M9), mk(5,MAJ7S11),         0.35f, 0.65f),
  mkP(mk(0,MAJ9), mk(3,D9), mk(5,MAJ7S11), mk(7,SUS9),          0.30f, 0.60f),

  // --- Toxic TrapSoul (Правый верхний квадрант: Мрачный ночной мегаполис, tension, d7b9, m7b5, mmaj7) ---
  mkP(mk(0,MMAJ7), mk(8,MAJ7S11), mk(5,M11), mk(7,D7B9),        0.95f, 0.85f),
  mkP(mk(0,M9), mk(2,M7B5), mk(7,D7B9), mk(0,M11),              0.85f, 0.80f),
  mkP(mk(0,MMAJ7), mk(0,M7,10), mk(8,MAJ9), mk(7,D7B9),         0.90f, 0.80f),
  mkP(mk(0,M11), mk(8,MAJ7S11), mk(1,MAJ9), mk(7,D7B9),         0.90f, 0.75f),
  mkP(mk(0,MMAJ7), mk(5,M9), mk(2,M7B5), mk(7,D7B9),            0.85f, 0.85f),
  mkP(mk(2,M7B5), mk(7,D7B9), mk(0,M11), mk(9,M9),              0.80f, 0.80f),
  mkP(mk(9,M11), mk(5,MAJ7S11), mk(2,M9), mk(7,D7B9),           0.75f, 0.75f),
  mkP(mk(0,M9), mk(8,MAJ9), mk(3,MAJ9), mk(7,D7B9),             0.80f, 0.70f),

  // ===== РАСШИРЕНИЕ: Candlelight & Dates (90s soul, Kaleem Taylor, Nobu Woods) =====
  mkP(mk(0,MAJ9), mk(4,M7), mk(9,M9), mk(2,D9),                  0.15f, 0.20f),
  mkP(mk(5,MAJ9), mk(0,MAJ9,4), mk(2,M9), mk(7,SUS9),            0.15f, 0.15f),
  mkP(mk(4,M9), mk(9,M9), mk(2,M9), mk(7,SUS9),                  0.25f, 0.15f),
  mkP(mk(0,MAJ9), mk(5,MAJ9,0), mk(4,M9), mk(2,M9),              0.15f, 0.10f),
  mkP(mk(0,MAJ9), mk(10,D9), mk(5,MAJ9), mk(0,SIX9),             0.30f, 0.30f),
  mkP(mk(9,M9), mk(2,M9), mk(7,SUS9), mk(0,MAJ9),                0.30f, 0.15f),
  mkP(mk(0,MAJ9), mk(6,M7B5), mk(5,MAJ9), mk(4,M9),              0.20f, 0.30f),
  mkP(mk(0,SIX9), mk(9,M7), mk(2,M9), mk(7,D13),                 0.20f, 0.25f),

  // ===== РАСШИРЕНИЕ: 4 AM Intimacy (PARTYNEXTDOOR, Drake, The Code) =====
  mkP(mk(0,M9), mk(8,MAJ9), mk(3,MAJ9), mk(10,SUS9),             0.70f, 0.30f),
  mkP(mk(0,M11), mk(5,M9), mk(8,MAJ7S11), mk(3,MAJ9),            0.75f, 0.40f),
  mkP(mk(8,MAJ9), mk(7,D9), mk(0,M9), mk(5,M11),                 0.70f, 0.40f),
  mkP(mk(0,M9), mk(3,MAJ9), mk(10,D9), mk(8,MAJ9),               0.65f, 0.35f),
  mkP(mk(5,M9), mk(3,MAJ9), mk(8,MAJ9), mk(7,SUS9),              0.75f, 0.35f),
  mkP(mk(0,M11), mk(0,M9,10), mk(8,MAJ9), mk(5,M9),              0.80f, 0.40f),
  mkP(mk(8,MAJ9), mk(5,M9), mk(10,D9), mk(3,MAJ9),               0.60f, 0.30f),
  mkP(mk(0,M9), mk(5,M9), mk(8,MAJ7S11), mk(7,D9),               0.80f, 0.45f),

  // ===== РАСШИРЕНИЕ: Ethereal Drama (Kaleem Taylor, Nobu Woods, Che Ecru) =====
  mkP(mk(0,MAJ9), mk(10,MAJ7S11), mk(5,MAJ9), mk(9,M9),          0.30f, 0.55f),
  mkP(mk(5,MAJ7S11), mk(10,MAJ7S11), mk(0,MAJ9), mk(9,M9),       0.30f, 0.60f),
  mkP(mk(0,MAJ9), mk(4,D7B9), mk(9,M11), mk(5,MAJ7S11),          0.30f, 0.65f),
  mkP(mk(2,M9), mk(8,MAJ7S11), mk(0,MAJ9), mk(5,MAJ7S11),        0.30f, 0.60f),
  mkP(mk(0,SIX9), mk(3,MAJ7S11), mk(5,MAJ9), mk(7,D13),          0.35f, 0.60f),
  mkP(mk(9,M11), mk(5,MAJ7S11), mk(0,MAJ9), mk(7,D13),           0.35f, 0.55f),
  mkP(mk(0,MAJ7S11), mk(6,M7B5), mk(5,MAJ9), mk(4,M9),           0.30f, 0.65f),
  mkP(mk(0,MAJ9), mk(2,D13), mk(5,MAJ7S11), mk(7,SUS9),          0.25f, 0.65f),

  // ===== РАСШИРЕНИЕ: Toxic TrapSoul (PARTYNEXTDOOR, Drake, Artie J) =====
  mkP(mk(0,M9), mk(1,MAJ7S11), mk(8,MAJ9), mk(7,D7B9),           0.90f, 0.80f),
  mkP(mk(0,MMAJ7), mk(5,M11), mk(1,MAJ9), mk(7,D7B9),            0.95f, 0.85f),
  mkP(mk(0,M11), mk(10,D13), mk(8,MAJ7S11), mk(7,D7B9),          0.85f, 0.80f),
  mkP(mk(8,MAJ7S11), mk(7,D7B9), mk(0,MMAJ7), mk(5,M9),          0.90f, 0.80f),
  mkP(mk(0,M9), mk(3,MAJ7S11), mk(2,M7B5), mk(7,D7B9),           0.85f, 0.75f),
  mkP(mk(5,M11), mk(1,D9), mk(0,MMAJ7), mk(8,MAJ9),              0.85f, 0.85f),
  mkP(mk(0,MMAJ7), mk(0,M9,10), mk(8,MAJ7S11), mk(2,M7B5),       0.95f, 0.80f),
  mkP(mk(1,MAJ9), mk(0,M11), mk(8,MAJ7S11), mk(7,D13),           0.80f, 0.70f),

  // ===== TWIST-БАНК (mkT): 3-й аккорд = V, 4-й = обманный уход. Выбираются ТОЛЬКО при End: Twist =====
  // минор, тёмная/напряжённая зона
  mkT(mk(0,M11),   mk(5,M9),       mk(7,D7B9), mk(8,MAJ9),       0.85f, 0.75f),  // i - iv - V7b9 - bVI
  mkT(mk(0,M9),    mk(8,MAJ7S11),  mk(7,D7B9), mk(1,MAJ9),       0.90f, 0.85f),  // i - bVI - V7b9 - bII (неаполитанский)
  mkT(mk(0,MMAJ7), mk(3,MAJ9),     mk(7,D7B9), mk(8,MAJ7S11),    0.90f, 0.80f),  // i - bIII - V7b9 - bVI#11
  mkT(mk(0,M11),   mk(2,M7B5),     mk(7,D7B9), mk(8,MAJ9),       0.85f, 0.80f),  // i - iiø - V7b9 - bVI
  mkT(mk(0,MMAJ7), mk(5,M9),       mk(7,D7B9), mk(1,MAJ9),       0.95f, 0.85f),  // i(maj7) - iv - V7b9 - bII
  // минор, средняя зона
  mkT(mk(0,M11),   mk(8,MAJ9),     mk(7,D9),   mk(5,M9),         0.75f, 0.50f),  // i - bVI - V9 - iv
  mkT(mk(0,M11),   mk(3,MAJ9),     mk(7,D9),   mk(8,MAJ7S11),    0.70f, 0.55f),  // i - bIII - V9 - bVI#11
  // мажор, светлая зона
  mkT(mk(0,MAJ9),  mk(2,M9),       mk(7,D13),  mk(9,M9),         0.30f, 0.40f),  // I - ii - V13 - vi
  mkT(mk(0,SIX9),  mk(5,MAJ9),     mk(7,D9),   mk(8,MAJ7S11),    0.30f, 0.55f),  // I6/9 - IV - V9 - bVI#11
  mkT(mk(0,MAJ9),  mk(9,M9),       mk(7,D9),   mk(5,M9),         0.30f, 0.40f),  // I - vi - V9 - iv
  mkT(mk(5,MAJ9),  mk(2,M9),       mk(7,D13),  mk(4,M9),         0.25f, 0.35f),  // IV - ii - V13 - iii
  mkT(mk(0,MAJ7S11), mk(4,M9),     mk(7,D13),  mk(3,MAJ9),       0.35f, 0.60f)   // I - iii - V13 - bIII
};

// Hit: обычный удар аккорда; sus>=0 — «слеза» (верхний голос сначала подвешен, потом опускается);
// topOnly — догоняющий верхний голос (разрешение «слезы»), остальные ноты не переигрываются
struct Hit { double start = 0, len = 0; int chord = 0; int sus = -1; double susLen = 0; bool topOnly = false; };

// ЧУВСТВО: набор «красок» сцены
enum { RES_HOME, RES_HANG, RES_TWIST };
struct Feel {
  float nost = 0.0f;   // горько-сладкий заимствованный аккорд (iv / bVI / дорийская IV)
  int   res  = RES_HOME; // как заканчивается круг: домой / повисает на sus / обманный каданс
  float air  = 0.0f;   // воздух в ритме: аккорд приходит чуть раньше + вдох в конце круга
  float tear = 0.0f;   // «слеза»: задержание в верхнем голосе
  float human = 0.5f;  // живость исполнения: микро-тайминг, динамика, дыхание
};
struct Scene { const char* name; float dk, tn; Feel f; bool strum; };

// 8 сцен-воспоминаний, по две на квадрант. У каждой свой набор красок.
inline const Scene SCENES[8] = {
  {"Last Summer Night",       0.20f, 0.20f, {0.65f, RES_HOME,  0.30f, 0.55f, 0.60f}, true },
  {"First Slow Dance",        0.15f, 0.10f, {0.10f, RES_HOME,  0.10f, 0.20f, 0.70f}, true },
  {"Empty Flat After Guests", 0.75f, 0.35f, {0.50f, RES_HANG,  0.60f, 0.60f, 0.80f}, true },
  {"Unsent Text",             0.80f, 0.45f, {0.35f, RES_HANG,  0.50f, 0.80f, 0.70f}, false},
  {"Taxi in the Rain",        0.30f, 0.60f, {0.40f, RES_HANG,  0.50f, 0.70f, 0.60f}, true },
  {"Weightless",              0.25f, 0.65f, {0.15f, RES_HANG,  0.40f, 0.40f, 0.50f}, true },
  {"Jealous at 3AM",          0.90f, 0.80f, {0.20f, RES_TWIST, 0.30f, 0.80f, 0.40f}, false},
  {"Cant Let Go",             0.85f, 0.85f, {0.50f, RES_TWIST, 0.50f, 0.60f, 0.50f}, false}
};

inline bool isMinorType(int ty) { return ty == M7 || ty == M9 || ty == M11 || ty == MMAJ7 || ty == M7B5; }
inline bool isDomType(int ty) { return ty == D9 || ty == D13 || ty == D7B9; }
struct PadPattern { int n; double s[4], l[4]; int cMap[4]; };

inline const PadPattern PATTERNS_SLOW[3] = {
  {2, {0.0, 8.0, 0, 0}, {8.0, 8.0, 0, 0}, {0, 2, 0, 0}},
  {3, {0.0, 8.0, 12.0, 0}, {8.0, 4.0, 4.0, 0}, {0, 1, 2, 0}},
  {2, {0.0, 12.0, 0, 0}, {12.0, 4.0, 0, 0}, {0, 3, 0, 0}}
};

inline const PadPattern PATTERNS_MID[3] = {
  {2, {0.0, 8.0, 0, 0}, {8.0, 8.0, 0, 0}, {0, 2, 0, 0}},
  {3, {0.0, 4.0, 8.0, 0}, {4.0, 4.0, 8.0, 0}, {0, 1, 2, 0}},
  {4, {0.0, 4.0, 8.0, 12.0}, {4.0, 4.0, 4.0, 4.0}, {0, 1, 2, 3}}
};

inline const PadPattern PATTERNS_FAST[3] = {
  {2, {0.0, 8.0, 0, 0}, {8.0, 8.0, 0, 0}, {0, 2, 0, 0}},
  {2, {0.0, 8.0, 0, 0}, {8.0, 8.0, 0, 0}, {0, 1, 0, 0}},
  {3, {0.0, 8.0, 12.0, 0}, {8.0, 4.0, 4.0, 0}, {0, 2, 3, 0}}
};


// ======================= ВКУС: обучение на понравившихся прогрессиях =======================
// Всё хранится в ОТНОСИТЕЛЬНЫХ ступенях (0 = тоника), поэтому тональность вкусу не мешает.
inline Ch normCh(Ch c) { if (c.b == c.r) c.b = -1; return c; }
inline int chKey(Ch c) { c = normCh(c); return c.r + 12 * c.t + 168 * (c.b < 0 ? 0 : c.b + 1); }
// класс аккорда: 0 мажор/add, 1 минор, 2 доминанта, 3 sus / полууменьшенный
inline int chCls(int t) {
  switch (t) {
    case MAJ7: case MAJ9: case MAJ7S11: case SIX9: case ADD9: return 0;
    case M7: case M9: case M11: case MMAJ7: return 1;
    case D9: case D13: case D7B9: return 2;
    default: return 3;
  }
}
inline uint64_t sigOf(const Ch* c) {
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < 4; ++i) { h ^= (uint64_t) (chKey(c[i]) + 1); h *= 1099511628211ULL; }
  return h;
}
inline bool sameProg(const Prog& a, const Prog& b) {
  for (int i = 0; i < 4; ++i) if (chKey(a.c[i]) != chKey(b.c[i])) return false;
  return true;
}
inline int sampleW(juce::Random& rng, const std::vector<double>& w) {
  double sum = 0; for (double x : w) sum += x;
  if (sum <= 0.0) return rng.nextInt((int) w.size());
  double r = rng.nextDouble() * sum; int last = 0;
  for (size_t i = 0; i < w.size(); ++i) { if (w[i] > 0.0) last = (int) i; r -= w[i]; if (r <= 0.0 && w[i] > 0.0) return (int) i; }
  return last;
}

struct Taste {
  struct Entry { Ch c[4]; float dk = 0.5f, tn = 0.5f, w = 1.0f; bool tw = false; int src = 0; Feel f; uint64_t sig = 0; };
  // src: 0 = сердечко, 1 = импорт MIDI (на будущее)
  std::vector<Entry> entries;
  std::vector<Prog> userProgs;   // понравившиеся прогрессии как записи пользовательского банка

  // --- всё ниже пересчитывается в rebuild() ---
  std::vector<Ch> vocab; std::unordered_map<int, int> vidx; int V = 0;
  std::vector<float> bTr, tTr, bFirst, tFirst, sDk, sTn;   // переходы банка / вкуса, средние метки аккордов
  float bdel[12][4][4], tdel[12][4][4], btp[14][14], ttp[14][14];
  float typeW[14], posW[4][14], degW[12], typeMax = 0, posMax[4], degMax = 0;
  double nEff = 0;

  Taste() { rebuild(); }

  float confidence() const { return (float) (1.0 - std::exp(-nEff / 4.0)); }
  bool canCross() const { return !entries.empty(); }

  int idOf(const Ch& c) const { auto it = vidx.find(chKey(c)); return it == vidx.end() ? -1 : it->second; }
  const Prog& progAt(size_t j) const { return j < BANK.size() ? BANK[j] : userProgs[j - BANK.size()]; }

  void rebuild() {
    vocab.clear(); vidx.clear();
    auto reg = [this](const Ch& c0) {
      const Ch c = normCh(c0); const int k = chKey(c);
      auto it = vidx.find(k); if (it != vidx.end()) return;
      vidx[k] = (int) vocab.size(); vocab.push_back(c);
    };
    for (const auto& p : BANK) for (int i = 0; i < 4; ++i) reg(p.c[i]);
    for (const auto& e : entries) for (int i = 0; i < 4; ++i) reg(e.c[i]);
    V = (int) vocab.size();
    const size_t VV = (size_t) V * (size_t) V;
    bTr.assign(VV, 0.f); tTr.assign(VV, 0.f); bFirst.assign((size_t) V, 0.f); tFirst.assign((size_t) V, 0.f);
    std::memset(bdel, 0, sizeof bdel); std::memset(tdel, 0, sizeof tdel);
    std::memset(btp, 0, sizeof btp); std::memset(ttp, 0, sizeof ttp);
    std::memset(typeW, 0, sizeof typeW); std::memset(posW, 0, sizeof posW); std::memset(degW, 0, sizeof degW);
    std::vector<double> sumDk((size_t) V, 0.0), sumTn((size_t) V, 0.0), sumN((size_t) V, 0.0);

    auto add = [&](const Ch* c, float dk, float tn, float w, bool taste) {
      int id[4]; for (int i = 0; i < 4; ++i) id[i] = vidx[chKey(c[i])];
      (taste ? tFirst : bFirst)[(size_t) id[0]] += w;
      auto& tr = taste ? tTr : bTr;
      float (&del)[12][4][4] = taste ? tdel : bdel;
      float (&tp)[14][14] = taste ? ttp : btp;
      for (int i = 0; i < 4; ++i) {
        const int ia = id[i], ib = id[(i + 1) % 4];
        const Ch& a = vocab[(size_t) ia]; const Ch& b = vocab[(size_t) ib];
        tr[(size_t) ia * (size_t) V + (size_t) ib] += w;
        del[(b.r - a.r + 12) % 12][chCls(a.t)][chCls(b.t)] += w;
        tp[a.t][b.t] += w;
        sumDk[(size_t) ia] += (double) dk * w; sumTn[(size_t) ia] += (double) tn * w; sumN[(size_t) ia] += w;
      }
    };
    for (const auto& p : BANK) add(p.c, p.dk, p.tn, 1.0f, false);
    nEff = 0; userProgs.clear();
    for (const auto& e : entries) {
      add(e.c, e.dk, e.tn, e.w, true);
      nEff += e.w;
      for (int i = 0; i < 4; ++i) {
        const Ch c = normCh(e.c[i]);
        typeW[c.t] += e.w; posW[i][c.t] += e.w; degW[c.r] += e.w;
      }
      Prog p; for (int i = 0; i < 4; ++i) p.c[i] = e.c[i];
      p.dk = e.dk; p.tn = e.tn; p.tw = e.tw; userProgs.push_back(p);
    }
    typeMax = 0; degMax = 0; for (int i = 0; i < 4; ++i) posMax[i] = 0;
    for (int t = 0; t < 14; ++t) { typeMax = std::max(typeMax, typeW[t]); for (int i = 0; i < 4; ++i) posMax[i] = std::max(posMax[i], posW[i][t]); }
    for (int r = 0; r < 12; ++r) degMax = std::max(degMax, degW[r]);
    sDk.assign((size_t) V, 0.5f); sTn.assign((size_t) V, 0.5f);
    for (int s = 0; s < V; ++s) if (sumN[(size_t) s] > 0.0) { sDk[(size_t) s] = (float) (sumDk[(size_t) s] / sumN[(size_t) s]); sTn[(size_t) s] = (float) (sumTn[(size_t) s] / sumN[(size_t) s]); }
  }

  bool addEntry(const Ch* c, float dk, float tn, const Feel& f, float w, int src, bool tw) {
    Entry e; for (int i = 0; i < 4; ++i) e.c[i] = normCh(c[i]);
    e.dk = dk; e.tn = tn; e.f = f; e.w = w; e.src = src; e.tw = tw; e.sig = sigOf(e.c);
    for (const auto& o : entries) if (o.sig == e.sig) return false;
    entries.push_back(e); rebuild(); return true;
  }
  bool removeBySig(uint64_t sig, int src) {
    for (size_t i = 0; i < entries.size(); ++i)
      if (entries[i].sig == sig && entries[i].src == src) { entries.erase(entries.begin() + (long) i); rebuild(); return true; }
    return false;
  }
  void clear() { entries.clear(); rebuild(); }

  // ---------- оценки ----------
  // переход a -> b в духе вкуса: точный переход + интервал корней и классов + пара типов (с запасом на новые сочетания)
  float tasteTrans(const Ch& a, const Ch& b) const {
    const int ia = idOf(a), ib = idOf(b);
    const float ex = (ia >= 0 && ib >= 0) ? tTr[(size_t) ia * (size_t) V + (size_t) ib] : 0.f;
    const float dl = tdel[(b.r - a.r + 12) % 12][chCls(a.t)][chCls(b.t)];
    const float tp = ttp[a.t][b.t];
    return 0.55f * (ex / (ex + 1.f)) + 0.30f * (dl / (dl + 2.f)) + 0.15f * (tp / (tp + 2.f));
  }
  // «проверенность» перехода по банку: насколько он родной для движка
  float bankFlow(const Ch& a, const Ch& b) const {
    const int ia = idOf(a), ib = idOf(b);
    const float ex = (ia >= 0 && ib >= 0) ? bTr[(size_t) ia * (size_t) V + (size_t) ib] : 0.f;
    const float dl = bdel[(b.r - a.r + 12) % 12][chCls(a.t)][chCls(b.t)];
    const float tp = btp[a.t][b.t];
    return 0.5f * (ex / (ex + 0.7f)) + 0.3f * (dl / (dl + 3.f)) + 0.2f * (tp / (tp + 4.f));
  }
  float chordAff(const Ch& c0, int pos) const {
    const Ch c = normCh(c0);
    const float ta = typeMax > 0 ? typeW[c.t] / typeMax : 0.f;
    const float pa = posMax[pos] > 0 ? posW[pos][c.t] / posMax[pos] : 0.f;
    const float da = degMax > 0 ? degW[c.r] / degMax : 0.f;
    return 0.4f * ta + 0.3f * pa + 0.3f * da;
  }
  static float kern(float dx, float dy, float sg) { return std::exp(-(dx * dx + dy * dy) / (2.f * sg * sg)); }
  // 0..1: насколько прогрессия (4 аккорда, относительные ступени) похожа на вкус
  float score(const Ch* c, float dk, float tn, bool usePad) const {
    if (entries.empty()) return 0.f;
    float A = 0, T = 0;
    for (int i = 0; i < 4; ++i) { A += chordAff(c[i], i); T += tasteTrans(normCh(c[i]), normCh(c[(i + 1) % 4])); }
    A /= 4.f; T /= 4.f;
    if (!usePad) return (0.30f * A + 0.50f * T) / 0.80f;
    float P = 0; for (const auto& e : entries) P = std::max(P, kern(e.dk - dk, e.tn - tn, 0.22f));
    return 0.30f * A + 0.50f * T + 0.20f * P;
  }
  float scoreProg(const Prog& p) const { return score(p.c, 0.f, 0.f, false); }
  // штраф за дословный повтор уже понравившегося (хотим «похоже, но новое»)
  float copyPenalty(const Ch* c) const {
    int best = 0;
    for (const auto& e : entries) {
      int same = 0; for (int i = 0; i < 4; ++i) if (chKey(c[i]) == chKey(e.c[i])) ++same;
      best = std::max(best, same);
    }
    return best >= 4 ? 14.f : (best == 3 ? 1.5f : 0.f);
  }
  // средняя «зона пэда» аккордов прогрессии (по меткам банка и вкуса)
  void estimatePad(const Ch* c, float& ex, float& ey) const {
    float sx = 0, sy = 0; int n = 0;
    for (int i = 0; i < 4; ++i) { const int id = idOf(c[i]); if (id >= 0) { sx += sDk[(size_t) id]; sy += sTn[(size_t) id]; ++n; } }
    if (n > 0) { ex = sx / (float) n; ey = sy / (float) n; }
  }
  // штраф за переходы, которых нет ни в банке, ни во вкусе
  float flowPenalty(const Ch* c) const {
    float s = 0;
    for (int i = 0; i < 4; ++i) {
      const Ch a = normCh(c[i]), b = normCh(c[(i + 1) % 4]);
      s += std::max(0.f, 0.45f - std::max(bankFlow(a, b), tasteTrans(a, b))); // ниже «обычной» связности банка — штраф
    }
    return 10.0f * s;
  }
  float padCompat(int s, float dk, float tn) const {
    return kern(sDk[(size_t) s] - dk, sTn[(size_t) s] - tn, 0.30f) + 0.05f;
  }

  // ---------- генерация ----------
  // Новая прогрессия «в стиле вкуса»: цепь Маркова. Основа — переходы банка (логика движка),
  // поверх них — переходы из понравившегося; зона пэда задаёт, какие аккорды уместны.
  Prog genMarkov(juce::Random& rng, float dk, float tn, float ta) const {
    std::vector<double> w((size_t) V);
    int id[4];
    for (int s = 0; s < V; ++s) w[(size_t) s] = ((double) bFirst[(size_t) s] + 6.0 * (double) ta * (double) tFirst[(size_t) s]) * (double) padCompat(s, dk, tn);
    id[0] = sampleW(rng, w);
    for (int i = 1; i < 4; ++i) {
      const Ch& a = vocab[(size_t) id[i - 1]];
      for (int s = 0; s < V; ++s) {
        const Ch& b = vocab[(size_t) s];
        const size_t k = (size_t) id[i - 1] * (size_t) V + (size_t) s;
        double v = (double) bTr[k] + 8.0 * (double) ta * (double) tTr[k];
        v += 0.015 * ((double) bankFlow(a, b) + (double) ta * (double) tasteTrans(a, b));
        v *= (double) padCompat(s, dk, tn);
        if (b.r == a.r && b.t == a.t) v *= 0.05;
        for (int j = 0; j < i; ++j) if (vocab[(size_t) id[j]].r == b.r && vocab[(size_t) id[j]].t == b.t) v *= 0.25;
        if (i == 3) v *= 0.2 + 0.8 * (double) bankFlow(b, vocab[(size_t) id[0]]);
        w[(size_t) s] = v;
      }
      id[i] = sampleW(rng, w);
    }
    Prog p; for (int i = 0; i < 4; ++i) p.c[i] = vocab[(size_t) id[i]];
    p.dk = dk; p.tn = tn; p.tw = false;
    return p;
  }

  // Скрещивание: начало от одной понравившейся, конец от другой прогрессии (из вкуса или из банка рядом с пэдом)
  Prog genCross(juce::Random& rng, float dk, float tn) const {
    std::vector<double> wa(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) wa[i] = (double) entries[i].w * (0.25 + (double) kern(entries[i].dk - dk, entries[i].tn - tn, 0.30f));
    const Entry& A = entries[(size_t) sampleW(rng, wa)];
    const size_t nb = BANK.size() + userProgs.size();
    std::vector<double> wb(nb);
    for (size_t j = 0; j < nb; ++j) {
      const Prog& p = progAt(j);
      wb[j] = p.tw ? 0.0 : ((double) kern(p.dk - dk, p.tn - tn, 0.25f) + 0.01) * (0.3 + 1.7 * (double) scoreProg(p));
    }
    const Prog& B = progAt((size_t) sampleW(rng, wb));
    const int cut = 1 + rng.nextInt(3);
    const bool aFirst = rng.nextFloat() < 0.5f;
    Prog p;
    for (int i = 0; i < 4; ++i) p.c[i] = (i < cut) == aFirst ? A.c[i] : B.c[i];
    p.dk = dk; p.tn = tn; p.tw = false;
    return p;
  }

  // Подбор аккорда «между соседями» (для Evolve): должен хорошо вести от prev и к next
  Ch sampleBridge(juce::Random& rng, const Ch& prev, const Ch& next, float dk, float tn, float ta, int avoidR1, int avoidR2) const {
    std::vector<double> w((size_t) V);
    for (int s = 0; s < V; ++s) {
      const Ch& v = vocab[(size_t) s];
      double x = ((double) bankFlow(prev, v) + 0.05 + (double) ta * (double) tasteTrans(prev, v))
               * ((double) bankFlow(v, next) + 0.05 + (double) ta * (double) tasteTrans(v, next))
               * (double) padCompat(s, dk, tn);
      if ((v.r == prev.r && v.t == prev.t) || (v.r == next.r && v.t == next.t)) x *= 0.05;
      if (v.r == avoidR1 || v.r == avoidR2) x = 0.0;
      w[(size_t) s] = x;
    }
    return vocab[(size_t) sampleW(rng, w)];
  }
};

struct ChordGen {
  int root[4] = {0,0,0,0}, type[4] = {MAJ9,MAJ9,MAJ9,MAJ9}, bass[4] = {0,0,0,0};
  std::vector<int> voiced[4];
  std::vector<Hit> hits;
  Prog base{};
  int baseIdx = -1;
  int topLine[4] = {72,72,72,72};
  float topCenter = 72.0f, tenseV = 0.4f;
  juce::String names;
  Feel feel;
  bool tearAt[4] = {false, false, false, false};
  int keyUsed = 0;                       // тональность, в которой сейчас лежат root[] (для перевода в ступени)
  float padDk = 0.5f, padTn = 0.5f;      // точка пэда, при которой родилась прогрессия

  // аккорд i в относительных ступенях (0 = тоника)
  Ch relCh(int i) const {
    Ch c; c.r = ((root[i] - keyUsed) % 12 + 12) % 12; c.t = type[i];
    c.b = (bass[i] == root[i]) ? -1 : ((bass[i] - keyUsed) % 12 + 12) % 12;
    return c;
  }
  uint64_t signature() const { Ch c[4]; for (int i = 0; i < 4; ++i) c[i] = relCh(i); return sigOf(c); }

  // поправка к оценке кандидата (меньше = лучше): бонус за вкус, штраф за дословный повтор,
  // а для новых (не из банка) — ещё и соответствие пэду, связность и отсутствие дублей
  double tasteDelta(const Taste* tz, float ta, float dk, float tn, bool synth) const {
    Ch c[4]; for (int i = 0; i < 4; ++i) c[i] = relCh(i);
    double d = -10.0 * (double) ta * (double) tz->score(c, dk, tn, true);
    d += (double) tz->copyPenalty(c);
    if (synth) {
      float ex = dk, ey = tn; tz->estimatePad(c, ex, ey);
      d += 3.0 * (1.0 - std::exp(-(double) ((ex - dk) * (ex - dk) + (ey - tn) * (ey - tn)) / (2.0 * 0.3 * 0.3)));
      d += (double) tz->flowPenalty(c);
      for (int i = 0; i < 4; ++i) if (c[i].r == c[(i + 1) % 4].r && c[i].t == c[(i + 1) % 4].t) d += 6.0;
    }
    return d;
  }

  static const Prog& progAt(int idx, const Taste* tz) {
    return (size_t) idx < BANK.size() ? BANK[(size_t) idx] : tz->userProgs[(size_t) idx - BANK.size()];
  }

  // ta > 0: к пулу добавляются понравившиеся прогрессии, а веса сдвигаются в сторону вкуса
  static int pickIdx(juce::Random& rng, float dk, float tn, float sigma = 0.25f, bool twist = false, const Taste* tz = nullptr, float ta = 0.0f) {
    const bool useT = tz != nullptr && ta > 0.0f && !tz->entries.empty();
    const size_t nb = BANK.size(), n = nb + (useT ? tz->userProgs.size() : 0);
    std::vector<double> w(n); double sum = 0;
    for (size_t i = 0; i < n; ++i) {
      const Prog& p = i < nb ? BANK[i] : tz->userProgs[i - nb];
      const double dx = p.dk - dk, dy = p.tn - tn;
      w[i] = std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma)) + 0.005;
      if (useT) { w[i] *= 1.0 + 2.5 * (double) ta * (double) tz->scoreProg(p); if (i >= nb) w[i] *= 0.6; }
      if (p.tw) w[i] = twist ? w[i] * 4.0 : 0.0; // twist-прогрессии не влияют на остальные режимы
      sum += w[i];
    }
    double r = rng.nextDouble() * sum;
    int last = 0;
    for (size_t i = 0; i < n; ++i) { if (w[i] > 0.0) last = (int) i; r -= w[i]; if (r <= 0.0 && w[i] > 0.0) return (int) i; }
    return last;
  }

  void makeRhythm(juce::Random& rng, double bpm = 120.0) {
    hits.clear();
    const PadPattern* selectedPool = PATTERNS_MID;
    if (bpm < 85.0) selectedPool = PATTERNS_SLOW;
    else if (bpm > 115.0) selectedPool = PATTERNS_FAST;

    const PadPattern& p = selectedPool[rng.nextInt(3)];
    for (int k = 0; k < p.n; ++k) hits.push_back(Hit{ p.s[k], p.l[k], p.cMap[k] % 4 });

    // броски всегда в одном порядке, чтобы слайдеры не перетасовывали прогрессию
    const float ra = rng.nextFloat(), rb = rng.nextFloat(), t1 = rng.nextFloat(), t2 = rng.nextFloat();
    const int ka = 1 + rng.nextInt((int) hits.size() - 1);

    // ВОЗДУХ: аккорд приходит на полдоли раньше такта (анти­ципация)
    if (ra < feel.air && hits[(size_t) ka - 1].len >= 3.0) {
      hits[(size_t) ka].start -= 0.5; hits[(size_t) ka].len += 0.5; hits[(size_t) ka - 1].len -= 0.5;
    }
    // ВОЗДУХ: вдох перед новым кругом
    if (rb < feel.air * 0.6f && hits.back().len >= 4.0) hits.back().len -= 1.0;

    // СЛЕЗА: где подвесить верхний голос (кульминация и приход домой)
    for (bool& b : tearAt) b = false;
    tearAt[2] = t1 < feel.tear;
    tearAt[0] = t2 < feel.tear * 0.5f;
    tearify();
  }

  // Строит «слёзы» детерминированно по tearAt и текущим голосам
  void tearify() {
    hits.erase(std::remove_if(hits.begin(), hits.end(), [](const Hit& h) { return h.topOnly; }), hits.end());
    std::vector<Hit> extra;
    for (auto& h : hits) {
      h.sus = -1; h.susLen = 0;
      if (h.chord < 0 || h.chord > 3 || !tearAt[h.chord] || h.len < 4.0) continue;
      const auto& v = voiced[h.chord];
      if (v.size() < 4) continue;
      const int top = v.back();
      int sus = -1;
      for (int add : {2, 1}) {
        const int cand = top + add; bool ok = cand <= 90;
        for (size_t i = 0; i + 1 < v.size(); ++i) {
          if (v[i] % 12 == cand % 12) ok = false;            // не удвоение аккордового тона
          if (i > 0 && std::abs(v[i] - cand) == 1) ok = false; // не злое трение с внутренними голосами
        }
        if (ok) { sus = cand; break; }
      }
      if (sus < 0) continue;
      h.sus = sus; h.susLen = h.len >= 8.0 ? 2.0 : 1.0;
      Hit e; e.start = h.start + h.susLen; e.len = h.len - h.susLen; e.chord = h.chord; e.topOnly = true;
      extra.push_back(e);
    }
    for (auto& e : extra) hits.push_back(e);
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.start < b.start; });
  }

  void apply(juce::Random& rng, int key, float tense) {
    tenseV = tense;
    const int effectiveKey = (key == 12) ? rng.nextInt(12) : key;
    keyUsed = effectiveKey;

    int r[4], t[4], b[4];
    for (int i = 0; i < 4; ++i) {
      r[i] = base.c[i].r; b[i] = base.c[i].b; t[i] = base.c[i].t;
      const float x = rng.nextFloat();
      if (t[i] == D7B9 && x < 0.4f * (1.0f - tense)) t[i] = D9;
      else if (t[i] == MAJ7S11 && x < 0.3f * (1.0f - tense)) t[i] = MAJ9;
      else if (t[i] == M11 && x < 0.3f * (1.0f - tense)) t[i] = M9;
    }
    bool minorKey = false;
    for (int i = 0; i < 4; ++i) if (r[i] == 0) { minorKey = isMinorType(t[i]); break; }
    const float rn = rng.nextFloat(), rp = rng.nextFloat(), rq = rng.nextFloat();

    // НОСТАЛЬГИЯ: заимствованный аккорд. В мажоре iv-минор или bVI, в миноре дорийская IV (F9 в Cm)
    int k = (r[2] != 7 && r[2] != 0) ? 2 : ((r[1] != 7 && r[1] != 0) ? 1 : -1);
    if (feel.res == RES_TWIST) k = (r[1] != 7 && r[1] != 0) ? 1 : -1; // 3-й аккорд в Twist всё равно станет V
    if (k > 0 && rn < feel.nost) {
      const int nr = minorKey ? 5 : (rp < 0.7f ? 5 : 8);
      const int nt = minorKey ? D9 : (rp < 0.7f ? M9 : MAJ9);
      bool clash = false; // в Twist не заимствуем корень, который уже есть в круге
      if (feel.res == RES_TWIST) for (int i = 0; i < 4; ++i) if (i != k && r[i] == nr) clash = true;
      if (!clash) { b[k] = -1; r[k] = nr; t[k] = nt; }
    }

    // КОНЦОВКА КРУГА: домой / повиснуть / обмануть
    if (feel.res == RES_HANG) {
      if (r[3] == 7 || isDomType(t[3])) t[3] = SUS9;
    } else if (feel.res == RES_HOME) {
      if (t[3] == SUS9) t[3] = tense > 0.6f ? D7B9 : D9;
    } else if (!base.tw) {
      // обманный каданс «по-мягкому»: V на 3-й позиции + сюрприз-аккорд, не повторяющий корни 1-2 аккордов
      r[2] = 7; b[2] = -1; t[2] = tense > 0.6f ? D7B9 : D9;
      static const int minR[4] = {8, 1, 8, 5}, minT[4] = {MAJ9, MAJ9, MAJ7S11, M9};   // bVI, bII, bVI#11, iv
      static const int majR[4] = {9, 8, 3, 5}, majT[4] = {M9, MAJ7S11, MAJ9, M9};     // vi, bVI#11, bIII, iv
      const int s0 = rq < 0.45f ? 0 : std::min(3, 1 + (int) ((rq - 0.45f) / 0.55f * 3.0f));
      for (int j = 0; j < 4; ++j) {
        const int c = (s0 + j) % 4, cr = minorKey ? minR[c] : majR[c];
        if (cr == r[0] || cr == r[1]) continue;
        r[3] = cr; b[3] = -1; t[3] = minorKey ? minT[c] : majT[c];
        break;
      }
    }

    for (int i = 0; i < 4; ++i) {
      root[i] = (r[i] + effectiveKey) % 12;
      bass[i] = ((b[i] < 0 ? r[i] : b[i]) + effectiveKey) % 12;
      type[i] = t[i];
    }
  }

  void planTop(juce::Random& rng) {
    std::vector<int> cands[4];
    for (int i = 0; i < 4; ++i) {
      const auto& iv = IV[type[i]];
      std::vector<int> pcs;
      for (size_t k = 1; k < iv.size(); ++k) {
        if (iv[k] % 12 == 7 && iv.size() >= 5) continue;
        if (iv[k] % 12 == 1) continue;
        pcs.push_back((root[i] + iv[k]) % 12);
      }
      for (int m = (int) topCenter - 4; m <= (int) topCenter + 4; ++m)
        for (int pc : pcs) if (m % 12 == pc) cands[i].push_back(m);
      if (cands[i].empty())
        for (int m = 64; m <= 76; ++m) for (int pc : pcs) if (m % 12 == pc) cands[i].push_back(m);
      if (cands[i].empty()) cands[i].push_back(72);
    }
    double bestS = 1e18; int bv[4] = {72, 72, 72, 72};
    for (int a : cands[0]) for (int b : cands[1]) for (int c : cands[2]) for (int d : cands[3]) {
      const int v[4] = {a, b, c, d};
      double s = rng.nextFloat();
      // певучесть: шаги по 1-2 полутона лучше всего, повтор ноты — мотив, скачки дороги
      for (int i = 0; i < 4; ++i) {
        const int leap = std::abs(v[i] - v[(i + 1) % 4]);
        if (leap == 0) s += 0.8;
        else if (leap <= 2) s += 0.4;
        else if (leap <= 4) s += 2.0 + (leap - 2) * 0.5;
        else s += 4.0 + (leap - 4) * 2.5;
      }
      const int mx = std::max(std::max(v[0], v[1]), std::max(v[2], v[3]));
      if (v[2] == mx && v[0] < mx && v[1] < mx && v[3] < mx) s -= 3.0; // единственная кульминация на 3-м аккорде
      else if (v[3] == mx) s += 2.0;
      if (v[3] < v[2]) s -= 1.0;   // спуск к разрешению
      if (v[0] == v[2]) s -= 1.5;  // повтор ноты-хука
      s += std::abs((a + b + c + d) / 4.0 - topCenter) * 0.3;
      if (s < bestS) { bestS = s; for (int i = 0; i < 4; ++i) bv[i] = v[i]; }
    }
    for (int i = 0; i < 4; ++i) topLine[i] = bv[i];
  }

  double quality() const {
    const double ideal[4] = {0.25, 0.5, 1.0, feel.res == RES_TWIST ? 0.3 : 0.5}; // в Twist финал мягче: это «приземление» после V
    double q = 0;
    for (int i = 0; i < 4; ++i) {
      const auto& a = voiced[i]; const auto& b = voiced[(i + 1) % 4];
      if (a.size() < 2 || b.size() < 2) return 1e9;
      const int bl = std::abs(a[0] - b[0]); if (bl > 5) q += (bl - 5) * 3;
      const int tl = std::abs(a.back() - b.back()); if (tl > 4) q += (tl - 4) * 2;
      q += 3.0 * std::abs(tensOf(type[i]) - ideal[i] * (0.4 + 0.6 * (double) tenseV));
    }
    return q;
  }

  void generate(juce::Random& rng, int key, float dark, float tense, bool open, double bpm = 120.0, bool differ = false,
                const Taste* tz = nullptr, float tasteAmt = 0.0f) {
    const bool twist = feel.res == RES_TWIST;
    const float ta = (tz != nullptr && !tz->entries.empty()) ? tasteAmt * tz->confidence() : 0.0f;
    const bool useT = ta > 0.03f;
    const Prog prev = base;
    ChordGen best; double bq = 1e18;
    const int attempts = useT ? 40 : 30;
    for (int a = 0; a < attempts; ++a) {
      ChordGen t; t.feel = feel;
      const float srcR = rng.nextFloat(), srcR2 = rng.nextFloat();
      int src = 0;
      if (useT && srcR < 0.10f + 0.65f * ta) {
        // новая прогрессия «в моём вкусе»: цепь Маркова или скрещивание понравившихся
        if (srcR2 < 0.55f || !tz->canCross()) { t.base = tz->genMarkov(rng, dark, tense, ta); t.baseIdx = -2; src = 1; }
        else { t.base = tz->genCross(rng, dark, tense); t.baseIdx = -3; src = 2; }
      } else {
        t.baseIdx = pickIdx(rng, dark, tense, 0.25f, twist, tz, useT ? ta : 0.0f);
        t.base = progAt(t.baseIdx, tz);
      }
      t.padDk = dark; t.padTn = tense;
      t.topCenter = 74.0f - 4.0f * dark;
      t.apply(rng, key, tense); t.revoice(open, rng, true); t.makeRhythm(rng, bpm);
      double q = t.quality();
      if (differ && sameProg(t.base, prev)) q += 25;
      if (useT) q += t.tasteDelta(tz, ta, dark, tense, src != 0);
      if (q < bq) { bq = q; best = t; }
    }
    *this = best;
  }

  void evolve(juce::Random& rng, int key, float tense, bool open, double bpm = 120.0, const Taste* tz = nullptr, float tasteAmt = 0.0f) {
    const bool twist = feel.res == RES_TWIST;
    const float ta = (tz != nullptr && !tz->entries.empty()) ? tasteAmt * tz->confidence() : 0.0f;
    const bool useT = ta > 0.03f;
    ChordGen best; double bq = 1e18; bool found = false;
    for (int a = 0; a < 12; ++a) {
      Prog np = base; bool changed = false;
      const int changes = 1;
      for (int m = 0; m < changes; ++m) {
        int k = 1 + rng.nextInt(3);
        if (base.tw || twist) k = 1; // 3-й и 4-й аккорды Twist-финала не трогаем
        Ch nc;
        if (useT && rng.nextFloat() < 0.30f + 0.50f * ta)
          nc = tz->sampleBridge(rng, np.c[k - 1], np.c[(k + 1) % 4], base.dk, base.tn, ta, base.tw ? np.c[2].r : -1, base.tw ? np.c[3].r : -1);
        else nc = progAt(pickIdx(rng, base.dk, base.tn, 0.2f, twist, tz, useT ? ta : 0.0f), tz).c[k];
        const Ch old = np.c[k];
        if (nc.r == old.r && nc.t == old.t && nc.b == old.b) continue;
        if (base.tw && (nc.r == np.c[2].r || nc.r == np.c[3].r)) continue;
        np.c[k] = nc; changed = true;
      }
      if (!changed) continue;
      ChordGen t = *this; t.base = np;
      t.apply(rng, key, tense); t.revoice(open, rng, true); t.makeRhythm(rng, bpm);
      double q = t.quality();
      if (useT) q += t.tasteDelta(tz, ta, padDk, padTn, false);
      if (q < bq) { bq = q; best = t; found = true; }
    }
    if (found) *this = best;
  }

  void transpose(int d, bool open, juce::Random& rng) {
    keyUsed = ((keyUsed + d) % 12 + 12) % 12;
    for (int i = 0; i < 4; ++i) {
      root[i] = (root[i] + d) % 12; bass[i] = (bass[i] + d) % 12;
      topLine[i] += d; if (topLine[i] > 77) topLine[i] -= 12;
    }
    revoice(open, rng);
  }

  static std::vector<int> pick(int rt, int t, const std::vector<int>& prev, juce::Random& rng, int top = -1) {
    const auto& iv = IV[t]; std::vector<int> pcs;
    for (size_t i = 1; i < iv.size(); ++i) { if (iv[i] % 12 == 7 && iv.size() >= 5) continue; pcs.push_back((rt + iv[i]) % 12); }
    std::vector<std::vector<int>> cand;
    for (int pc : pcs) { std::vector<int> a; for (int m = 59; m <= 78; ++m) if (m % 12 == pc) a.push_back(m); cand.push_back(a); }
    std::vector<int> best, cur; double bestCost = 1e9;
    std::function<void(size_t)> rec = [&](size_t k) {
      if (k == cand.size()) {
        auto s = cur; std::sort(s.begin(), s.end());
        double mean = 0; for (int v : s) mean += v; mean /= (double) s.size();
        double cost = rng.nextFloat() * 0.5;
        if (s.back() - s.front() > 14) cost += 100;
        if (top >= 0 && s.back() != top) cost += 400;
        for (size_t i = 0; i + 1 < s.size(); ++i) {
          const int gap = s[i + 1] - s[i];
          if (gap < 3 && s[i] < 66) cost += 25; else if (gap == 1) cost += 2;
        }
        if (!prev.empty()) {
          double pm = 0; for (int v : prev) pm += v; pm /= (double) prev.size();
          for (int v : s) { int d = 99; for (int q : prev) d = std::min(d, std::abs(v - q)); cost += d; }
          cost += std::abs(mean - pm) * 0.5;
        } else cost += std::abs(mean - 68) * 1.5;
        if (cost < bestCost) { bestCost = cost; best = s; }
        return;
      }
      for (int n : cand[k]) { cur.push_back(n); rec(k + 1); cur.pop_back(); }
    };
    rec(0);
    return best;
  }

  void revoice(bool open, juce::Random& rng, bool replan = false) {
    if (replan) planTop(rng);
    std::vector<int> prev; juce::StringArray nm; int prevBass = -1;
    for (int i = 0; i < 4; ++i) {
      auto up = pick(root[i], type[i], prev, rng, topLine[i]); prev = up;
      int bassNote = 31 + (((bass[i] - 31) % 12) + 12) % 12;
      if (prevBass >= 0) {
        int bestD = 99;
        for (int m = 31; m <= 43; ++m)
          if (m % 12 == bass[i] && std::abs(m - prevBass) < bestD) { bestD = std::abs(m - prevBass); bassNote = m; }
      }
      prevBass = bassNote;
      if (open && up.size() >= 4) {
        const int dn = up[up.size() - 2] - 12;
        if (dn >= 55) up[up.size() - 2] = dn;
      }
      std::sort(up.begin(), up.end());
      voiced[i].clear(); voiced[i].push_back(bassNote);
      for (int v : up) voiced[i].push_back(v);
      juce::String n = juce::String(NOTE_NAMES[root[i]]) + SUF[type[i]];
      if (bass[i] != root[i]) n += juce::String("/") + NOTE_NAMES[bass[i]];
      nm.add(n);
    }
    names = nm.joinIntoString("   |   ");
    tearify();
  }
};
