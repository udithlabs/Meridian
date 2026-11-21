// Public holidays of Sri Lanka, the United Kingdom and the United States. Sri Lanka: 2026 follows the Government's published calendar and
// 2027 the gazetted list; other years are estimated (fixed dates, Good Friday, and Poya days from the Moon). UK and US: worked out from the
// bank-holiday and federal-holiday rules for any year (the 2026-2028 tables in the reference sheet are the test vectors). See holidays.h.
#include "holidays.h"
#include "astro.h"
#include "util.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

using util::fmt;

namespace holiday {

namespace {

struct Row { int m, d, id; bool adhi; };

const Row Y2026[] = {
    {1, 3, H_DURUTHU, false}, {1, 15, H_PONGAL, false}, {2, 1, H_NAVAM, false}, {2, 4, H_INDEPENDENCE, false},
    {2, 15, H_SIVARATHRI, false}, {3, 2, H_MEDIN, false}, {3, 21, H_EID_FITR, false}, {4, 1, H_BAK, false},
    {4, 3, H_GOOD_FRIDAY, false}, {4, 13, H_NY_EVE, false}, {4, 14, H_NY_DAY, false}, {5, 1, H_VESAK, false},
    {5, 1, H_MAY_DAY, false}, {5, 2, H_VESAK_AFTER, false}, {5, 28, H_EID_ADHA, false}, {5, 30, H_POSON, true},
    {6, 29, H_POSON, false}, {7, 29, H_ESALA, false}, {8, 26, H_MILAD, false}, {8, 27, H_NIKINI, false},
    {9, 26, H_BINARA, false}, {10, 25, H_VAP, false}, {11, 8, H_DEEPAVALI, false}, {11, 24, H_IL, false},
    {12, 23, H_UNDUVAP, false}, {12, 25, H_CHRISTMAS, false},
};

const Row Y2027[] = {
    {1, 15, H_PONGAL, false}, {1, 22, H_DURUTHU, false}, {2, 4, H_INDEPENDENCE, false}, {2, 20, H_NAVAM, false},
    {3, 6, H_SIVARATHRI, false}, {3, 10, H_EID_FITR, false}, {3, 22, H_MEDIN, false}, {3, 26, H_GOOD_FRIDAY, false},
    {4, 13, H_NY_EVE, false}, {4, 14, H_NY_DAY, false}, {4, 20, H_BAK, false}, {5, 1, H_MAY_DAY, false},
    {5, 17, H_EID_ADHA, false}, {5, 19, H_VESAK, false}, {5, 20, H_VESAK_AFTER, false}, {6, 18, H_POSON, false},
    {7, 18, H_ESALA, false}, {8, 15, H_MILAD, false}, {8, 16, H_NIKINI, false}, {9, 15, H_BINARA, false},
    {10, 15, H_VAP, false}, {10, 28, H_DEEPAVALI, false}, {11, 13, H_IL, false}, {12, 13, H_UNDUVAP, false},
    {12, 25, H_CHRISTMAS, false},
};

// Gregorian Easter Sunday (anonymous algorithm)
void easter(int Y, int& m, int& d) {
    int a = Y % 19, b = Y / 100, c = Y % 100, dd = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - dd - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    int mm = (a + 11 * h + 22 * l) / 451;
    m = (h + l - 7 * mm + 114) / 31; d = (h + l - 7 * mm + 114) % 31 + 1;
}

void addDays(int& y, int& m, int& d, int n) {
    long long z = (long long)std::llround(astro::daysFromCivil(y, m, d)) + n;
    astro::civilFromDays(z, y, m, d);
}

double elongationAt(double t) {
    astro::Geo g;
    return astro::norm360(astro::moonInfo(t, g).eclLon - astro::sunInfo(t, g).eclLon);
}

// The Poya day is the day the full-moon tithi (the Moon 12 degrees short of full) begins, unless that happens
// after 6 PM, when the next day counts. This reproduces all 25 dates in the official 2026 and 2027 lists.
void poyaDate(double fullMoon, int& y, int& m, int& d) {
    double lo = fullMoon - 34 * 3600.0, hi = fullMoon;
    for (int i = 0; i < 50; ++i) { double mid = 0.5 * (lo + hi); if (elongationAt(mid) < 168.0 && elongationAt(mid) > 90.0) lo = mid; else hi = mid; }
    util::LocalTime L = util::toLocal(hi, 19800);
    y = L.y; m = L.m; d = L.d;
    if (L.hh >= 18) addDays(y, m, d, 1);
}

std::vector<Holiday> estimate(int Y) {
    std::vector<Holiday> v;
    auto add = [&](int m, int d, int id, bool est, bool adhi = false) { Holiday h; h.y = Y; h.m = m; h.d = d; h.id = id; h.estimated = est; h.adhi = adhi; v.push_back(h); };
    add(1, 15, H_PONGAL, true);
    add(2, 4, H_INDEPENDENCE, false);
    add(4, 13, H_NY_EVE, false);
    add(4, 14, H_NY_DAY, false);
    add(5, 1, H_MAY_DAY, false);
    add(12, 25, H_CHRISTMAS, false);
    { int m, d; easter(Y, m, d); int yy = Y; addDays(yy, m, d, -2); add(m, d, H_GOOD_FRIDAY, true); }
    // Poya days. The name follows the Sun's sign at the new moon that starts the lunar month (Aries = Vesak ...);
    // when two new moons fall in the same sign, the earlier month is the extra (Adhi) one.
    static const int SIGN_ID[12] = {H_VESAK, H_POSON, H_ESALA, H_NIKINI, H_BINARA, H_VAP, H_IL, H_UNDUVAP, H_DURUTHU, H_NAVAM, H_MEDIN, H_BAK};
    auto sign = [](double t) {
        astro::Geo g;
        double yr = 2000.0 + (t - 946728000.0) / (365.2425 * 86400.0);
        double sid = astro::norm360(astro::sunInfo(t, g).eclLon - (23.857 + 0.013969 * (yr - 2000.0)));   // Lahiri ayanamsa
        return (int)(sid / 30.0) % 12;
    };
    std::vector<Holiday> poyas;
    for (double t = astro::nextPhaseTime(astro::daysFromCivil(Y, 1, 1) * 86400.0 - 3 * 86400.0, 2); ; t = astro::nextPhaseTime(t + 5 * 86400.0, 2)) {
        int y, m, d; poyaDate(t, y, m, d);
        if (y > Y) break;
        if (y < Y) continue;
        int s0 = sign(astro::prevPhaseTime(t, 0)), s1 = sign(astro::nextPhaseTime(t, 0));
        Holiday h; h.y = y; h.m = m; h.d = d; h.id = SIGN_ID[s0]; h.adhi = s0 == s1; h.estimated = true;
        poyas.push_back(h);
    }
    for (auto& h : poyas) {
        v.push_back(h);
        if (h.id == H_VESAK && !h.adhi) { Holiday a = h; a.id = H_VESAK_AFTER; int yy = a.y, mm = a.m, dd = a.d; addDays(yy, mm, dd, 1); a.y = yy; a.m = mm; a.d = dd; v.push_back(a); }
    }
    return v;
}

struct Entry { const char* name; const char* shortName; const char* tag; int group; bool merc; };
const Entry ENT[H_LK_COUNT] = {
    {"Duruthu Full Moon Poya Day", "Duruthu Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Navam Full Moon Poya Day", "Navam Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Medin Full Moon Poya Day", "Medin Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Bak Full Moon Poya Day", "Bak Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Vesak Full Moon Poya Day", "Vesak Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Poson Full Moon Poya Day", "Poson Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Esala Full Moon Poya Day", "Esala Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Nikini Full Moon Poya Day", "Nikini Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Binara Full Moon Poya Day", "Binara Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Vap Full Moon Poya Day", "Vap Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Il Full Moon Poya Day", "Il Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Unduvap Full Moon Poya Day", "Unduvap Poya", "BUDDHIST \xC2\xB7 POYA", G_BUDDHIST, false},
    {"Day after Vesak Full Moon Poya Day", "Day after Vesak", "BUDDHIST", G_BUDDHIST, true},
    {"Tamil Thai Pongal Day", "Thai Pongal", "HINDU \xC2\xB7 TAMIL", G_HINDU, true},
    {"Independence Day", "Independence Day", "NATIONAL DAY", G_NATIONAL, true},
    {"Maha Sivarathri Day", "Maha Sivarathri", "HINDU", G_HINDU, false},
    {"Eid al-Fitr (Ramazan Festival Day)", "Eid al-Fitr", "MUSLIM", G_MUSLIM, false},
    {"Good Friday", "Good Friday", "CHRISTIAN", G_CHRISTIAN, false},
    {"Day before Sinhala and Tamil New Year", "Day before New Year", "SINHALA & TAMIL", G_NEWYEAR, true},
    {"Sinhala and Tamil New Year Day", "Sinhala & Tamil New Year", "SINHALA & TAMIL", G_NEWYEAR, true},
    {"International Labour Day (May Day)", "May Day", "LABOUR DAY", G_LABOUR, true},
    {"Eid al-Adha (Hajj Festival Day)", "Eid al-Adha", "MUSLIM", G_MUSLIM, false},
    {"Milad-un-Nabi (Prophet's Birthday)", "Milad-un-Nabi", "MUSLIM", G_MUSLIM, true},
    {"Deepavali Festival Day", "Deepavali", "HINDU", G_HINDU, false},
    {"Christmas Day", "Christmas Day", "CHRISTIAN", G_CHRISTIAN, true},
};

const char* WHAT[H_LK_COUNT] = {
    // Duruthu
    "Duruthu Poya, the January full moon, marks the Buddha's first visit to Sri Lanka, in the ninth month after his Enlightenment, when tradition says he travelled to Mahiyangana. The Kelaniya Duruthu Perahera, near Colombo, is held on this day.",
    // Navam
    "Navam Poya, the February full moon, marks the Buddha's appointment of Arahants Sariputta and Moggallana as his two chief disciples. Colombo's Gangaramaya Temple holds the Navam Perahera, a famous procession of elephants, dancers and drummers.",
    // Medin
    "Medin Poya, the March full moon, remembers the Buddha's journey to his home city Kapilavastu in the seventh year after his Enlightenment, where he met his father King Suddhodana and his son Rahula, who became a novice monk.",
    // Bak
    "Bak Poya, the April full moon, marks the Buddha's return visit to Sri Lanka in the fifth year after his Enlightenment, when tradition says he went to Nagadipa to settle a quarrel between two Naga kings.",
    // Vesak
    "Vesak is the holiest day in the Buddhist year. This full moon marks three events in the life of the Buddha: his birth, his Enlightenment and his passing away (Parinibbana), all remembered on a single day.",
    // Poson
    "Poson Poya, the June full moon, celebrates the arrival of Buddhism in Sri Lanka in the 3rd century BCE, when Arahant Mahinda, son of Emperor Ashoka, preached to King Devanampiyatissa at Mihintale. Mihintale and Anuradhapura host the biggest celebrations.",
    // Esala
    "Esala Poya marks the Buddha's first sermon, the Dhammacakkappavattana Sutta, preached to five ascetics at Isipatana. The monks' three-month rains retreat (Vas) begins around now, and Kandy's Esala Perahera, honouring the Sacred Tooth Relic, peaks around this full moon.",
    // Nikini
    "Nikini Poya, the August full moon, marks the first Buddhist council (Sangayana), held after the Buddha's passing, where his teachings were recited and agreed by the monks.",
    // Binara
    "Binara Poya, the September full moon, marks the founding of the Bhikkhuni Sangha, the Buddhist order of nuns, when women were first allowed to be ordained.",
    // Vap
    "Vap Poya, the October full moon, marks the end of the monks' three-month rains retreat (Vas). The Kathina season, when lay people offer new robes to the monks, follows it.",
    // Il
    "Il Poya, the November full moon, is the day Buddhists remember Bodhisattva Maitreya, the Buddha of the future, receiving the assurance that he would become a Buddha. Kathina robe offerings continue around this time.",
    // Unduvap
    "Unduvap Poya, the December full moon, marks the arrival of Theri Sanghamitta, Mahinda's sister, from India in the 3rd century BCE with a sapling of the Bodhi tree under which the Buddha was enlightened. It was planted at Anuradhapura, where the Jaya Sri Maha Bodhi still grows.",
    // Day after Vesak
    "The day after Vesak Poya is also a public holiday, so the Vesak celebrations run over two days.",
    // Pongal
    "Thai Pongal is the Tamil harvest festival, held as the Sun begins its northward journey at the start of the Tamil month of Thai. It is a thanksgiving to the Sun god Surya, the earth, and the cattle that help bring in the harvest.",
    // Independence
    "On 4 February 1948 Ceylon became independent after more than a century of British rule; it became the Republic of Sri Lanka in 1972.",
    // Sivarathri
    "Maha Sivarathri, the Great Night of Shiva, is a Hindu festival honouring Lord Shiva, held on the 14th night of the dark half of the lunar month of Masi (February or March).",
    // Eid al-Fitr
    "Eid al-Fitr, called Ramazan Festival Day in Sri Lanka, ends Ramadan, the Muslim month of fasting from dawn to sunset. The date depends on the sighting of the new moon, so it can shift by a day close to the time.",
    // Good Friday
    "Good Friday is the Christian day that remembers the crucifixion and death of Jesus. It falls on the Friday before Easter Sunday.",
    // New Year's eve
    "The last day of the old year in the traditional Sinhala and Tamil calendar, a time of rest and religious observance before the Sun moves from Pisces into Aries and the new year begins. Astrologers fix every time and ritual.",
    // New Year
    "Aluth Avurudda (Sinhala) and Puthandu (Tamil), the traditional new year, begins when the Sun moves from Pisces into Aries. It is a harvest and family festival that Sinhalese and Tamil communities celebrate together, with astrologers setting the time for every ritual.",
    // May Day
    "International Workers' Day honours the labour movement and workers' rights. It has been marked in Sri Lanka since the 1930s.",
    // Eid al-Adha
    "Eid al-Adha, called Hajj Festival Day in Sri Lanka, is the Muslim Festival of Sacrifice. It remembers the Prophet Ibrahim's willingness to sacrifice his son in obedience to God, falls on the 10th of Dhul Hijjah, and comes at the end of the Hajj pilgrimage.",
    // Milad-un-Nabi
    "Milad-un-Nabi marks the birthday of the Prophet Muhammad, observed on 12 Rabi' al-Awwal. It is a day of remembrance, with recitals of the Prophet's life and praise poems (mawlid).",
    // Deepavali
    "Deepavali, the Festival of Lights, celebrates the victory of light over darkness and good over evil. Tamil Hindus in Sri Lanka link it to Lord Krishna's defeat of the demon Narakasura.",
    // Christmas
    "Christmas Day is the Christian celebration of the birth of Jesus, observed by Christians of every denomination across the island.",
};

const char* POYA_HOW = " Devout Buddhists visit temples dressed in white, keep the Eight Precepts, and offer flowers and oil lamps. By law no alcohol or meat is sold, so bars and butcher shops stay closed.";

const char* HOW[H_LK_COUNT] = {
    "People visit temples in white and take part in the all-night Duruthu Perahera at Kelaniya, with dancers, drummers and decorated elephants.",
    "Colombo's Navam Perahera lights up the streets with elephants, torch dancers and drummers, and many people spend the day at temples.",
    "People visit temples in white to listen to sermons and practise generosity and meditation.",
    "People visit temples in white to listen to sermons and practise generosity and meditation.",
    "Homes and streets are hung with paper lanterns (vesak koodu), and lit pandals show scenes from the Jataka tales. Volunteers run free food and drink stalls (dansal), and people visit temples in white. The following day is usually a holiday too.",
    "Pilgrims in white climb the steps of Mihintale and gather at Anuradhapura. Towns put up Poson lanterns and pandals, and free food stalls (dansal) serve passers-by.",
    "People visit temples in white, and the Kandy Esala Perahera fills the city with drummers, dancers, fire performers and richly dressed elephants carrying the casket of the Sacred Tooth Relic.",
    "People visit temples in white to listen to the Dhamma and to offer alms to monks.",
    "People visit temples in white; many women and girls attend special sermons and offer alms.",
    "People visit temples in white and prepare to offer robes (Kathina) to monks.",
    "People visit temples in white; Kathina robe-offering ceremonies are held at many temples.",
    "People visit temples in white; Anuradhapura's Sri Maha Bodhi draws many pilgrims.",
    "Lanterns, pandals and free food stalls usually carry on into this second day.",
    "Families boil fresh rice with milk and jaggery in a new clay pot in the open air and cheer 'Pongalo Pongal!' as it boils over. They draw kolam patterns at the door, wear new clothes, and honour their cattle the next day.",
    "The main ceremony is held at Independence Square in Colombo, with the national flag raised, the national anthem sung in Sinhala and Tamil, a military parade and a message from the President. Flags fly across the island.",
    "Devotees fast, stay awake all night in prayer and chanting, and bathe the Shiva lingam with milk, honey and water. Large crowds gather at ancient Shiva temples such as Koneswaram in Trincomalee and Naguleswaram in Keerimalai.",
    "Muslims gather for a special morning prayer, wear new clothes, give charity (Zakat al-Fitr) to the poor, and share festive meals and sweets with family and neighbours.",
    "Churches hold solemn services and the Stations of the Cross, and many Christians fast or avoid meat. Many parishes hold processions and passion plays.",
    "A neutral period (nonagathe) follows when no work is done. Families spend it at temples, clean and prepare the house, and get ready for the new year rituals.",
    "Families light the hearth and boil milk until it overflows (kiri ibeema), share a first meal of kiribath with kavum and kokis, anoint their heads with herbal oil, and wear clothes in the lucky colour. Games such as raban, pillow fights and greasy-pole climbing fill the days.",
    "Trade unions and political parties hold rallies and marches, the biggest of them in Colombo.",
    "After the special morning prayer, families sacrifice an animal and share the meat in three parts: with family, with friends and neighbours, and with the poor.",
    "Mosques and streets are decorated, processions are held in many Muslim towns, and people give charity and food to the poor.",
    "Families take an early oil bath, wear new clothes, light rows of oil lamps at home, visit the temple, share sweets and let off fireworks.",
    "Midnight Mass on Christmas Eve, carols, cribs and Christmas trees. Families share a big meal with rich fruit Christmas cake and love cake.",
};

std::string ordinal(int n) {
    const char* s = "th";
    if (n % 100 < 11 || n % 100 > 13) { if (n % 10 == 1) s = "st"; else if (n % 10 == 2) s = "nd"; else if (n % 10 == 3) s = "rd"; }
    return fmt("%d%s", n, s);
}

std::map<int, std::vector<Holiday>>& cache() { static std::map<int, std::vector<Holiday>> c; return c; }

// ---- dates from the user's holidays.txt
struct Over { int y, m, d, id; bool adhi; };
std::vector<Over>& overs() { static std::vector<Over> v; return v; }

// the words holidays.txt uses, in the order of enum Id
const char* const KEYS[H_LK_COUNT] = {
    "duruthu", "navam", "medin", "bak", "vesak", "poson", "esala", "nikini", "binara", "vap", "il", "unduvap",
    "vesak-after", "pongal", "independence", "sivarathri", "eid-fitr", "good-friday", "ny-eve", "ny-day", "may-day",
    "eid-adha", "milad", "deepavali", "christmas" };

int daysIn(int y, int m) {
    static const int n[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return n[m - 1];
}

// "2028-01-12" or "2028/1/12"; false when it is not a real date between 1990 and 2100
bool parseDate(const std::string& s, int& y, int& m, int& d) {
    int v[3] = {0, 0, 0}, k = 0, digits = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i < s.size() && std::isdigit((unsigned char)s[i])) { if (++digits > 4) return false; v[k] = v[k] * 10 + (s[i] - '0'); }
        else if (i == s.size() || s[i] == '-' || s[i] == '/') { if (digits == 0 || k > 2) return false; if (i < s.size()) { if (++k > 2) return false; } digits = 0; }
        else return false;
    }
    if (k != 2) return false;
    y = v[0]; m = v[1]; d = v[2];
    return y >= 1990 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= daysIn(y, m);
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// ======================================================= shared by the UK and US rules
// the day of the month of the n-th weekday (wd 0 = Sunday) of a month; n = -1 is the last one
int nthWeekday(int y, int m, int wd, int n) {
    if (n > 0) return 1 + (wd - astro::dayOfWeek(y, m, 1) + 7) % 7 + (n - 1) * 7;
    const int last = daysIn(y, m);
    return last - (astro::dayOfWeek(y, m, last) - wd + 7) % 7;
}
bool isWeekend(int y, int m, int d) { const int w = astro::dayOfWeek(y, m, d); return w == 0 || w == 6; }
Holiday mkHoliday(int y, int m, int d, int id, int kind = 0, int ly = 0, int lm = 0, int ld = 0) {
    Holiday h; h.y = y; h.m = m; h.d = d; h.id = id; h.kind = kind; h.ly = ly; h.lm = lm; h.ld = ld;
    return h;
}

#define MD "\xC2\xB7"          // the middle dot used in every tag
struct XInfo { const char* name; const char* shortName; const char* tag; int group; const char* what; const char* who; const char* how; const char* src; };

#include "holidays_us.h"
#include "holidays_uk.h"

// ======================================================= the countries
// A country is its ids (holidays.h), its generator and texts (its own header), and one row here. Adding one: write the header, add the ids and a row.
struct Country { const char* code; const char* name; int firstId, endId; };
const Country COUNTRIES[] = {
    {"LK", "Sri Lanka", 0, H_LK_COUNT},
    {"US", "United States", H_US_NEWYEAR, H_UK_NEWYEAR},
    {"UK", "United Kingdom", H_UK_NEWYEAR, H_COUNT},
};
const Country& countryOfId(int id) { for (const auto& c : COUNTRIES) if (id >= c.firstId && id < c.endId) return c; return COUNTRIES[0]; }
const XInfo& xinfo(int id) { return id >= H_UK_NEWYEAR ? XI_UK[id - H_UK_NEWYEAR] : XI_US[id - H_US_NEWYEAR]; }

int g_mask = C_LK;               // which countries' holidays are on
int g_nation = R_UK_EW;          // which UK nation's list is used when the UK is on
int g_overrideVer = 0;           // incremented each time setOverrides() changes the table

} // namespace

std::string setOverrides(const std::string& text) {
    std::vector<Over> out;
    std::string firstProblems;
    int bad = 0, line = 0;
    auto problem = [&](const std::string& why) {
        if (++bad <= 2) firstProblems += std::string(firstProblems.empty() ? "" : "; ") + "line " + std::to_string(line) + ": " + why;
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string ln = text.substr(pos, e - pos);
        pos = e + 1;
        ++line;
        size_t hash = ln.find('#');
        if (hash != std::string::npos) ln.erase(hash);
        if (line == 1 && ln.compare(0, 3, "\xEF\xBB\xBF") == 0) ln.erase(0, 3);      // a Notepad byte-order mark
        ln = trimmed(ln);
        if (ln.empty()) continue;
        std::vector<std::string> w;
        for (size_t i = 0; i < ln.size();) {
            while (i < ln.size() && std::isspace((unsigned char)ln[i])) ++i;
            size_t j = i;
            while (j < ln.size() && !std::isspace((unsigned char)ln[j])) ++j;
            if (j > i) w.push_back(ln.substr(i, j - i));
            i = j;
        }
        Over o{0, 0, 0, -1, false};
        if (w.size() < 2 || w.size() > 3) { problem("write it as  2028-01-12  duruthu"); continue; }
        if (!parseDate(w[0], o.y, o.m, o.d)) { problem("'" + w[0] + "' is not a date like 2028-01-12"); continue; }
        std::string key;
        for (char ch : w[1]) key += ch == '_' ? '-' : (char)std::tolower((unsigned char)ch);
        for (int i = 0; i < H_LK_COUNT; ++i) if (key == KEYS[i]) { o.id = i; break; }
        if (o.id < 0) { problem("I do not know a holiday called '" + w[1] + "'"); continue; }
        if (w.size() == 3) {
            std::string extra;
            for (char ch : w[2]) extra += (char)std::tolower((unsigned char)ch);
            if (extra != "adhi" || o.id > H_UNDUVAP) { problem("only a Poya can be followed by the word adhi"); continue; }
            o.adhi = true;
        }
        out.push_back(o);
    }
    overs() = out;
    ++g_overrideVer;
    cache().clear();
    if (bad > 2) firstProblems += "; and " + std::to_string(bad - 2) + " more";
    return firstProblems;
}

int overrideCount() { return (int)overs().size(); }
int overrideVersion() { return g_overrideVer; }

std::string exampleFile() {
    std::string s =
        "# Meridian holiday dates.\n"
        "#\n"
        "# Meridian has the official lists for 2026 and 2027 built in and works out other years from the calendar and the Moon.\n"
        "# When the Government publishes a new list, type the dates here and they replace the built-in ones.\n"
        "# Save the file and Meridian picks the change up within a few seconds, with no restart.\n"
        "#\n"
        "# One holiday per line: the date as year-month-day, then its name. Lines starting with # are ignored.\n"
        "#     2028-01-12  duruthu\n"
        "#     2028-04-12  bak\n"
        "# An extra leap-month Poya has the word adhi after the name:\n"
        "#     2028-06-07  poson  adhi\n"
        "# If you change Vesak, the day after it follows by itself unless you also give vesak-after.\n"
        "#\n"
        "# Names you can use:\n"
        "#   Poya days:   ";
    for (int i = 0; i <= H_UNDUVAP; ++i) { s += KEYS[i]; if (i < H_UNDUVAP) s += ", "; if (i == 5) s += "\n#                "; }
    s += "\n#   Other days:  ";
    for (int i = H_UNDUVAP + 1; i < H_LK_COUNT; ++i) { s += KEYS[i]; if (i < H_LK_COUNT - 1) s += ", "; if (i == 16 || i == 21) s += "\n#                "; }
    s += "\n#\n# Your dates go below this line.\n\n";
    return s;
}

bool officialYear(int year) { return year == 2026 || year == 2027; }
std::vector<Holiday> estimateYear(int year) { return estimate(year); }

namespace {
// Sri Lanka's list: the official table (or an estimate), then the dates typed into holidays.txt. Sorted by date, Poya first on a shared date.
std::vector<Holiday> lkYear(int year) {
    std::vector<Holiday> v;
    if (year == 2026 || year == 2027) {
        const Row* rows = year == 2026 ? Y2026 : Y2027;
        size_t n = year == 2026 ? sizeof Y2026 / sizeof Y2026[0] : sizeof Y2027 / sizeof Y2027[0];
        for (size_t i = 0; i < n; ++i) { Holiday h; h.y = year; h.m = rows[i].m; h.d = rows[i].d; h.id = rows[i].id; h.adhi = rows[i].adhi; v.push_back(h); }
    } else v = estimate(year);
    {   // dates the user typed into holidays.txt replace the matching holiday, or add it when the year has none (supporting two Islamic lunar occurrences in one year)
        bool vesakGiven = false, vesakAfterGiven = false;
        for (const auto& o : overs()) if (o.y == year) { if (o.id == H_VESAK && !o.adhi) vesakGiven = true; if (o.id == H_VESAK_AFTER) vesakAfterGiven = true; }
        std::vector<bool> replaced(v.size(), false);
        for (const auto& o : overs()) {
            if (o.y != year) continue;
            Holiday* hit = nullptr;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i < replaced.size() && replaced[i]) continue;
                if (v[i].id == o.id && v[i].adhi == o.adhi) { hit = &v[i]; if (i < replaced.size()) replaced[i] = true; break; }
            }
            if (!hit) { Holiday n; n.y = year; n.id = o.id; n.adhi = o.adhi; v.push_back(n); replaced.push_back(true); hit = &v.back(); }
            hit->m = o.m; hit->d = o.d; hit->estimated = false; hit->custom = true;
        }
        if (vesakGiven && !vesakAfterGiven) {
            const Holiday* vs = nullptr;
            for (const auto& h : v) if (h.id == H_VESAK && !h.adhi) vs = &h;
            if (vs) {
                int yy = vs->y, mm = vs->m, dd = vs->d; addDays(yy, mm, dd, 1);
                if (yy == year) {
                    bool found = false;
                    for (auto& h : v) if (h.id == H_VESAK_AFTER) { h.y = yy; h.m = mm; h.d = dd; h.estimated = false; h.custom = true; found = true; }
                    if (!found) { Holiday n; n.y = yy; n.m = mm; n.d = dd; n.id = H_VESAK_AFTER; n.custom = true; v.push_back(n); }
                }
            }
        }
    }
    std::stable_sort(v.begin(), v.end(), [](const Holiday& a, const Holiday& b) {
        if (a.m != b.m) return a.m < b.m;
        if (a.d != b.d) return a.d < b.d;
        return (a.poya() ? 0 : 1) < (b.poya() ? 0 : 1);        // the Poya first on a shared date
    });
    return v;
}
} // namespace

// The holidays of every country that is switched on, merged into one list sorted by date (the Poya first, then Sri Lanka, UK, US on a shared date).
std::vector<Holiday> forYear(int year) {
    auto& c = cache();
    const int ck = (g_mask | g_nation << 3) * 10000 + year;
    auto it = c.find(ck);
    if (it != c.end()) return it->second;
    std::vector<Holiday> v;
    if (g_mask & C_LK) { auto l = lkYear(year); v.insert(v.end(), l.begin(), l.end()); }
    if (g_mask & C_UK) { auto u = genUK(year, g_nation == R_UK_SCT ? N_SCT : g_nation == R_UK_NI ? N_NI : N_EW); v.insert(v.end(), u.begin(), u.end()); }
    if (g_mask & C_US) { auto u = genUS(year); v.insert(v.end(), u.begin(), u.end()); }
    std::stable_sort(v.begin(), v.end(), [](const Holiday& a, const Holiday& b) {
        if (a.m != b.m) return a.m < b.m;
        if (a.d != b.d) return a.d < b.d;
        return (a.poya() ? 0 : 1) < (b.poya() ? 0 : 1);
    });
    if (c.size() > 24) c.clear();
    c[ck] = v;
    return v;
}

std::vector<Holiday> onDate(int y, int m, int d) {
    std::vector<Holiday> out;
    for (const auto& h : forYear(y)) if (h.m == m && h.d == d) out.push_back(h);
    return out;
}

std::vector<Day> upcoming(int y, int m, int d, int maxDays, int maxCount) {
    std::vector<Day> out;
    const double d0 = astro::daysFromCivil(y, m, d);
    for (int yy = y; yy <= y + 1; ++yy) {
        for (const auto& h : forYear(yy)) {
            double dd = astro::daysFromCivil(h.y, h.m, h.d) - d0;
            if (dd < 0 || dd > maxDays) continue;
            if (!out.empty() && out.back().y == h.y && out.back().m == h.m && out.back().d == h.d) { out.back().items.push_back(h); continue; }
            Day day; day.y = h.y; day.m = h.m; day.d = h.d; day.items.push_back(h);
            out.push_back(day);
        }
    }
    if ((int)out.size() > maxCount) out.resize(maxCount);
    return out;
}

void setCountries(int mask, int nation) {
    g_mask = mask & (C_LK | C_UK | C_US);
    g_nation = nation == R_UK_SCT || nation == R_UK_NI ? nation : R_UK_EW;
}
int countries() { return g_mask; }
int ukNation() { return g_nation; }

std::string countryList(int mask, int nation) {
    std::string s;
    auto add = [&](const std::string& t) { if (!s.empty()) s += " + "; s += t; };
    if (mask & C_LK) add("Sri Lanka");
    if (mask & C_UK) add(std::string("United Kingdom (") + (nation == R_UK_SCT ? "Scotland" : nation == R_UK_NI ? "Northern Ireland" : "England & Wales") + ")");
    if (mask & C_US) add("United States");
    return s.empty() ? std::string("None") : s;
}

std::string countryCode(const Holiday& h) { return countryOfId(h.id).code; }
std::string countryName(const Holiday& h) { return countryOfId(h.id).name; }

namespace {
std::string lower(std::string s) { for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; }
bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
// the UK nation of a place: from its region text when it has one, else from where it is on the map
int ukNation(const std::string& regionText, double lat, double lon) {
    const std::string t = lower(regionText);
    if (has(t, "scotland") || has(t, "sct")) return R_UK_SCT;
    if (has(t, "northern ireland") || has(t, "nir") || has(t, "ulster")) return R_UK_NI;
    if (has(t, "wales") || has(t, "england") || has(t, "eng") || has(t, "wls")) return R_UK_EW;
    if (lat == 0 && lon == 0) return R_UK_EW;
    if (lon <= -5.4 && lat >= 54.0 && lat <= 55.35) return R_UK_NI;                  // the island of Ireland's north-east corner
    // the Scotland-England border from the Solway Firth to the North Sea, as a few points (longitude, latitude of the border there)
    static const double B[][2] = {{-3.6, 54.97}, {-3.06, 54.99}, {-2.78, 55.18}, {-2.48, 55.35}, {-2.25, 55.64}, {-2.03, 55.80}};
    double border = 54.6;                                                           // west of the Solway: Scotland starts at the Galloway coast
    if (lon >= B[5][0]) border = B[5][1];
    else if (lon >= B[0][0]) for (int i = 0; i < 5; ++i) if (lon <= B[i + 1][0]) { border = B[i][1] + (lon - B[i][0]) / (B[i + 1][0] - B[i][0]) * (B[i + 1][1] - B[i][1]); break; }
    if (lat > border) return R_UK_SCT;
    return R_UK_EW;
}
bool inSriLankaBox(double lat, double lon) { return lat >= 5.7 && lat <= 10.0 && lon >= 79.4 && lon <= 82.2; }
}

int resolveUkNation(int pref, const std::string& regionText, double lat, double lon) {
    switch (pref) {
        case 1: return R_UK_EW;
        case 2: return R_UK_SCT;
        case 3: return R_UK_NI;
        default: return ukNation(regionText, lat, lon);
    }
}

int resolveAuto(const std::string& country, const std::string& regionText, double lat, double lon, const std::string& systemCountry) {
    (void)regionText;
    std::string cc;
    for (char ch : country) cc += (char)std::toupper((unsigned char)ch);
    if (cc.empty() && inSriLankaBox(lat, lon)) cc = "LK";                    // a place typed in by hand has no country, but Sri Lanka is easy to tell
    if (cc.empty()) for (char ch : systemCountry) cc += (char)std::toupper((unsigned char)ch);
    if (cc == "LK") return C_LK;
    if (cc == "US") return C_US;
    if (cc == "GB" || cc == "UK") return C_UK;
    return 0;
}

// One line for a date's holidays: "Christmas Day (International)  +  Memorial Day (US)". The labels only when more than one country is on.
std::string dayTitle(const Day& d, bool codes) {
    std::string t;
    for (const auto& m : merge(d.items)) {
        if (!t.empty()) t += "  +  ";
        t += shortName(m.lead());
        if (codes) t += " (" + label(m) + ")";
    }
    return t;
}

std::string name(const Holiday& h) { return h.id >= H_LK_COUNT ? std::string(xinfo(h.id).name) : std::string(h.adhi ? "Adhi " : "") + ENT[h.id].name; }
std::string shortName(const Holiday& h) { return h.id >= H_LK_COUNT ? std::string(xinfo(h.id).shortName) : std::string(h.adhi ? "Adhi " : "") + ENT[h.id].shortName; }
namespace {
std::string baseTag(const Holiday& h) {
    if (h.id >= H_LK_COUNT) return xinfo(h.id).tag;
    return h.adhi ? "BUDDHIST \xC2\xB7 ADHI POYA" : ENT[h.id].tag;
}
}
std::string tag(const Holiday& h) { return countryCode(h) + " \xC2\xB7 " + baseTag(h); }

int countryBit(const Holiday& h) { return h.id >= H_UK_NEWYEAR ? C_UK : h.id >= H_US_NEWYEAR ? C_US : C_LK; }
std::string countryCodes(int mask) {
    std::string s;
    auto add = [&](const char* c) { if (!s.empty()) s += " \xC2\xB7 "; s += c; };
    if (mask & C_LK) add("LK");
    if (mask & C_UK) add("UK");
    if (mask & C_US) add("US");
    return s;
}
std::vector<Merged> merge(const std::vector<Holiday>& items) {
    std::vector<Merged> out;
    std::vector<std::string> keys;
    for (const auto& h : items) {
        const std::string k = name(h) + "|" + std::to_string(h.kind) + "|" + std::to_string(h.ly * 10000 + h.lm * 100 + h.ld);
        size_t i = 0; while (i < keys.size() && keys[i] != k) ++i;
        if (i == keys.size()) { keys.push_back(k); out.emplace_back(); }
        out[i].all.push_back(h); out[i].mask |= countryBit(h);
    }
    return out;
}
std::string label(const Merged& m) { return m.international() ? std::string("International") : countryCode(m.lead()); }
std::string tag(const Merged& m) { return m.international() ? "INTERNATIONAL \xC2\xB7 " + baseTag(m.lead()) : tag(m.lead()); }
int group(const Holiday& h) { return h.id >= H_LK_COUNT ? xinfo(h.id).group : ENT[h.id].group; }
bool mercantile(const Holiday& h) { return h.id < H_LK_COUNT && ENT[h.id].merc; }
std::string categories(const Holiday& h) {
    if (h.id >= H_UK_NEWYEAR) return "Bank";
    if (h.id >= H_US_NEWYEAR) return "Federal \xC2\xB7 Bank";
    return std::string("Public \xC2\xB7 Bank") + (mercantile(h) ? " \xC2\xB7 Mercantile" : "");
}

std::string note(const Holiday& h) {
    if (h.kind == 0) return std::string();
    const std::string other = fmt("%s %d %s", util::WEEKDAYS3[astro::dayOfWeek(h.ly, h.lm, h.ld)], h.ld, util::MONTHS3[h.lm - 1]);
    const bool us = h.id >= H_US_NEWYEAR && h.id < H_UK_NEWYEAR;
    if (h.kind == 1) return (us ? "Observed for " : "Substitute for ") + other;
    return (us ? "Observed on " : "Day off moves to ") + other;
}

std::string kindLine(const Holiday& h) {
    std::string s;
    if (h.id >= H_UK_NEWYEAR) s = "Bank holiday";
    else if (h.id >= H_US_NEWYEAR) s = "Federal holiday";
    else s = mercantile(h) ? "Public, bank and mercantile holiday" : "Public and bank holiday";
    const std::string n = note(h);
    if (!n.empty()) s += " \xC2\xB7 " + n;
    return s;
}

std::string what(const Holiday& h) {
    if (h.id >= H_LK_COUNT) return xinfo(h.id).what;
    std::string s = WHAT[h.id];
    if (h.id == H_INDEPENDENCE) s += fmt(" This year is the %s anniversary.", ordinal(h.y - 1948).c_str());
    if (h.adhi) s += " An Adhi Poya is an extra full moon: every few years the Sinhala lunar calendar adds a leap month to stay in step with the seasons, which gives two Poya days with the same name.";
    return s;
}

std::string who(const Holiday& h) {
    if (h.id >= H_LK_COUNT) return xinfo(h.id).who;
    if (h.poya()) return "Everyone. Poya days are common holidays for all employees, so government offices, schools, banks and private firms are closed.";
    if (mercantile(h)) return "Government offices, state schools and banks are closed, and private-sector staff covered by the Shop and Office Act get it as a mercantile holiday too.";
    return "Government offices, state schools and banks are closed. It is not a mercantile holiday, so whether private firms close is up to the employer.";
}

std::string how(const Holiday& h) {
    if (h.id >= H_LK_COUNT) return xinfo(h.id).how;
    std::string s = HOW[h.id];
    if (h.poya() && h.id != H_VESAK) s += POYA_HOW;
    else if (h.id == H_VESAK) s += " By law no alcohol or meat is sold.";
    return s;
}

std::string source(const Holiday& h) {
    if (h.id >= H_LK_COUNT) {
        const bool inRange = h.y >= 2026 && h.y <= 2028;
        if (h.id == H_UK_WORLDCUP) return xinfo(h.id).src;
        return inRange ? std::string(xinfo(h.id).src) : std::string("Worked out from the ") + (h.id >= H_UK_NEWYEAR ? "bank holiday rules" : "federal holiday rules") + "; extra one-off days are not known in advance";
    }
    if (h.custom) return "Your holidays.txt";
    if (h.y == 2026) return "Government of Sri Lanka holiday calendar 2026";
    if (h.y == 2027) return "Gazetted public holidays for 2027";
    if (!h.estimated) return "Fixed date";
    return "Estimated: this year's official list is not published yet";
}

} // namespace holiday
