MERIDIAN 2.9.7 "Snowfall"  -  Sonnet Edition  -  an always-on clock for Windows
=============================================================================

Source code. Build with build.bat (Windows, MinGW-w64) or build.sh (Linux cross-compile). The result is one
Meridian.exe; put the "themes" folder next to it to use theme packs.
The setup wizard (Meridian-Setup-2.9.7.exe) is built from setup\ (a small C++ program, no installer framework): see setup\BUILD.txt.

Faces (Left/Right arrows, or 1-6; theme packs add more after Focus)
  Sky      big clock, a rotating left panel (what is special today, sky events, holidays,
           world clocks), a live sky map with the Sun, Moon, planets and constellations in their real
           positions, day/night band, hourly weather and air quality badge
  Quiet    just the time and date, with the weather drifting behind them (clouds, wind, rain, snow, fog, a tornado)
  Timer    countdown, stopwatch with laps, alarm
  Almanac  Moon close-up and phase dates, full sun table with Golden & Blue Hour, planets, 7-day forecast, what is special today
  Widgets  pages of tiles that take turns, like the rotating panel on the Sky face (see below)
  Focus    lo-fi focus sanctuary with an independent focus timer (25m default), smaller live clock, live weather card and ambient backdrops

Themes (T)
  Auto, Day, Sunlit, Sunlit II, Dawn, Sunset, Dusk, Night, Midnight, Ember. Dawn is the pre-sunrise counterpart of Dusk: deep blue at the top,
  rose and peach toward the horizon. Sunset is the orange one: wine red at the top burning to bright orange at
  the bottom, with cream text. Auto uses Dawn before sunrise and Dusk after sunset (pick Sunset yourself).
  The sky map on the Sky face wears the theme too (new in 2.9.1): Sunset is orange at the rim going up into plum, Dusk soft violet, Dawn
  rosy blue, Night deep navy, Sunlit and Sunlit II their own blues, and so on. The rim ring takes the sky's colour. Late in the evening the
  sky deepens into the theme's night colours and the stars come out; dark themes keep a few faint stars even at noon. Auto is as before and
  follows the real Sun. Theme packs get a sky made from their background colours, or set "skyTop" and "skyHorizon" in theme.json.
  Sunlit (new in 2.4.0) is the second daylight theme: a clear blue sky fading to a warm cream horizon, navy text, an orange
  accent. Day stays the plain light mode; Sunlit is made for the sun flare (below), and there the Sun glows ALL DAY.
  Sunlit II (new in 2.5.0) is a richer daylight: a deeper azure overhead burning down to a golden horizon, deep navy text, a
  tangerine accent and frosted panels. Behind the Quiet and Sky tabs, soft rays of sunlight fan out from the Sun (the same
  place the flare follows) and a few wisps of high cloud drift past. Sunlit stays exactly as it was.
  Midnight (new in 2.5.0) is the night counterpart: near-black blue, pale lavender-white text, a lilac accent. Behind the Quiet and
  Sky tabs it draws a deep night sky: dim stars (a few twinkling), a faint band of the Milky Way, soft purple and teal nebula,
  a ringed planet low on the left and a far galaxy at the top right. Every few minutes a meteor streaks through, now and then a
  comet with a long coloured tail crosses the sky, and once each evening (19:00-23:00, or press U) an unhurried flying saucer
  glides across with its own stereo-panned soundtrack following it from speaker to speaker (new in 2.9.3). Cloud, fog, rain and
  storms put the stars out as the weather says.
  Settings > Themes > "Midnight sky" switches all of the Midnight scenery off (the colours stay).
  The scenery is only on the Quiet and Sky tabs, like the sun flare; Timer, Almanac and Widgets stay clean. Between events the
  window only redraws when something moves, so Midnight idles as cheaply as Night.

Your location (new in 2.1.0)
  Settings > Place > FIND MY LOCATION asks Windows where you are (GPS, or nearby Wi-Fi networks) and uses that for the sky,
  sunrise and sunset, the Moon, storms and the weather. If Windows location is off or the PC cannot tell, it asks your internet
  address instead: that is only city level and can be off by a whole city, so the screen says which one it used and how precise it is.
  The answer is kept in settings.ini and is NEVER looked up again by itself: only pressing the button asks again.
  The place on show is remembered too (the "showing=" line in settings.ini): a saved place you picked, or one you typed by hand, is
  still there after a restart, and the automatic first-start lookup never replaces it.
  A brand new install (no settings.ini yet) looks once at its very first start; if that fails (for example no internet) it tries
  again at the next start, and never after the first success.
  Windows has to allow it: Settings > Privacy & security > Location, with "Location services" and "Let desktop apps access your
  location" on (the button OPEN WINDOWS LOCATION SETTINGS in the app jumps there when a lookup fails). You can still type a place by hand
  (latitude and longitude) or add saved places and switch between them with N.

Theme packs (new in 2.0.0)  -  themes/README.txt has the whole format
  A theme pack is a FOLDER inside the "themes" folder next to Meridian.exe (or %APPDATA%\Meridian\themes).
  Drop one in and Meridian picks it up within a few seconds, no restart. Delete the folder and it is gone.
  A pack can bring its own tab, colours, fonts, pictures and animated elements, all from one theme.json text
  file with notes in it. The built-in themes and faces are compiled into the program and are never touched.
  A pack only adds its OWN tab by default: Sky, Quiet, Timer, Almanac and Widgets look exactly as before.
  (A pack that says "global": true in its theme.json can also be used as a theme for every tab.)
  A pack always looks the same: the weather (cloud, fog) never changes its colours. (Unless its theme.json says "weather": true.)
  Both packs below are ALREADY in the themes folder that comes with this download. Nothing to copy: unzip, run
  Meridian.exe, and press the Right arrow past Widgets to reach their tabs.
  Included 1: GTA VI, a neon-sunset tab counting down to the 19 November 2026 release: a detailed Art Deco city with
  palm trees, a turning Ferris wheel and a lighthouse, boats on the sea, a police helicopter patrolling the roofs, one
  far-away plane, and six police stars that fill up as the date gets closer (2 days left = 4 stars) and then blink red and
  blue once the game is out. A plane pulling a banner flies BEHIND the big number and the clock digits. A second view
  (click the pill at the top, or press Tab) is a LAUNCH MAP: the world lights up from east to west, country by country,
  as each of the 14 rolling-midnight launch waves goes live (times shown on your clock), and the whole map is lit when the
  last one does. Click or hover a wave card to look at it. The art is original; it uses no logo, key art or characters.
  The release date is Rockstar's announced one, counted down to midnight where the clock is; the regional times are the
  rolling-midnight schedule reported from PlayStation Store data, not an announcement by Rockstar.
  Remove GTA VI later: delete the folder themes\gta6 (or switch it off in Settings > Themes). It also hides
  itself on 20 December 2026 (its "expires" line; delete the line to keep it).
  Included 2: Halloween, a haunted night tab: a huge orange moon behind the time, ghosts, bats, a witch and her cat
  dragging a banner behind the numbers, a haunted house with flickering windows, jack-o'-lanterns, a spider on its thread,
  zombie hands, eyes in the dark, lightning, ground fog. It counts down to Halloween, then (on 31 October) to midnight when
  "the spell breaks", with extra bats and confetti on the day. Original art. Remove it: delete themes\halloween
  (or switch it off in Settings > Themes). It hides itself on 3 November and comes back on 1 October every year.
  Make your own: copy themes\_template, rename the copy, edit theme.json, save. Settings > Themes lists
  every pack, why one failed to load (with the line to fix), and has OPEN THEMES FOLDER and RESCAN.

The dock
  Rests out of sight. Move the mouse to the bottom edge and it slides up; leave and it slides away.

Widgets face
  14 widgets: World clocks, Month calendar, System, Note, Countdown, Moon phases, Week ahead (5-day forecast),
  Today (what is special today), Holidays (the next holidays of the countries you chose), Weather (now + next 12 hours), Daylight
  (day length, sunrise, solar noon, sunset, the Sun's path through the day), Sky tonight (Moon, planets up,
  next sky event, Sun), Conditions (humidity, wind, pressure, UV, air quality, cloud cover), Year (progress,
  week number, next equinox or solstice).
  They come in pages of 1-6 (default 2). Each page stays 15 s (4-300 s), then the next one arrives with the
  same Slide / Rise / Fade transition as the Sky panel. Tab / Shift+Tab or the little dots under the clock
  skip; the page holds while the pointer is over it (it lets go after about 8 s of the mouse sitting still).
  Put everything on one page and nothing rotates.
  Pick the widgets, their order and their pages in Settings > Widgets and Settings > Rotation.
  Settings files from the previous version get the new widgets added once, so the face has pages to cycle.

Quiet face weather
  Reads the current weather and draws it behind the clock, nothing else changes:
    clouds   when more than about a third of the sky is covered (more cover, more clouds). Lit tops, darker
             bellies, drifting with the wind. One big cloud also passes IN FRONT of the time, as a soft veil
             that never hides it.
    wind     from about 12 km/h: bright particles with a rounded head, each dragging a fading tail with a sharp
             core line and a few sparks, in three depths like the rain (near ones bigger, brighter and faster), blown across in
             the direction the wind goes, surging in gusts and faster as it picks up. Clouds and wind together show both.
    fog      a soft haze drifts in front of the sky when fog or mist is reported, and the light turns flat
    rain     a few small clouds come with it (also at night), then streaks in three depths, slanted by the wind, over a wet
             ground that runs away from you (far drops land higher up the screen, near ones lower). Every drop that lands
             splashes: a ring that opens out, a flash and a small spout, and a crown of droplets thrown up that fall back.
             Mist along the bottom. A few drops fall IN FRONT of the time and date on the Quiet tab; the rest stay behind.
             Since 2.5.0 it is drawn from pre-made pictures (each streak and splash is pixel-sharp and the screen needs far
             fewer draw calls), and it also rains on the Sky tab, behind the map and every label, so the info stays readable
             (Settings > Quiet > Rain on the Sky tab).
    thunder  heavy dark cloud with lightning whenever thunder is reported: the bolts come out of the visible clouds and
             light them up from inside, with a soft flash. Rain only if rain is reported too, so thunder with cloud and no rain is its own
             scene (dry lightning); thunder with rain shows both.
    tornado  a tornado twists up from the ground, with lightning, for very strong gusts: about 70 km/h and
             more, or 55 km/h and more during thunder (rain, wind and cloud come with it)
    sun flare  new in 2.3.0, also on the Sky tab since 2.4.0. In the morning and the evening, while the Sun is low, a soft golden flare
             comes in from the side the Sun is on: the left in the morning, the right in the evening (the sky map has east on the
             left too). A warm glow at the edge, a thin streak and a few faint ghosts, behind the clock. On the Sky tab it sits
             behind the clock and around the dome, never over it. It fades as the Sun climbs or sinks, and cloud, fog and rain take
             it away. In the SUNLIT theme it stays all day: from the left edge in the morning, across the top at noon (soft), to the
             right edge in the evening. Only the Quiet and Sky tabs have it; Timer, Almanac and Widgets never do.
             Settings > Quiet > Sun flare switches it off, and the SUN FLARE preview shows it at any hour.
    sunny / calm  nothing else
  The rain, wind, cloud, fog, thunder and tornado are on the Quiet face (rain also on the Sky face). Settings > Quiet switches them off, sets the Strength (50% to 200%) and previews each
  effect (LIVE, CLOUDS, WIND, BOTH, RAIN, FOG, THUNDER = thunder with no rain, T-STORM = with rain, TORNADO, SUN FLARE).
  Resizing the window no longer makes the clouds jump: they keep their place and just stretch with the window.
  Smooth motion (new in 2.5.0, Settings > Quiet > Smooth motion, on by default): rain, wind, thunder and tornado are paced to
  your screen's own refresh rate, 60 to 90 frames a second depending on the display (never more), with a high-resolution timer instead of the old coarse one,
  so motion is even and nothing stutters. If the display does not wait for the refresh itself (a remote desktop, say), Meridian
  notices and paces itself by the clock. Clouds alone redraw at 15 frames a second. Switch Smooth motion off to save power:
  rain and wind then run at 30 frames a second.
  Built to sit on all day: the effects are soft (thin streaks, gentle lightning with no full-screen flashes), the clouds are
  drawn once and then just slide, and if a PC cannot keep up, it quietly draws fewer streaks and drops and works back up when it
  can. Strength 50% makes it fainter still.

Weather changes the light (new rules in 2.2.0)
  Cloud, storms and fog change the light of the built-in themes, gradually (it follows the real cloud cover, fog and
  thunder) and on every face:
    Day, Sunlit, Sunlit II   a heavy cloud turns the bright page into a slate overcast, fog makes it flat and pale (as before).
    Midnight starts as deep as Night and keeps its stars out of the cloud, fog and rain.
    Night    gets DEEPER: blue-black, darker than a clear night, never grey or milky. The clouds and fog drawn on the Quiet
             face become dim shapes at night instead of pale glowing ones.
    Dusk, Dawn, Sunset   show MORE of their own colours: under cloud the sky turns a rich violet above and a glowing
             orange below (dawn: indigo to orange-rose), instead of fading to grey. Auto mode glides through the same.
  Theme packs (GTA VI, Halloween ...) are NOT touched by the weather at all: colours and scenes always stay exactly
  the same. A pack that wants the weather says "weather": true in its theme.json.
  The text keeps its contrast while the sky changes, so the clock and date stay readable in any weather.
  Settings > Themes > "Light follows the weather" switches it off or sets how strongly.

Named storms
  When a named tropical storm (cyclone, typhoon, hurricane) is within 1500 km of your place, its name,
  distance and direction show in three places: quietly under the date on the Quiet face, in the header of the
  Sky face between your place and the time zone, and as the first item of the Today list (Sky panel and
  Today widget). Example: "TROPICAL STORM MONTHA . 830 KM NE . WINDS 100 KM/H".
  Reports come from GDACS (gdacs.org), with NASA EONET (eonet.gsfc.nasa.gov) as a backup: free, no key, one
  small download every 30 minutes for the whole world; the nearby ones are picked on your PC. Nothing about
  you is sent except the request itself. Settings > Quiet > Named storms switches it off or changes the
  radius (300 to 4000 km). Settings > Quiet and Settings > Weather show when the storm reports were last
  fetched and from which source (GDACS or NASA EONET), with a REFRESH button to fetch them again now. The
  result is also written to weather.log (lines starting with "Storms:").
  Names, positions and winds are only as good as those agencies' latest advisory.

Sky map rotation (Settings > Sky map)
  Face any direction: tap N, NE, E ... or type an angle (0-359, 124 for example) or click the little
  dial. The compass direction you choose is at the top of the map; the whole sky, the compass letters, the
  Moon's tilt and the twilight glow turn together, so everything stays correct.
  Free rotate: the sky turns slowly and continuously, one full turn every N seconds (default 2 minutes),
  clockwise or counter-clockwise. Turning it off keeps the map facing wherever it stopped. Free rotate redraws
  30 times a second, which uses a little more power than the usual once a second.

Settings (press the comma key, or SETTINGS in the dock)
  Widgets      switch widgets on/off, reorder them, pick world-clock cities, write a note, set a countdown,
               add your own dates, pin one widget to the Sky face in place of the Today list
  Elements     switch parts of the Sky face on/off (today list, weather, day band, names, constellations,
               Milky Way, ecliptic, clouds, tracks), seconds, 24-hour, Fahrenheit, theme, brightness ...
  Themes       theme packs found in the themes folder: on/off, what each one changes, when it expires, errors;
               "Light follows the weather" and its strength
  Rotation     Sky face: which panels rotate and how long each stays. Widgets face: widgets per page and how long
               each page stays. Shared: Slide / Rise / Fade, pause on hover, page dots, calendar holidays
  Sky map      heading (buttons, typed angle, dial) and free rotate
  Quiet        weather behind the clock on/off, strength, preview of each effect, named storms on/off and radius,
               when the storm reports were fetched, from where, and a refresh button
  Sky events   meteor showers, planets lining up, solar and lunar eclipses; shown on the sky map only
               when they are N days away or less (default 5). Hover one for the details.
  Weather      when the weather was last refreshed and which source it came from (plus the storm reports),
               four free sources tried in order until one answers; status of each; refresh now
  Place        home location, saved places, add a place
  Holidays     whose holidays to show: Sri Lanka, the United Kingdom, the United States, any mix (or none), picked by your place or by hand
  Away         turn Meridian on when the PC is not used (it becomes your screen saver, after 1-60 minutes) and then put the PC to sleep
               after another 20-120 minutes; a small timeline shows how it plays out (see "Away: Meridian on, then sleep" below)
  About        the version number, open settings.ini / the weather log, shortcuts, and ABOUT FEATURES / WHAT'S NEW

What is special today
  The Today panel (Sky face, Today widget, and the Almanac) lists what is special today: Poya and public
  holidays (of the countries you chose), eclipses, meteor showers, planet meetings, equinoxes, solstices, named storms. On a quiet day it
  never goes empty: it fills up to three lines with the Moon phase, the next holiday, the best planet this
  evening, the length of the day and the day of the year.

Rotating panel (left side of the Sky face)
  Four panels take turns: Today, Sky events, Holidays and World clocks. Each one slides in as the last
  slides out (Settings > Rotation lets you pick Slide, Rise or Fade, and how long each stays, 4-120 s, default 12).
  Tick one panel to keep it on screen, or several to rotate. Press Tab (Shift+Tab for back) or click the page
  dots to skip. It pauses while the pointer is over it, so a card you are reading does not leave.

Holidays: Sri Lanka, the United Kingdom, the United States (new in 2.9.0)
  Settings > Holidays chooses whose holidays Meridian shows. The choice is the same everywhere: the Today list, the Holidays panel and widget,
  the month calendar and the hover cards. Switch on Sri Lanka and the UK and you get both lists; switch on all three and you get all three.
  Every holiday carries its country (LK, UK or US) on its row and on its card.
  Pick by my place (on by default): Meridian shows the holidays of the country your place is in. FIND MY LOCATION (Settings > Place) learns the
  country from the lookup; a place you typed in is told by its position (Sri Lanka) and otherwise by your Windows region. A place in any other
  country gets no holidays at all, and the Holidays panel says so instead of showing the wrong country's days. Switch Pick by my place off to
  choose the countries yourself (the place is then ignored; you can even switch all three off).
  A holiday that countries share is shown once. Christmas Day is one entry marked International, not one per country, and its card says who
  gets the day off in each of them. Two days merge only when they really are the same day: Independence Day stays US, and a Christmas that
  falls on a weekend and moves to a different Monday in the UK and the US stays two entries, each with its own day off.
  United Kingdom: the bank holidays of England and Wales, Scotland and Northern Ireland (New Year's Day, 2nd January, St Patrick's Day, Good
  Friday, Easter Monday, the two May ones, the Twelfth, both Summer ones, St Andrew's Day, Christmas Day, Boxing Day, and Scotland's one-off
  World Cup day on 15 June 2026). Settings > Holidays > UK list for: Auto (from your place; a Scottish or Northern Irish place gets its own
  list, anywhere else England and Wales), or pick one. A holiday that falls on a weekend shows the substitute weekday it moves to.
  United States: the eleven federal holidays (New Year's Day to Christmas Day). When one falls on a Saturday the Friday before is the day off,
  on a Sunday the Monday after (the card says so, for example Independence Day observed on Fri 3 Jul 2026).
  The UK and US dates follow the law's rules and match the official 2026, 2027 and 2028 calendars; later years are worked out from the same
  rules and the card says so. The descriptions (what it is, who gets the day off, how it is observed) come from the sheet you provided.
  holidays.txt, below, replaces Sri Lanka's dates only.

Sri Lankan holidays
  The Holidays panel and widget list the next ones; hover a row for the full card. The Month calendar widget
  marks Poya days and public holidays with a dot, has < > arrows and a TODAY button to browse months, and
  hovering a date shows each holiday: what it is (for example what Vesak is), who gets the day off (public,
  bank, mercantile), how it is observed, and where the date comes from. Turn the calendar marks off in
  Settings > Rotation. Every holiday is shown as starting at 12:00 AM; when the full moon falls later in the
  day, the Poya entry says when ("Full Moon tomorrow at 9:42 AM") so the clock time is not mistaken for the start.
  Dates: the official 2026 list (Government of Sri Lanka holiday calendar) and the 2027 list as reported by
  tamilguru.lk from the gazette, a secondary source, so check the 2027 mercantile flags against the gazette.
  Other years are estimated: Poya days are worked out from the Moon (the day the full moon begins, or the next
  day when it begins after 6 pm), Good Friday from the Easter rule. Eid, Deepavali and Maha Sivarathri are not
  estimated, so they appear only for 2026 and 2027. Poya and Eid dates can still move by a day when gazetted.
  When a newer list is published (new in 2.8.1): type its dates into holidays.txt (Settings > Rotation > OPEN HOLIDAYS.TXT creates the file,
  in %APPDATA%\Meridian next to settings.ini, and opens it). One holiday per line, the date as year-month-day and then its name:
      2028-01-12  duruthu
      2028-06-07  poson  adhi        (the word adhi marks an extra leap-month Poya)
  A line replaces that one holiday in that year (or adds it when the year has none); every other holiday keeps its date. A line starting
  with # is a note. The file lists all the names it understands. If you move Vesak, the day after it moves too unless you give
  vesak-after yourself. Save the file and Meridian shows the change within a few seconds, no restart. A line it cannot read is skipped and
  named once (as a message, and under the button); the rest is used. Hovering a date then says "Your holidays.txt" as the source.

Sky events (all worked out on your PC, no internet)
  Meteor showers draw falling streaks from the real radiant point; planet meetings and parades are
  linked on the map; eclipses show the Sun or Moon being covered. Hover for time, visibility, Moon conditions.

Keys (press ? or F1 in the app for the same list)
  ,  settings            T theme            Left/Right or 1-5  face       F / F11 / double-click  fullscreen
  Space start/pause      R reset   L lap    0-9 type a time on the Timer   + / -  adjust
  S seconds   C degC/degF   H 12/24 hour    P keep on top   K keep screen awake (ON by default; lets go while on battery)
  [ ] dim/brighten (mouse wheel too)         B red Ember theme at bedtime    W refresh weather   N next place
  Tab / Shift+Tab next / previous panel on the Sky face, next / previous page on the Widgets face
  M burn-in pixel shift                      G open settings.ini
  Right-click (or a long press on a touch screen) opens a menu: face, next theme, fullscreen, keep on top, keep awake, burn-in shift,
  Dim with the sun, Start with Windows, settings, shortcuts, exit.

Screen saver (new in 2.6.0)
  Meridian can be your Windows screen saver: after a few minutes without touching the keyboard or mouse it starts by itself, full
  screen, and shows the clock. Any key, click, wheel turn or mouse move closes it.
  To set it up: unzip the whole Meridian folder somewhere you will keep it, then double-click Install-ScreenSaver.bat. It makes
  Meridian.scr (a copy of Meridian.exe; Windows only starts screen savers called .scr) and tells Windows to use it after 5 minutes.
  A different wait: run it from a command prompt with the minutes (1-60), e.g.  Install-ScreenSaver.bat 10   (or change it later in Windows:
  Settings > Personalization > Lock screen > Screen saver settings). Try it at once by double-clicking Meridian.scr.
  If you move the folder, run Install-ScreenSaver.bat again. Remove-ScreenSaver.bat undoes it and puts back the screen
  saver you had before (Install remembers it the first time). No administrator rights are needed.
  What it shows: Meridian exactly as you left it (same tab, theme, place, weather, burn-in shift). It does not save anything to your
  settings and does not hold the screen awake, so Windows still turns the screen off when its own timer says so. A second or third
  monitor goes black. The little preview in Windows' Screen Saver Settings box stays blank. "Settings..." there opens Meridian itself.
  Meridian.exe, started the usual way, is unchanged. (Under the hood: Windows starts "Meridian.scr /s"; the code is in src/saver.cpp.
  Starting Meridian.exe with /s shows the screen saver too, which is handy for a shortcut of your own.)
  How it starts (new in 2.9.0): it no longer pops onto the screen. The screen is black, a soft circle of light opens from the middle out to
  the corners while the whole picture fades up, and the clock settles from a touch zoomed in to its true size, all in about two seconds.
  Any key, click or mouse move ends it, in the middle of the animation too, and it leaves the same way it came: the corners close in and the
  picture fades to black in about half a second, and then the desktop is back (the clock ignores clicks and keys while it fades). It is only for
  the screen saver: Meridian.exe started by hand opens as before. (src/reveal.h has the numbers; a test build can freeze them for a picture with
  --reveal 0.4 and --leave 0.4.)

Away: Meridian on, then sleep (new in 2.8.0)
  Two switches, in Settings > Away and on the setup's "While you're away" page (both change the same things):
    Turn Meridian on when I am away   After 1-60 minutes of not touching the keyboard or mouse, Meridian starts full screen as your screen
                                      saver (see above). Any key, click or mouse move closes it. Switching it on does what Install-ScreenSaver.bat
                                      does for the folder Meridian.exe is in (a copy called Meridian.scr, Windows told to use it, your earlier
                                      screen saver remembered); switching it off puts the earlier one back.
    Then put the PC to sleep          If nobody comes back within another 20-120 minutes, Meridian closes and Windows goes to sleep. For example
                                      10 and 60: the PC sleeps after 70 minutes without use. The sleep timer does not count time the PC itself
                                      spent asleep. It only runs while Meridian is the screen saver, so it needs the first switch. If the PC
                                      cannot sleep (switched off by a policy or a driver), Meridian just closes.
  The two times are Windows' own screen saver wait (Control Panel\Desktop, ScreenSaveTimeOut) and, for the sleep, a value Meridian reads when it
  runs as the screen saver: HKCU\Software\Meridian\Saver, SleepAfterMinutes (0 or missing = no sleep). It is in the registry rather than in
  settings.ini so that the setup can set it without creating a settings file.
  Good to know: Windows' own timers (Settings > System > Power) still apply, so if Windows turns the screen off or sleeps sooner than your
  times, it wins; Meridian does not keep the PC awake while it is the screen saver. If you put a newer Meridian.exe in the folder, switch Away
  off and on again (or run the setup) so that Meridian.scr is the new version too. Remove-ScreenSaver.bat forgets the sleep time as well.
  Not tried on a real PC by the author: the sleep itself (Windows' SetSuspendState) was only checked with a test hook, so tell me if a PC does
  not go to sleep.

Setup file (Meridian-Setup-2.9.7.exe, a separate download next to this zip)
  If you would rather have a real installer than a folder, run Meridian-Setup-2.9.7.exe. It is a 64-bit graphical wizard drawn by Meridian's own
  code (same fonts, same Moon), one file with nothing unpacked beside it, and it needs NO administrator rights: it installs for you alone into
  %LOCALAPPDATA%\Programs\Meridian (or a folder you pick). The pages:
    Welcome        greets you by name, with tonight's real Moon (phase and all) drawn from the same astronomy code as the app
    Make it yours  the folder, a Start menu shortcut, a Desktop shortcut, open Meridian when I sign in; the GTA VI and Halloween theme
                   packs are two chips you can untick (Customise, optional: Welcome's Next goes straight to the next page)
    While you're away   two switches: turn Meridian on when the PC is not used (1-60 minutes; it does exactly what Install-ScreenSaver.bat
                   does, and your old screen saver is remembered), and then put the PC to sleep after another 20-120 minutes. A little
                   timeline shows what happens when. Both are off until you switch them on (the second needs the first)
    What's new     the release notes, scrollable
    Installing     the Moon fills up as the files go in
    All set        open Meridian now, or just close it
  Meridian is listed in Settings > Apps. Uninstalling there (or running uninstall.exe from the install folder) shows a Moon that wanes, puts your
  old screen saver back (only if it is still Meridian's), takes away the shortcuts and the start-up entry, and asks whether to keep your
  settings, places and notes (your own theme packs are always left alone).
  Run the setup again (or open uninstall.exe in the install folder) once Meridian is installed, and its first page offers Update and Uninstall:
  Update brings an older Meridian (2.6.x too, also one made by the old NSIS setup) up to date, or puts the files back, and keeps your settings
  (one click; its away times are kept too, and Customise > Next > While you're away changes them);
  Uninstall is the same as in Settings > Apps (it asks about your settings); Customise and What's new are links beside the buttons.
  Not code-signed: Windows may say "Windows protected your PC": click "More info", then "Run anyway". Windows 11's Smart App Control, if it is
  on, can refuse any unsigned program with no "Run anyway" button; then use the plain Meridian.exe from the zip, turn Smart App Control off
  (Windows Security > App & browser control), or sign the setup with a code-signing certificate (see setup\BUILD.txt).
  For scripts: Meridian-Setup-2.9.7.exe /S installs silently with the defaults (exit code 0 = fine, 2 = failed, 3 = Meridian would not close,
  4 = it is already running);  /D=C:\Tools\Meridian picks the folder;  /ScreenSaver=5 also sets the screen saver
  (5 minutes, 1-60);  /SleepAfter=60 also puts the PC to sleep after Meridian has been on 60 more minutes (20-120; it turns the screen saver on
  too, 10 minutes unless /ScreenSaver says otherwise);  /NoSleep takes the sleep step away;  /StartWithWindows opens it at sign-in;
  /NoDesktop  /NoStartMenu  /NoGTA  /NoHalloween leave those out.
  Uninstall from a script: "<folder>\uninstall.exe" /uninstall /S  (add /DeleteSettings to remove settings too).
  Building the setup yourself: see setup\BUILD.txt in the source.

New in 2.9.7 "Snowfall"  (21 November 2026)
  Snowfall, Focus Studio, Daylight Saving awareness, shooting-star physics, Air Quality badge, Golden & Blue Hour, and QOL polish:
  - Calm snowfall: three depths of gently swaying snowflakes, soft foreground bokeh and a faint frosted ground glow now
    appear behind both the Quiet and Sky faces whenever snow is falling (plus a SNOW preview chip in Settings > Quiet).
  - Dedicated Focus tab (key 6): brand-new lo-fi Focus Studio tab with its own independent focus timer (default 25m
    Pomodoro), smaller live clock + date card, live weather card with icon & AQI, frosted focus medallion with calm
    breathing halo, and a collapsible side Ambience & Theme orb that smoothly expands on hover (Off, Live, Rain, Snow, Clouds, Fog, Storm, Stars)
    — leaving the classic Timer tab completely untouched.
  - Daylight Saving Time (DST zones only): automatic 2 AM DST transitions, per-hour DST-accurate forecasts, a subtle
    DST header tag only when DST is active, a 48-hour clock-change notice in Today, and per-year Windows Dynamic DST
    across 31 World Clock cities — leaving non-DST countries completely untouched.
  - Weather resilience, Air Quality & Golden/Blue Hour: hardened WinHTTP networking (longer timeouts, forecast/AQI
    staggering, and TLS 1.2 + HTTP/1.1 retry to eliminate E_ABORT 2147500036 drops), 4 new weather fallback APIs
    (Bright Sky / DWD, NWS / NOAA, WeatherAPI.com, OpenWeatherMap) for 8 total sources, multi-source Air Quality
    fallback with a custom air-waves emblem badge on Sky, frost/wind-chill/heat/UV cues, and Golden & Blue Hour in Almanac.
  - Living night sky, 42-constellation atlas & compass fix: natural compass East/West orientation on the Sky dome
    (N up, E right, S down, W left), deeper obsidian Midnight/Night/Ember dome skies, and 42 accurate year-round
    constellations across both hemispheres with spectral star colors, trimmed filigree lines and diamond glints —
    visible strictly at night by default with an optional daytime toggle in Settings > Sky Map; shooting stars ignite,
    flare and stream in a 6-week alternating loop; the Midnight saucer sports a 3x supersampled sculpted model.
  - QOL & precision polish: fluid liquid-glass pill-to-dock hover expansion (hovering the bottom-center pill
    smoothly morphs it into the navigation dock at screen refresh rate without accidental corner triggers),
    optional Soft hourly chime (off by default in Settings > Elements), optional Saved place local time in
    Settings > Place, global Celsius & km defaults, optically centered circular panel arrows, custom vector
    Back button in About, and case-insensitive hidden soundtracks and jukebox controls in Settings > About.

New in 2.9.6 "Compass"  (21 November 2026)
  Easier panel navigation, clean privacy defaults, and hidden soundtracks inside About:
  - Theme-adaptive panel arrows: crisp previous (<) and next (>) buttons now flank the page dots on both the Sky
    panel and the Widgets face with generous click targets so you no longer need pixel-perfect aim. They automatically
    adapt to all ten built-in themes, Auto lighting, weather moods, Ember red-night tint and custom theme packs.
  - Privacy and neutrality: default coordinates now start at the Prime Meridian (Greenwich) with no author names,
    personal hometowns or local build paths left in the binaries, installer or source tree.
  - Hidden jukebox: Settings > About quietly accepts classic no-textbox cheat codes — compactly stored as AAC and
    streamed straight from memory — for both the full unedited stereo track and the installer soundtrack.

New in 2.9.5 "Anchor"  (21 November 2026)
  Hardening and accessibility pass from an independent Windows review:
  - Transactional upgrades: setup unpacks and verifies all files into a staging folder, backs up the old files before
    swapping, and restores the entire previous install if any file is locked. Moving to a new folder retires the old
    binaries, an old uninstaller will never delete a newer install's Apps entry, and silent uninstall waits for
    completion and reports the real exit code.
  - Weather, AQI, storms and saved places: air quality caches carry coordinates and fetch time (and clear when a city's
    air request fails); null temperature/weather-code responses are rejected so backup providers take over (while
    preserving real zeros vs missing optional fields); storms older than 72 hours expire during offline outages; and
    saved places with identical display names persist cleanly by coordinates as well as name.
  - Screen saver, privacy and Settings: screen saver mode keeps crash-restart suppression separate from the 10-minute
    retry counter; automatic first-start location lookup respects explicit Windows Location denial; Settings supports
    Tab / Shift+Tab / Space / Enter keyboard navigation and expands cleanly on compact 640x400 windows; dev builds
    output to build-dev\Meridian-dev.exe so CI always ships the clean release Meridian.exe.

New in 2.9.4 "Visitor"  (21 November 2026)
  A quiet way to hush the Midnight saucer, and fresh polish across the About page and setup:
  - If you ever decide that visitors from outer space simply do not exist, someone who knows the right words to whisper
    on Settings > About > About features & What's new (with no text box in sight) can make the saucer and its music
    vanish for good, and call them back whenever they change their mind.
  - The About page and the setup wizard now carry the release's name, new tips while installing, and the full version
    history all the way back to 1.x.

New in 2.9.3 "Flyby"  (21 November 2026)
  The little flying saucer in the Midnight sky now brings a tune of its own as it crosses your screen:
  - Embedded a lossless soundtrack directly inside Meridian.exe with real-time equal-power stereo panning that follows
    the saucer across your speakers — left to right or right to left — swelling as it passes the middle and fading out
    cleanly at the edges.
  - Slowed the saucer to an unhurried 9-second glide (~7 seconds visibly on screen) so you can enjoy both the saucer
    and the stereo sweep. Press U on Midnight (Sky or Quiet face) any time to summon it; silent in screen saver mode.

New in 2.9.2 "Clear"  (20 November 2026)
  A thorough round of fixes and polish across the clock, the weather, the sky, theme packs and the setup:
  - Fixed util::fmt() 512-byte truncation (which cut off Open-Meteo URLs and [quiet]/[storms] settings.ini writes),
    dangling lambda captures in Settings > Rotation and Sky Map, COM/Direct2D shutdown order, and duplicate countdown bounds.
  - Fixed weather provider parsing (7Timer light-snow/rain-rate, wttr.in UTC-10/11 Pacific offset, Open-Meteo air quality caching,
    daily grouping past 20:59), WinHTTP gzip/deflate decompression, proxy refresh, and immediate cancellation on shutdown.
  - Fixed astronomy nutation in the equation of time, polar sunrise/sunset day length, yearly meteor shower rollover, 3-planet
    conjunction rendering, global holiday isolation, multi-line Note editing, burn-in hover alignment, portrait/4:3 layouts,
    and the Halloween theme pack yearly countdown.
  - Hardened Meridian Setup: preserved CLI flags in GUI mode, fixed /D= parsing, atomic file replacement with rollback, safe
    reparse-point handling, safe temp-uninstaller cleanup, reduced idle CPU/GPU redraws, and clean audio thread shutdown.

New in 2.9.1 "Skylight"  (15 November 2026)
  Sky map colours: the round sky map on the Sky face used to be only day blue or night blue, whatever the theme. Now each theme paints its own
    sky (see "Themes" above), the rim ring is tinted to match, and the real time of day still deepens it into the theme's night colours with
    the stars out. Auto is unchanged. Weather still shows: cloud greys a light theme's sky, fog hazes it, twilight and night themes keep their
    colour.
  Theme packs: the palette in theme.json takes two new optional keys, "skyTop" and "skyHorizon" (see themes\README.txt). Left out, the sky
    is made from bgTop and bgBottom, so every existing pack gets a fitting sky with no change.

New in 2.9.0 "Reveal"  (15 November 2026)
  Holidays by country: Settings has a new Holidays tab. Sri Lanka, the United Kingdom (England and Wales, Scotland, Northern Ireland) and the
    United States each have their own list; switch on any mix, or let Meridian pick the country your place is in. A place in any other
    country shows no holidays. Every holiday carries its country code. See "Holidays" above.
  International holidays: a day that countries share (Christmas Day) is shown once and marked International, with who gets the day off in
    each country on its card. A holiday that falls on a weekend shows the day off it moves to.
  Screen saver: it starts with a graceful reveal instead of popping on (see "Screen saver" above).
  What's new: Settings > About > WHAT'S NEW now shows every version, newest first, and scrolls (mouse wheel, Up and Down, PageUp, PageDown,
    Home, End, or the bar). Before, it only showed the latest.
  Location lookups now also keep the country and region they report ([location] country= and region= in settings.ini).
  settings.ini gets a [holidays] section: auto=1, countries=lk,uk,us, uk_nation=auto.
  For people who build it: new tests/holidays_regions_test.cpp (dates, substitute days, merging, country choice) and tests/reveal_test.cpp,
    both in run_all.sh. The holiday lists live in src/holidays.cpp (Sri Lanka), src/holidays_us.h and src/holidays_uk.h.

New in 2.8.1 "Tidy"  (13 November 2026)
  A round of fixes from a second code review, plus a few things that make Meridian easier to look after. Nothing looks different.
  Drawing: if the graphics card resets (a driver update, a display change, waking from sleep) and the drawing surface cannot be rebuilt at once,
    Meridian draws nothing, keeps everything else running, and tries again about once a second, instead of staying blank for good.
  Keys: holding a key down no longer repeats it, except for the keys that adjust something (arrows, + and -, Backspace, the brightness
    brackets). Before, a held S, T, C, H, P, K, B, M, N, W, R or G flipped the setting back and forth, refreshed the weather over and over or
    opened settings.ini again and again. G now says one thing (opened it, or could not save first) instead of two.
  Mouse wheel and touchpad: small movements add up to one step per wheel notch (120), so a touchpad flick no longer drops the brightness to
    the bottom or moves a timer by minutes. Typing digits for a new countdown no longer stops and resets a running one: it asks you to pause first.
    The timer is one second at the shortest however you set it (typing, + and -, the wheel, settings.ini).
  Privacy: weather services get your position rounded to two decimals (about a kilometre); the three-decimal position is only used for looking
    up a place name. The contact line in Settings > Weather is sent only to MET Norway (which asks apps to identify themselves).
  Weather on older Windows: Meridian asks for TLS 1.2 and 1.3; where Windows does not know 1.3 it now asks for 1.2 alone instead of failing.
  holidays.txt: see "Sri Lankan holidays" above.
  Crash dump and settings.ini layout number: see "Files" above.
  Theme pictures: all the pictures of the themes together may use 192 MB of memory once opened (one 8192 x 8192 picture alone is 256 MB);
    a picture that would pass that is left out and the Themes tab says why.
  Setup: a folder such as C:folder or ..\x is refused (it needs a drive, a colon and a backslash); a screen saver whose path was stored in the
    registry as an expanding string is put back as one; Uninstall no longer removes the Settings > Apps entry when a file could not be deleted
    (it says which one, and you can run it again once nothing is using it).
  Install-ScreenSaver.bat and Remove-ScreenSaver.bat: 08 and 09 minutes work (a batch file reads a leading 0 as octal, so they used to fail), and
    the earlier screen saver is put back with the right registry type.
  For people who build it: build.bat now uses the same warning flags as build.sh (set MERIDIAN_WERROR=1 to make a warning an error, as the
    automatic checks do); .github/workflows/build.yml runs the tests and builds on GitHub; tests/smoke_wine.sh draws every face, theme and
    settings page under Wine; two more tests (holidays_test, pathcheck_test) and clockguard_test and locate_test are now in run_all.sh.
  Not changed, on purpose: the program still keeps its state in one global object and a few large files (main.cpp, scene_settings.cpp);
    it still draws through a window render target rather than a swap chain; and the screen saver, sleep, a real GPU reset, TLS on an old
    Windows and the crash dump on real Windows have only been tried under Wine or by reading the code, not on real PCs.

New in 2.8.0 "Away"  (11 November 2026)
  Settings has a new tab, Away, and the setup has a new page, While you're away, with the same two switches: turn Meridian on after N minutes
  of no use (1-60; it becomes your screen saver) and then put the PC to sleep after another N minutes (20-120), because nobody is likely to be
  there after N + N minutes. See "Away: Meridian on, then sleep" above.
  Setup: the screen saver choice moved from Make it yours to its own page, so a first install goes Welcome > (Make it yours) > While you're away >
  Install; the Update button on an installed PC stays one click. New flags /SleepAfter=N and /NoSleep; /ScreenSaver=N now takes 1-60, and so does
  Install-ScreenSaver.bat. Remove-ScreenSaver.bat also forgets the sleep time.

New in 2.7.0 "Sonnet" - the Sonnet Edition  (11 November 2026)
  Meridian now has an edition name, like other hand-made things do: "Sonnet Edition", shown in Settings > About (with the credit line),
  in the file properties of Meridian.exe, and in the setup.
  A real setup program replaces the NSIS one from 2.6.1: Meridian-Setup-2.9.1.exe, see "Setup file" above. Meridian itself is unchanged
  (same clock, same faces, same files); only the About page, the version and the credit line are new.

Fixes in 2.6.1 (from a code review of 2.6.0)
  Theme files: nesting is limited (a file with 100,000 "[" can no longer crash Meridian), the whole file must be valid (stray text after the
    end, a bad number or an unknown \ escape is refused with its line), and a formula is limited to 2000 characters. A pack that makes
    Meridian stop while it is being read is skipped at the next start (themes-skipped.txt) until its theme.json changes. Pictures and
    fonts over 32 MB, a theme.json over 1 MB are refused. Packs can repeat every year ("yearly": true; the Halloween pack does).
  Weather: after you change the place, a late answer for the old place can no longer replace the new one. Closing Meridian while a request is
    still running now ends it (within about a second) instead of leaving the program running in the background.
  Minimised: no busy loop. It wakes twice a second at most, and a timer or alarm that finishes still shows (the taskbar button flashes).
  Alarm: it is a time on the wall clock, worked out with the real daylight-saving rules, so it is right on the night the clocks change
    (a time that happens twice rings the first time; a time that is skipped rings just after the gap). It moves with a changed time zone.
  Dates and times typed anywhere (Your dates, the countdown date, theme files) must be real: 31 February, month 13 and 99:99 are refused. A
    yearly 29 February is shown on 28 February in years without a 29th. "Your dates" shows four to a page with BACK / NEXT, up to 200.
  Notes: a backslash in a note (C:\notes, \n) now comes back exactly as typed; old settings.ini files still load (note_format=2 marks the new way).
  Settings text boxes: a caret you can move (arrows, Home, End, Ctrl+arrows by word, Up / Down in the note), Shift to select, Ctrl+A / C / X / V,
    Ctrl+Z / Ctrl+Y undo and redo, Delete, click to place the caret, double-click to select all. The boxes are still drawn by Meridian itself,
    so screen readers cannot see them yet, and Tab does not move between boxes.
  Saving: if settings.ini cannot be written (disk full, locked file) Meridian says so and retries every 10 seconds. The fallback write used on
    shares that cannot swap files keeps the old contents and puts them back when a write fails.
  Build: a test build (MERIDIAN_DEV) has its own folder (build-dev), and object files made with other flags are never reused.

Files
  Settings:  %APPDATA%\Meridian\settings.ini   (everything is saved automatically; new sections [skymap],
             [quiet] (weather, strength, sun_flare, smooth_motion, sky_rain, midnight_sky), [storms] (watch, radius_km), [themes] (disabled packs), and
             per_page / page_seconds under [widgets]; [location] now also keeps where the position came from; [holidays] (auto, countries, uk_nation))
  Theme packs:  themes\ next to Meridian.exe, or %APPDATA%\Meridian\themes
  Holiday dates you typed:  %APPDATA%\Meridian\holidays.txt  (only exists once you press OPEN HOLIDAYS.TXT)
  Weather cache and log:  %LOCALAPPDATA%\Meridian  (forecast.json, weather.log)
  If Meridian ever crashes: a line in %LOCALAPPDATA%\Meridian\crash.log, a small crash dump beside it in %LOCALAPPDATA%\Meridian\crash
  (Meridian-<date>-<time>.dmp, the newest three are kept; it holds where every thread was when it stopped, and is useful to whoever
  fixes the bug. It can contain bits of what was in memory, so look before sending one to anyone), and Meridian starts itself again
  (at most three times in a row). The .exe is stripped of names, so the dump alone shows addresses; crash.log has the offset.
  settings.ini starts with a [meridian] section: settings_version (the layout, so a newer Meridian can convert an older file) and
  written_by. A settings.ini written by a NEWER Meridian is copied once to settings.ini.from-newer-version-N.bak before this one saves.

Staying up for months (new in 2.5.1)
  Files are written beside the old one and swapped in, so a power cut cannot leave a half-written settings.ini.
  Only one Meridian runs at a time; opening it again brings the first one to the front.
  The window returns to the monitor it was on. A covered window, or a screen that is off, draws nothing.
  If the PC's clock is corrected or changed, a running countdown or stopwatch keeps its place (the alarm stays on clock time).
  On battery, Keep screen awake lets go; it takes hold again when the charger is back.
  Start with Windows (right-click menu) adds Meridian to your own startup list (no admin rights); it counts as on only while it
  points at this exe, so after moving the folder just switch it on again.
  Dim with the sun (right-click menu): the Brightness you set is eased down from sunset to the end of twilight (Sun 12 degrees below the
  horizon) and back up at dawn. settings.ini [view] auto_dim=1, and dim_at_night=0.40 is how bright it gets (15% to 100% of Brightness).
  Weather needs a contact for MET Norway: put an email or a website in Settings > Weather > Contact. Every request sends
  "Meridian/<version> (<contact>)" as its User-Agent. 7Timer is only tried over https now.

Data
  Sun, Moon, planets, stars, sky events and Poya days are computed on your PC. The holiday lists (Sri Lanka, UK, US) are built in.
  Weather: Open-Meteo, then MET Norway, wttr.in and 7Timer if the first ones fail. Refreshed every
  15 minutes and cached, so the last forecast still shows when offline.
  If weather will not load, open Settings > Weather: each source shows why it failed
  (and OPEN LOG shows the details). A proxy or firewall blocking those sites is the usual cause.

Build from source
  Windows: install MinGW-w64 (MSYS2 or winlibs.com), then run build.bat in the source folder.
  Linux (mingw-w64 cross-compiler): ./build.sh   (MERIDIAN_WERROR=1 makes any compiler warning an error, with either script)
  Test build: set MERIDIAN_DEV=1 first (build.bat) or run MERIDIAN_DEV=1 ./build.sh. Only that build has the --shot / --bench
  screenshot and benchmark switches and the test hooks; the normal Meridian.exe has none of them.
  Where things are: src/themes.h explains how theme packs are loaded and how to add a new kind of layer;
  src/locate.cpp is "find my location" (Windows location service, then an internet-address lookup);
  src/expr.cpp is the formula language; src/scene_theme.cpp draws a pack; src/scene_quiet.cpp is the Quiet weather (clouds, wind, rain, lightning, flare);
  src/scene_space.cpp is the Midnight sky and the Sunlit II rays; src/main.cpp holds the frame pacing.
  src/textedit.h: the text boxes (caret, selection, undo); src/textesc.h: how a note is stored on one line.
  src/holidays.cpp, holidays_uk.h, holidays_us.h: the three holiday lists, the country choice and the merging of shared days.
  src/reveal.h: the screen saver's graceful start (numbers only; scene.cpp draws it).
  src/saver.cpp: the screen saver mode (the switches Windows uses, the black windows on other monitors, when to close, the sleep step
  with SetSuspendState) and the in-app Away switches (making Meridian.scr, remembering and restoring the old screen saver).
  src/robust.cpp: the Windows helpers for running for months (atomic file writes, one copy at a time, crash note, crash dump and restart,
  monitor check, battery, start with Windows, dark menus). src/clockguard.h notices when the system clock is stepped.
  What is tested and how: tests/ (expr_test, minijson_test, textedit_test, textesc_test, dates_test, alarm_test, locate_test, clockguard_test,
  holidays_test, holidays_regions_test, reveal_test and pathcheck_test build on their own with g++, no Windows needed; the first line of each file shows the command;
  "bash tests/run_all.sh" runs them all with the address and undefined-behaviour checkers switched on). "bash tests/smoke_wine.sh" builds the
  test build and draws every face, theme and settings page under Wine, checking that each picture is really drawn (it needs wine, ImageMagick
  and mingw-w64; it cannot judge how things look). Nothing here replaces trying the app on a real Windows PC.
Fonts: Sora, Instrument Serif, DM Mono; for the GTA VI pack Bungee and Chakra Petch (plus the optional Archivo Black,
Big Shoulders Display and Anton); for the Halloween pack Creepster and Special Elite. All are SIL Open Font License except
Special Elite (Apache 2.0). The licence texts are in licenses and in themes/gta6/fonts/licenses and themes/halloween/fonts/licenses.
The GTA VI pack's Launch Map (themes/gta6/art/world.json) is made from Natural Earth country shapes (public domain) in the form published by the
world-atlas package (ISC licence); the notice is in themes/gta6/art/world-LICENSE.txt.
