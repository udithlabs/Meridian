// United States federal holidays: names, descriptions and the date rules (observed days when a holiday falls on a weekend).
// Included inside the anonymous namespace of holidays.cpp, after its helpers. One country = one file like this, its ids in holidays.h and one
// line in the COUNTRIES table of holidays.cpp. Facts follow the "US & UK Public Holidays 2026-2028" reference sheet (U.S. OPM, 5 U.S.C. 6103).

const char* const SRC_OPM = "U.S. Office of Personnel Management (OPM) " MD " 5 U.S.C. \xC2\xA7 6103";

// in the order of the ids from H_US_NEWYEAR
const XInfo XI_US[H_US_COUNT] = {
    {"New Year's Day", "New Year's Day", "CIVIC " MD " NATIONAL", G_NEWYEAR,
     "New Year's Day marks the first day of the Gregorian year. It has been a federal holiday since 1870, when Congress created the first four. It celebrates the start of the new year after the New Year's Eve countdown.",
     "Federal offices, federal courts, public schools and most private businesses close on a weekday New Year's Day. When 1 January is a Saturday, federal employees get the Friday before off, while the stock exchanges, the Federal Reserve and the Post Office stay open that Friday.",
     "Many Americans rest after the midnight celebrations, watch college football bowl games and the Rose Parade in Pasadena, eat \"good luck\" foods such as black-eyed peas and collard greens, and set resolutions.",
     SRC_OPM},
    {"Birthday of Martin Luther King, Jr.", "MLK Jr. Day", "CIVIC " MD " CIVIL RIGHTS", G_NATIONAL,
     "Honors Dr. Martin Luther King, Jr. (born 15 January 1929), the chief spokesperson of nonviolent activism in the Civil Rights Movement. Signed into law in 1983 and first observed nationwide in 1986, it falls on the third Monday in January.",
     "All federal agencies and courts, the Post Office, the Federal Reserve, commercial banks, NYSE and Nasdaq, and most public schools and state offices close. Private employers are mixed: many offices close, while shops and hospitality stay open.",
     "A national Day of Service: communities organize volunteer projects, food drives, marches, interfaith services and educational programs. Many museums and national parks waive admission fees.",
     SRC_OPM},
    {"Washington's Birthday (Presidents' Day)", "Presidents' Day", "CIVIC " MD " HISTORICAL", G_NATIONAL,
     "Set up in 1880 for George Washington's 22 February birthday and moved to the third Monday in February by the Uniform Monday Holiday Act (1971). Federal law still calls it Washington's Birthday; popularly it is Presidents' Day and also honors Abraham Lincoln and all presidents.",
     "Federal offices and courts, the Post Office, the Federal Reserve, banks, NYSE and Nasdaq, and most public schools close (many districts fold it into a winter break). Most private employers, shops and restaurants stay open.",
     "Schools and civic groups hold history lessons and ceremonies, Mount Vernon hosts wreath-laying events, and retailers advertise Presidents' Day weekend sales on furniture, appliances, mattresses and cars.",
     SRC_OPM},
    {"Memorial Day", "Memorial Day", "NATIONAL " MD " MEMORIAL", G_NATIONAL,
     "Originally Decoration Day after the Civil War, it honors the U.S. military personnel who died while serving. It became a federal holiday in 1888 and moved to the last Monday in May in 1971. It is one of the country's most solemn days of remembrance.",
     "Federal and state offices and courts, the Post Office, the Federal Reserve, banks, NYSE and Nasdaq, public schools and most corporate and non-retail businesses close for the three-day weekend.",
     "Flags fly at half-staff until noon, and a National Moment of Remembrance is observed at 3:00 PM local time. Families and veterans' groups decorate graves at Arlington and local cemeteries, attend parades and gather for cookouts that mark the unofficial start of summer.",
     SRC_OPM},
    {"Juneteenth National Independence Day", "Juneteenth", "CIVIC " MD " EMANCIPATION", G_NATIONAL,
     "Commemorates 19 June 1865, when Union General Gordon Granger arrived in Galveston, Texas, and announced that enslaved African Americans in Texas were free, more than two years after the Emancipation Proclamation. Signed into law on 17 June 2021, it is the newest federal holiday.",
     "Federal agencies and courts and many corporate offices close. On a weekday the Federal Reserve, banks, the Post Office and the stock exchanges close too. On a Saturday, federal offices and the exchanges take the Friday, while the Federal Reserve and Post Office run normally. Many private retailers stay open.",
     "Cities and communities host parades, historical exhibits, concerts, lectures, family cookouts and cultural festivals celebrating African American freedom, heritage and resilience.",
     SRC_OPM},
    {"Independence Day (Fourth of July)", "Independence Day", "NATIONAL " MD " PATRIOTIC", G_NATIONAL,
     "Celebrates the adoption of the Declaration of Independence by the Second Continental Congress on 4 July 1776, which made the United States an independent nation. A federal holiday since 1870.",
     "Federal offices and courts and most private businesses close. When 4 July is a Saturday, federal offices and the exchanges close on Friday, while the Federal Reserve and Post Office run normally. When it is a Sunday, federal offices, banks, the Post Office and the markets all close on the Monday.",
     "Evening fireworks, patriotic concerts, town parades, backyard barbecues and beach trips, with red, white and blue decorations everywhere.",
     SRC_OPM},
    {"Labor Day", "Labor Day", "CIVIC " MD " LABOR", G_LABOUR,
     "Born of the late 19th-century labor movement and made a federal holiday by Congress in 1894, it honors the social and economic contributions of American workers. It falls on the first Monday in September.",
     "Federal and state offices and courts, the Post Office, the Federal Reserve, banks, NYSE and Nasdaq, public schools and most corporate offices close. Shops, supermarkets and restaurants mostly stay open.",
     "Seen as the unofficial end of summer and the back-to-school milestone: long-weekend cookouts, parades, college football openers, travel and Labor Day sales.",
     SRC_OPM},
    {"Columbus Day (Indigenous Peoples' Day)", "Columbus Day", "CIVIC " MD " HISTORICAL", G_NATIONAL,
     "Marks Christopher Columbus's arrival in the Americas on 12 October 1492 and became a permanent Monday federal holiday in 1971. Many states, cities and federal proclamations also recognize it as Indigenous Peoples' Day, honoring Native American histories and cultures.",
     "Federal offices and courts, the Post Office, and the Federal Reserve and commercial banks close. NYSE and Nasdaq stay OPEN for normal trading, though bond markets usually close. Most private businesses are open, and school closures vary by state and district.",
     "Some cities, such as New York and Chicago, hold Italian-American heritage parades, while other places host Indigenous Peoples' Day powwows, cultural showcases and educational events. Most workplaces run as a normal Monday.",
     SRC_OPM},
    {"Veterans Day", "Veterans Day", "NATIONAL " MD " MEMORIAL", G_NATIONAL,
     "Proclaimed Armistice Day by President Wilson in 1919 for the end of the fighting in World War I at the 11th hour of the 11th day of the 11th month of 1918. Congress renamed it Veterans Day in 1954 to honor all living and deceased U.S. veterans.",
     "Federal agencies and courts, the Post Office, and the Federal Reserve and banks close on a weekday Veterans Day. NYSE and Nasdaq stay OPEN. On a Saturday, federal employees get the Friday off while the exchanges, the Federal Reserve and the Post Office run normally. Most private businesses stay open; schools vary.",
     "Ceremonies at Arlington National Cemetery include a wreath-laying at the Tomb of the Unknown Soldier at 11:00 AM. Schools hold assemblies, and many restaurants and shops offer free meals or discounts to veterans and service members.",
     SRC_OPM},
    {"Thanksgiving Day", "Thanksgiving", "NATIONAL " MD " HERITAGE", G_NATIONAL,
     "Rooted in 17th-century harvest celebrations, most famously the 1621 Plymouth feast, and proclaimed a national holiday by President Lincoln in 1863. Congress fixed it to the fourth Thursday in November in 1941. It is one of the busiest travel and family days of the year.",
     "Nearly everything closes: federal and state offices and courts, the Post Office, the Federal Reserve, banks, NYSE and Nasdaq, schools, and most companies and big retail chains. The exchanges also close early, at 1:00 PM ET, on the Friday after.",
     "Families gather for roast turkey with stuffing, cranberry sauce and pumpkin pie. Millions watch the Macy's parade and NFL games, charities serve community meals, and Black Friday sales begin that evening or the next morning.",
     SRC_OPM},
    {"Christmas Day", "Christmas Day", "CHRISTIAN " MD " NATIONAL", G_CHRISTIAN,
     "Celebrates the birth of Jesus Christ for Christians and is a major winter holiday across the whole country. A federal holiday since 1870, it is marked with gift-giving, family gatherings and church services.",
     "Virtually all federal and state offices, schools, banks, the stock exchanges, the Post Office and almost every shop and business close. When 25 December is a Saturday, federal offices and the exchanges close on Friday 24 December, while the Federal Reserve and Post Office run normally.",
     "Families exchange gifts around decorated trees, attend Christmas Eve or Christmas Day services, share festive meals and watch NBA Christmas games and holiday films, with almost every shop closed.",
     SRC_OPM},

};

// The federal holidays of a year. A fixed-date holiday on a Saturday is observed on the Friday before, on a Sunday on the Monday after
// (so New Year's Day of next year can put a day off on 31 December). The real date stays in the list as a kind 2 marker.
std::vector<Holiday> genUS(int Y) {
    std::vector<Holiday> v;
    auto fixedDay = [&](int yy, int m, int d, int id) {
        const int w = astro::dayOfWeek(yy, m, d);
        if (w != 0 && w != 6) { if (yy == Y) v.push_back(mkHoliday(yy, m, d, id)); return; }
        int oy = yy, om = m, od = d; addDays(oy, om, od, w == 6 ? -1 : 1);
        if (oy == Y) v.push_back(mkHoliday(oy, om, od, id, 1, yy, m, d));
        if (yy == Y) v.push_back(mkHoliday(yy, m, d, id, 2, oy, om, od));
    };
    auto monday = [&](int m, int wd, int n, int id) { v.push_back(mkHoliday(Y, m, nthWeekday(Y, m, wd, n), id)); };
    fixedDay(Y, 1, 1, H_US_NEWYEAR); fixedDay(Y + 1, 1, 1, H_US_NEWYEAR);      // next year's can be observed on 31 December of this one
    if (Y >= 1986) monday(1, 1, 3, H_US_MLK);
    monday(2, 1, 3, H_US_PRESIDENTS);
    monday(5, 1, -1, H_US_MEMORIAL);
    if (Y >= 2021) fixedDay(Y, 6, 19, H_US_JUNETEENTH);
    fixedDay(Y, 7, 4, H_US_INDEPENDENCE);
    monday(9, 1, 1, H_US_LABOR);
    monday(10, 1, 2, H_US_COLUMBUS);
    fixedDay(Y, 11, 11, H_US_VETERANS);
    monday(11, 4, 4, H_US_THANKSGIVING);
    fixedDay(Y, 12, 25, H_US_CHRISTMAS);
    return v;
}
