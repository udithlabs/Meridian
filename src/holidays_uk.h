// United Kingdom bank holidays: names, descriptions and the date rules (England & Wales, Scotland, Northern Ireland; substitute days).
// Included inside the anonymous namespace of holidays.cpp, after its helpers. One country = one file like this, its ids in holidays.h and one
// line in the COUNTRIES table of holidays.cpp. Facts follow the "US & UK Public Holidays 2026-2028" reference sheet (GOV.UK, gov.scot, nidirect).

const char* const SRC_GOV = "GOV.UK bank holidays " MD " Banking and Financial Dealings Act 1971";
const char* const SRC_SCOT = "GOV.UK / mygov.scot " MD " Banking and Financial Dealings Act 1971";
const char* const SRC_NI = "GOV.UK / nidirect " MD " Banking and Financial Dealings Act 1971";

// in the order of the ids from H_UK_NEWYEAR
const XInfo XI_UK[H_UK_COUNT] = {
    {"New Year's Day", "New Year's Day", "CIVIC " MD " BANK", G_NEWYEAR,
     "New Year's Day celebrates the start of the Gregorian year. Rooted in Scotland's Hogmanay tradition, it became a statutory bank holiday in England, Wales and Northern Ireland from 1974 under the Banking and Financial Dealings Act 1971.",
     "Government offices, banks, post offices and schools (on winter break) close, on the substitute weekday when 1 January is a weekend. UK employees have no automatic right to paid time off on bank holidays: it depends on the contract. Shops and hospitality often trade on reduced hours.",
     "London holds the New Year's Day Parade through the West End. Scotland carries on Hogmanay, with the Loony Dook cold-water swim at South Queensferry and \"first-footing\" visits. Many people watch Premier League football and New Year's Day racing.",
     SRC_GOV},
    {"2nd January", "2nd January", "NATIONAL " MD " BANK " MD " SCOTLAND", G_NEWYEAR,
     "Unique to Scotland, reflecting the centuries-old Scottish focus on Hogmanay as the main midwinter festival. It became a statutory Scottish bank holiday under the 1971 Act (from 1973) to keep the two-day New Year break.",
     "Scottish government offices, councils, many workplaces and schools (in winter holidays) close. Scottish clearing banks often stay open to keep in step with England and Wales, where it is a normal working day. Shops and supermarkets mostly open on holiday hours.",
     "Families keep visiting friends and relatives (\"first-footing\" with shortbread, black bun or whisky), go to local New Year gatherings, and recover from Hogmanay before going back to work.",
     SRC_SCOT},
    {"St Patrick's Day", "St Patrick's Day", "CHRISTIAN " MD " NATIONAL " MD " NORTHERN IRELAND", G_CHRISTIAN,
     "Remembers Saint Patrick, the 5th-century Romano-British missionary and patron saint of Ireland, on 17 March, the anniversary of his death. It became a bank holiday across Ireland under the Bank Holiday (Ireland) Act 1903 and stayed one in Northern Ireland after partition.",
     "High-street banks, Northern Ireland public offices and schools close. Many private offices close, while pubs, restaurants and city-centre shops, especially in Belfast, stay open. It is not a bank holiday in England, Wales or Scotland.",
     "Towns and cities, above all Belfast, Armagh and Downpatrick (where St Patrick is buried), host parades, Irish music and dancing, church services, shamrocks and green clothes, and community festivals.",
     SRC_NI},
    {"Good Friday", "Good Friday", "CHRISTIAN " MD " BANK", G_CHRISTIAN,
     "The Christian solemn observance of the crucifixion of Jesus, two days before Easter Sunday. In England, Wales and Northern Ireland it is one of the oldest holidays recognized at common law; in Scotland it is a statutory bank holiday.",
     "Government offices, clearing banks in all four UK nations, London's financial markets, post offices and schools (on Easter break) close, starting the four-day Easter weekend in England, Wales and Northern Ireland. Most supermarkets and shops stay open, often on shorter hours.",
     "Christians attend solemn afternoon services, Stations of the Cross or public passion plays. Families eat hot cross buns and fish rather than meat, and many people start their Easter weekend travel.",
     SRC_GOV},
    {"Easter Monday", "Easter Monday", "CHRISTIAN " MD " BANK " MD " E&W & NI", G_CHRISTIAN,
     "Follows Easter Sunday and celebrates the Resurrection. Made a statutory bank holiday in England, Wales and Ireland by Sir John Lubbock's Bank Holidays Act 1871 (kept in the 1971 Act), it completes the four-day Easter break outside Scotland.",
     "Government offices, banks, post offices and most offices in England, Wales and Northern Ireland close. It is not a statutory bank holiday in Scotland, but Scottish clearing banks also close to stay aligned, and most Scottish councils give schools the spring holiday. Shops and attractions stay open.",
     "Families take spring day trips, country walks and egg-rolling contests, and visit theme parks and garden centres. Football and racing fixtures (such as the Irish Grand National) take place, and public transport runs bank holiday timetables.",
     SRC_GOV},
    {"Early May Bank Holiday", "Early May", "SEASONAL " MD " BANK", G_BANK,
     "Introduced across the whole UK in 1978 on the first Monday in May, building on earlier Scottish May traditions. It reflects both International Workers' Day and the ancient May Day spring festivals.",
     "Government offices, courts, schools, post offices, banks and most office-based employers across England, Wales, Scotland and Northern Ireland close (subject to contracts). Shops, supermarkets, pubs and attractions generally stay open.",
     "Towns and villages mark May Day with maypole and Morris dancing, May Queens, village fetes and trade union marches in the cities, plus spring gardening and coastal trips.",
     SRC_GOV},
    {"Spring Bank Holiday", "Spring Bank Holiday", "SEASONAL " MD " BANK", G_BANK,
     "Replaced Whit Monday (the Monday after Pentecost, which drifted with Easter) for good under the 1971 Act, after trial years in the late 1960s. Fixed to the last Monday in May, it usually lines up with the school half-term break.",
     "Government offices, banks, post offices, schools (half-term week) and most corporate offices across all four UK nations close. Supermarkets, shops and hospitality run as normal or on Sunday and holiday hours.",
     "One of the busiest leisure weekends of late spring: music festivals, street fairs, seaside trips, traditional events such as Cooper's Hill Cheese-Rolling in Gloucestershire, DIY projects and family barbecues.",
     SRC_GOV},
    {"FIFA World Cup Bank Holiday (Scotland)", "World Cup Holiday", "ROYAL PROCLAMATION " MD " SPECIAL " MD " SCOTLAND", G_NATIONAL,
     "A special one-off bank holiday proclaimed by Royal Proclamation, on the proposal of Scotland's First Minister, for Monday 15 June 2026. It celebrates the Scotland men's football team playing in the World Cup finals for the first time since 1998, after the opening match against Haiti.",
     "Scotland only: England, Wales and Northern Ireland did not get it. Scottish Government offices closed, but private employers did not have to give paid leave or close unless their contracts said so.",
     "Fans gathered at fan zones, pubs and community events over the long weekend to celebrate Scotland's World Cup campaign and to recover from the early-morning kick-off.",
     "Scottish Government (gov.scot) " MD " Royal Proclamation 2026"},
    {"Battle of the Boyne (Orangemen's Day)", "The Twelfth", "HISTORICAL " MD " BANK " MD " NORTHERN IRELAND", G_NATIONAL,
     "\"The Twelfth\" remembers the 1690 Battle of the Boyne, where the Protestant King William III (William of Orange) defeated the deposed Catholic King James II. It became a Northern Ireland bank holiday after partition, is in the 1971 Act, and moves to Monday when 12 July is a weekend day.",
     "High-street banks, government offices, councils and many businesses in Northern Ireland close, and schools are on summer break. Retail hours vary: many big Belfast shops open while smaller towns close for the parade routes. Not observed in England, Wales or Scotland.",
     "The Orange Order holds Twelfth parades with flute and Lambeg drum bands in about 18 towns, including Belfast, after the Eleventh Night bonfires on 11 July. Many residents leave the province during the \"Twelfth fortnight\".",
     SRC_NI},
    {"Summer Bank Holiday (Scotland)", "Summer Bank Holiday", "SEASONAL " MD " BANK " MD " SCOTLAND", G_BANK,
     "The Bank Holidays Act 1871 set the August bank holiday on the first Monday in August. Scotland kept that date after England, Wales and Northern Ireland moved theirs to the end of August (trialled from 1965, fixed by the 1971 Act).",
     "Many Scottish public offices and councils treat it as a holiday (council holidays vary, and schools are on summer break). Major Scottish clearing banks generally stay open and close on the last Monday in August instead, to stay in step with England and Wales.",
     "It falls in the peak of the Scottish summer festival season, with the opening weeks of the Edinburgh Fringe and International Festival, Highland games, outdoor events and family day trips.",
     SRC_SCOT},
    {"Summer Bank Holiday (England, Wales & Northern Ireland)", "Summer Bank Holiday", "SEASONAL " MD " BANK " MD " E&W & NI", G_BANK,
     "The 1871 Act put it on the first Monday in August. England, Wales and Northern Ireland trialled the end of August from 1965 and fixed it to the last Monday in August under the 1971 Act, to spread out summer travel and improve the odds of good weather.",
     "Government offices, courts, post offices and most offices in England, Wales and Northern Ireland close. Clearing banks across the whole UK, Scotland included, close too, though it is a normal working day for other Scottish employers. Retail and leisure stay open.",
     "The last long weekend before schools reopen: Notting Hill Carnival in London, big music festivals (Reading and Leeds, Creamfields), seaside trips, country shows and busy motorways.",
     SRC_GOV},
    {"St Andrew's Day", "St Andrew's Day", "CHRISTIAN " MD " NATIONAL " MD " SCOTLAND", G_NATIONAL,
     "Celebrates Scotland's patron saint, Andrew the Apostle, whose saltire cross is Scotland's flag. The St Andrew's Day Bank Holiday (Scotland) Act 2007 made 30 November (or the next weekday, if it falls on a weekend) an official Scottish bank holiday.",
     "In practice it is voluntary: Scottish schools stay open, Scottish clearing banks generally stay open, and private employers need not give the day off unless the contract says so. Some public bodies and Scottish Government offices grant leave or substitute another day.",
     "Ceilidhs, traditional Scottish music and dance, piping, Saltire displays and heritage events, with free entry to many Historic Environment Scotland sites around the weekend.",
     "GOV.UK / gov.scot " MD " St Andrew's Day Bank Holiday (Scotland) Act 2007"},
    {"Christmas Day", "Christmas Day", "CHRISTIAN " MD " BANK", G_CHRISTIAN,
     "Celebrates the birth of Jesus Christ and is the centerpiece of Britain's winter season, shaped by Victorian traditions: Christmas cards, trees, crackers and festive feasts. A common-law holiday in England, Wales and Northern Ireland and a statutory bank holiday in Scotland.",
     "A near-total shutdown: government offices, banks, post offices, schools, public transport and almost all businesses close, and again on the substitute weekday when 25 December is a weekend. By law, large shops over 280 square metres (3,000 sq ft) cannot open on 25 December.",
     "Families open presents, pull crackers, share roast turkey with all the trimmings and Christmas pudding, go to church services, and watch the King's Christmas Broadcast at 3:00 PM GMT.",
     SRC_GOV},
    {"Boxing Day", "Boxing Day", "HISTORICAL " MD " BANK", G_BANK,
     "St Stephen's Day (26 December). The name comes from older British customs of giving servants a \"Christmas box\" of money and gifts the day after Christmas, and from church alms-boxes for the poor. A bank holiday since 1871 (England, Wales, NI) and from 1974 in Scotland, it moves to a weekday when it falls on a weekend.",
     "Government offices, banks, post offices, schools (winter break) and most offices close, including on substitute Mondays and Tuesdays. Many chains and department stores reopen for the post-Christmas sales, though some big supermarkets stay shut to give staff an extra day.",
     "A day of British sport (Premier League, EFL and Scottish Premiership football, the King George VI Chase), winter walks, family visits, Christmas leftovers (turkey sandwiches, bubble and squeak) and the Boxing Day sales.",
     SRC_GOV},
};

enum { N_EW = 1, N_SCT = 2, N_NI = 4 };          // the three nations (a holiday's mask says which of them have it)

// The bank holidays of one nation in a year. A holiday on a weekend gets a substitute weekday: weekday holidays are placed first, then each weekend
// one goes to the next weekday that is still free (so Christmas Day and Boxing Day end up on consecutive weekdays). The real date stays as a kind 2 marker.
std::vector<Holiday> genUK(int Y, int nation) {
    std::vector<Holiday> v;
    std::set<int> taken;                                   // month * 100 + day of every day off so far
    auto key = [](int m, int d) { return m * 100 + d; };
    auto add = [&](int m, int d, int id) { v.push_back(mkHoliday(Y, m, d, id)); taken.insert(key(m, d)); };
    { int m, d; easter(Y, m, d); int y2 = Y, m2 = m, d2 = d;
      addDays(y2, m2, d2, -2); add(m2, d2, H_UK_GOODFRIDAY);
      if (nation & (N_EW | N_NI)) { y2 = Y; m2 = m; d2 = d; addDays(y2, m2, d2, 1); add(m2, d2, H_UK_EASTERMON); } }
    if (Y >= 1978) add(5, nthWeekday(Y, 5, 1, 1), H_UK_EARLYMAY);
    add(5, nthWeekday(Y, 5, 1, -1), H_UK_SPRING);
    if (nation == N_SCT) add(8, nthWeekday(Y, 8, 1, 1), H_UK_SUMMER_SCT); else add(8, nthWeekday(Y, 8, 1, -1), H_UK_SUMMER);
    if (nation == N_SCT && Y == 2026) add(6, 15, H_UK_WORLDCUP);                 // the one-off Royal Proclamation for the World Cup
    struct Fx { int m, d, id, mask; };
    static const Fx fx[] = {
        {1, 1, H_UK_NEWYEAR, 7}, {1, 2, H_UK_2JAN, N_SCT}, {3, 17, H_UK_STPATRICK, N_NI}, {7, 12, H_UK_BOYNE, N_NI},
        {11, 30, H_UK_STANDREW, N_SCT}, {12, 25, H_UK_CHRISTMAS, 7}, {12, 26, H_UK_BOXING, 7} };
    for (const auto& f : fx) if ((f.mask & nation) && !isWeekend(Y, f.m, f.d)) add(f.m, f.d, f.id);
    for (const auto& f : fx) {
        if (!(f.mask & nation) || !isWeekend(Y, f.m, f.d)) continue;
        int y2 = Y, m2 = f.m, d2 = f.d;
        do { addDays(y2, m2, d2, 1); } while (isWeekend(y2, m2, d2) || taken.count(key(m2, d2)));
        taken.insert(key(m2, d2));
        v.push_back(mkHoliday(y2, m2, d2, f.id, 1, Y, f.m, f.d));
        v.push_back(mkHoliday(Y, f.m, f.d, f.id, 2, y2, m2, d2));
    }
    return v;
}
