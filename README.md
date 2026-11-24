# Meridian — 3.0.0 "Aurora" (Sonnet Edition)

**A quiet, always-on clock, live sky map, 3D solar system orrery, weather studio, and focus sanctuary for Windows.**

Meridian is a native, single-file Windows desktop clock and screen saver written in C++17 with Direct2D and DirectWrite — no browser engine, no Electron runtime, and zero external DLL dependencies.

![Meridian Sky Face](docs/screenshots/sky.png)

---

## Highlights in 3.0.0 "Aurora"

- **Museum-Grade Aurora Borealis & Live NOAA Space Weather**
  - Three depth-layered 3D perspective serpentine aurora curtains rendered with vertical magnetic-field ray striations (oxygen-emerald lower rims, seafoam-teal shafts, violet-amethyst crowns, and rose-magenta nitrogen fold fringes).
  - Live non-blocking **NOAA SWPC Planetary Kp-Index** worker automatically triggers real aurora at night during geomagnetic storms, displays a live `KP` storm badge on Sky and in **Settings → Quiet**, previews via the **`AURORA`** chip, and joins **Focus Studio** as its 15th studio backdrop.
- **Modern 3D Fisheye Crystal Sky Dome**
  - Remade into a convex 3D optical hemisphere with an orthogonal bowed great-circle fisheye grid and foreshortened altitude parallels (`15°–75°`) so the entire 360° horizon stays visible at once — complete horizon-to-horizon Sun & Moon arcs with rise/set rim beacons, below-horizon refraction glows, layered solar corona, cached Fresnel crystal lens sheen, precision zenith reticle, and a clean double-step optical bezel.
- **3D Horological Planetarium Solar System Orrery**
  - Switch seamlessly on **Sky** between the **3D Fisheye Sky Dome** and a 3D perspective-tilted **Heliocentric Solar System Orrery** (`O` key or `SKY | ORRERY` sliding pill) featuring a cached celestial vault, **520-rock Main Asteroid Belt**, icy **Kuiper Belt** dust halo, radiant solar corona, depth-sorted directionally sun-lit 3D spheres for **Mercury through Neptune**, banded **Jupiter with 4 live Galilean moons**, **Saturn's split back/front Cassini-division rings**, and **Earth's tilted orbiting Moon** — also available as a **Solar System** widget (`5`).
- **Photorealistic Volumetric fBm Clouds**
  - Replaces smooth oval puffs with multi-octave domain-warped fractal Brownian motion clouds with Beer-Lambert directional self-shadowing and silver-lining edge scattering — painting wispy cirrus, ragged convective cumulus, and sweeping nocturnal stratocumulus decks across **Quiet**, **Overcast**, and **Focus Studio**.
- **Wet-Horizon Clock Reflections on Quiet**
  - During **Rain**, **Drizzle**, **Storm**, **Sleet**, or **Hail**, rain-slicked water reflections mirror the live Quiet clock digits in soft 34-strip wave-rippled shimmer beneath the date line (toggleable anytime in **Settings → Quiet**).
- **Sculpted Volumetric Clouds, Cinematic Atmospheric Fog & Four New Artistic Weather Effects**
  - **Remade Volumetric Clouds** — Three sky-altitude archetypes painted with smooth directional elliptical gradients (zero polka-dot artifacts): sweeping feathered **cirrus wisps**, asymmetric cauliflower **cumulus towers**, and dramatic **stratocumulus decks** sorted cleanly back-to-front across all 11 built-in themes.
  - **Remade Cinematic Atmospheric Fog** — Four-tone layered valley mist with diffused upper-sky glow, sculpted rolling mist banks, counter-drifting thermal depth strata, slow curling gossamer mist tendrils, and floating dew-light micro-motes.
  - **Drizzle, Sleet, Hail & Gale** — Auto-trigger from live WMO weather codes & gusts, fall cleanly behind the Sky map (precipitation), preview in **Settings → Quiet**, and join **Focus Studio**'s Ambience Orb (now **15 studio backdrops**, including **Aurora**, **Midnight**, and **Stars**).
  - **Galaxy Theme & Artistic Midnight Theme** — The deep-space cosmos theme (Milky Way, emission nebulae, Andromeda, ringed Saturn, meteors, comet & `U`-key UFO visitor) is named **Galaxy**, paired with the handcrafted Prussian-velvet & moonlit champagne-gold **Midnight** theme.
- **Liquid Glass Tactile UI Everywhere, Artistic Blizzard & Deep-Space Cosmos**
  - Every button across **Timer**, **Focus Studio**, the **bottom navigation dock**, rotating panel arrows, Month Calendar arrows, and **Settings** (including liquid-droplet gliding toggles and frosted `−`/`+` steppers) responds with spring-eased hover bloom, cursor-tracking specular glass sheen, tactile press-and-hold squish, elastic liquid-jelly rebound, and expanding caustic click ripples — plus **gliding active pills** across segmented rows and touch-reactive Focus Studio cards & Sky AQI badge.
  - **Artistic Blizzard / Harsh Snowstorm** — four organic blowing-snow variants across three parallax depths, billowing whiteout squall curtains, curling frosted spindrift wind ribbons, racing ground snowdrifts, and fast foreground wisps.
  - **Remade Deep-Space Cosmos (`Galaxy` & Focus `STARS`)** — sculpted Milky Way with Great Dark Rift, organic multi-cloud emission nebulae, ~1,400 spectral O–M stars with 4-point diffraction starbursts, Pleiades cluster, feathered spiral Andromeda + companion dwarf galaxy, tilted banded Saturn with Cassini Division & moons, crescent ice-giant, 44 living jewel stars, and weather-immune dual meteor streams & comets in Focus `STARS`.
  - Runs at full display refresh rate during interaction and settles completely back to **1 FPS idle** with zero background CPU/GPU cost.

- **Six Distinct Faces (`1`–`6`)**
  - **Sky (`1`)** — Big display clock, live astronomical sky dome **or** 3D Solar System Orrery (`O`), rotating Today / Events / Holidays / World Clocks panel, live weather & AQI badge, and NOAA geomagnetic storm indicator.
  - **Quiet (`2`)** — Minimalist full-screen typography with live weather & aurora drawn artistically behind the clock (aurora, photorealistic volumetric fBm clouds, cinematic fog, wind, gale, drizzle, rain, sleet, hail, snowfall, blizzard, thunder, tornado, sun flare, named tropical storm watch, and wet-horizon clock reflections).
  - **Timer (`3`)** — Clean countdown timer, stopwatch with laps, and desk alarm.
  - **Almanac (`4`)** — Detailed Sun & Moon telemetry, exact morning & evening **Golden Hour** and **Blue Hour** spans, planetary visibility, and 7-day forecast.
  - **Widgets (`5`)** — Modular pages for World Clocks (31 cities with per-year Windows Dynamic DST), Month Calendar, **3D Solar System Orrery**, Moon Phases, Hourly Forecast, Daylight, Sky, Air Quality, System stats, Countdown, and Notes.
  - **Focus Studio (`6`)** — Dedicated lo-fi focus sanctuary with its own independent Pomodoro timer (`25m` default), smaller live clock & weather corner cards, frosted focus medallion with breathing halo, and a **collapsible floating Ambience & Theme orb** (`Off · Live · Drizzle · Rain · Storm · Hail · Sleet · Snow · Blizzard · Gale · Clouds · Fog · Midnight · Stars · Aurora`) that expands smoothly on hover.

![Meridian Quiet Aurora Borealis](docs/screenshots/aurora-quiet.png)
![Meridian 3D Solar System Orrery](docs/screenshots/orrery-sky.png)
![Meridian Focus Studio Volumetric Clouds](docs/screenshots/focus-clouds.png)
![Meridian Focus Studio Orb Expanded](docs/screenshots/focus-menu.png)

- **42-Constellation Artistic Sky Atlas, Layered Snowfall & Shooting-Star Physics**
  - Natural compass Sky Map orientation (`N` up, `E` right, `S` down, `W` left), true dark cosmic Midnight/Galaxy/Night/Ember dome palettes, and **42 accurate J2000 constellations** across both hemispheres with real spectral star colors, trimmed filigree lines, and diamond glints — appearing strictly at night by default (even on dark themes), with an optional daytime toggle in **Settings → Sky Map**.
  - Calm three-depth snowfall behind both the Quiet and Sky faces (plus a `SNOW` preview chip in **Settings → Quiet**).
  - Galaxy meteors ignite from a pinpoint, flare with warm-to-white color temperature and shedding sparks, and stream in a weekly alternating 6-week directional loop.
- **Daylight Saving Time (DST Zones Only)**
  - Automatic 2 AM DST transitions, per-hour DST-accurate forecasts, subtle `DST` header tag only when DST is active, 48-hour clock-change Today notices, and Windows Dynamic DST across World Clock cities — leaving non-DST zones completely untouched.
- **Resilient Weather & Multi-Source Air Quality**
  - **8 weather providers** tried in fallback order: **Open-Meteo**, **MET Norway**, **wttr.in**, **7Timer**, **Bright Sky (DWD)**, **NWS / NOAA**, plus optional free-key **WeatherAPI.com** and **OpenWeatherMap** in **Settings → Weather**.
  - Independent **4-tier Air Quality pipeline** (**Open-Meteo Air Quality → WeatherAPI.com → OpenWeatherMap → WAQI nearby station**) with a color-coded air-waves emblem badge on Sky and Focus.
  - Hardened WinHTTP networking with generous timeouts, forecast/AQI staggering, and automatic TLS 1.2 + HTTP/1.1 fallback recovery.
- **Eleven Built-In Themes + Custom Theme Packs**
  - `Auto`, `Day`, `Sunlit`, `Sunlit II`, `Dawn`, `Sunset`, `Dusk`, `Night`, `Midnight`, `Galaxy`, and `Ember` (plus optional red-night bedtime mode and drop-in JSON/art Theme Packs in `themes/`).
- **Screen Saver Mode & Privacy-First Defaults**
  - Includes `Install-ScreenSaver.bat` / `Remove-ScreenSaver.bat` and a full standalone Windows installer (`Meridian-Setup-3.0.0.exe`).
  - Defaults cleanly to the Prime Meridian (Greenwich) with Celsius & km/h units. Optional API keys and settings stay strictly local in `settings.ini`.

---

## Download & Install (Windows)

Grab the latest binaries from the [**Releases**](../../releases) page:

| File | Description |
| :--- | :--- |
| **`Meridian-Setup-3.0.0.exe`** | Recommended Windows installer with Start Menu shortcut, optional startup & screen saver setup, and transactional rollback-safe upgrades. |
| **`Meridian-3.0.0.zip`** | Portable ZIP — unzip anywhere and run `Meridian.exe`. |
| **`Meridian.exe`** | Standalone single-file executable (Windows 10 / 11, 64-bit). |

---

## Keyboard Shortcuts

| Key | Action |
| :--- | :--- |
| `1` – `6` | Switch face (`Sky`, `Quiet`, `Timer`, `Almanac`, `Widgets`, `Focus`) |
| `T` / `Shift+T` | Next / previous theme |
| `O` | Toggle Sky Dome ↔ Live 3D Solar System Orrery (on Sky) |
| `F` or `F11` | Toggle fullscreen |
| `P` | Toggle keep on top |
| `Space` / `R` | Start/pause or reset on Timer & Focus |
| `,` | Open Settings |
| `F1` or `?` | Show all shortcuts |
| `Esc` | Close overlay, dismiss alert, or exit fullscreen |

---

## Building from Source

Meridian compiles to a static, dependency-free 64-bit Windows executable with **MinGW-w64** (`g++` C++17) and **Python 3** (for packing the setup wizard).

### Windows (MSYS2 MINGW64 or w64devkit)
```bat
:: Build Meridian.exe (release)
set MERIDIAN_WERROR=1
build.bat

:: Build Meridian-Setup-3.0.0.exe (installer)
cd setup
build-setup.bat
```

### Linux Cross-Compile (Ubuntu / Debian)
```bash
sudo apt-get install -y mingw-w64 python3

# Build release Meridian.exe
MERIDIAN_WERROR=1 ./build.sh

# Build Windows installer
MERIDIAN_WERROR=1 ./setup/build.sh

# Run portable unit test suite (ASan + UBSan)
bash tests/run_all.sh
```

> **Development build:** Set `MERIDIAN_DEV=1` before running `build.bat` or `./build.sh` to build `build-dev/Meridian-dev.exe` with off-screen screenshot (`--shot`) and benchmark (`--bench`) switches without overwriting release `Meridian.exe`.

---

## Continuous Integration

Pushing this repository to GitHub automatically runs [`.github/workflows/build.yml`](.github/workflows/build.yml):
1. **`portable-tests`** — Runs all 20 C++ unit tests on Ubuntu with AddressSanitizer and UndefinedBehaviorSanitizer.
2. **`cross-build`** — Cross-compiles release `Meridian.exe`, dev `Meridian-dev.exe`, and `Meridian-Setup-3.0.0.exe` with `-Werror` and verifies no dev flags leak into the release binary.
3. **`windows-build`** — Compiles natively on `windows-latest` with MSYS2 MinGW-w64 (`build.bat` & `setup\build-setup.bat`).
4. **`wine-smoke`** — Renders every face, theme, and Settings tab off-screen under Wine.

---

## Licenses & Credits

- Bundled open-source fonts (`Sora`, `Instrument Serif`, `DM Mono`, `Anton`) are licensed under the SIL Open Font License 1.1 — see [`licenses/`](licenses/).
- Full documentation and complete version history from 1.x through 3.0.0 "Aurora" are in [`README.txt`](README.txt).
