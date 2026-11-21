#!/usr/bin/env python3
"""Writes sample responses in the documented formats of MET Norway, wttr.in and 7Timer (for parser tests)."""
import json, math, datetime as dt
start = dt.datetime(2026, 10, 19, 9, 0, 0)           # 09:00Z = 14:30 local (+5:30)

def met():
    ts = []
    t = start
    for i in range(60 + 28):                          # 60 hourly points, then 6-hourly
        step = 1 if i < 60 else 6
        hour_local = (t.hour + 5.5) % 24
        temp = 26 + 3 * math.sin((hour_local - 9) / 24 * 2 * math.pi) - 0.04 * i
        rainy = (t.hour in (9, 10, 11, 12, 13)) and (t.day in (19, 21))
        sym = "lightrain_day" if rainy and 6 <= hour_local < 18 else ("rain_night" if rainy else ("partlycloudy_day" if 6 <= hour_local < 18 else "cloudy_night"))
        e = {"time": t.strftime("%Y-%m-%dT%H:%M:%SZ"), "data": {"instant": {"details": {"air_pressure_at_sea_level": 1009.4, "air_temperature": round(temp, 1), "cloud_area_fraction": 82.0 if rainy else 45.0, "relative_humidity": 74.0, "wind_from_direction": 250.0, "wind_speed": 3.2}}}}
        d = e["data"]
        if step == 1:
            d["next_1_hours"] = {"summary": {"symbol_code": sym}, "details": {"precipitation_amount": 0.6 if rainy else 0.0}}
        d["next_6_hours"] = {"summary": {"symbol_code": sym}, "details": {"precipitation_amount": 2.4 if rainy else 0.0}}
        d["next_12_hours"] = {"summary": {"symbol_code": sym}}
        ts.append(e)
        t += dt.timedelta(hours=step)
    return {"type": "Feature", "geometry": {"type": "Point", "coordinates": [79.8612, 6.9271, 600]}, "properties": {"meta": {"updated_at": "2026-10-19T08:30:00Z"}, "timeseries": ts}}

def wttr():
    days = []
    for dd in range(3):
        d = dt.date(2026, 10, 19) + dt.timedelta(days=dd)
        hourly = []
        for k in range(8):
            hr = k * 3
            temp = round(26 + 3 * math.sin((hr - 9) / 24 * 2 * math.pi))
            hourly.append({"time": str(hr * 100), "tempC": str(temp), "FeelsLikeC": str(temp + 3), "humidity": "72", "chanceofrain": "70" if 9 <= hr <= 15 else "10",
                           "precipMM": "1.2" if 9 <= hr <= 15 else "0.0", "weatherCode": "296" if 9 <= hr <= 15 else "116", "cloudcover": "60", "windspeedKmph": "11", "uvIndex": "6" if 9 <= hr <= 15 else "0"})
        days.append({"date": d.isoformat(), "maxtempC": "28", "mintempC": "21", "hourly": hourly})
    return {"current_condition": [{"temp_C": "27", "FeelsLikeC": "30", "humidity": "71", "pressure": "1009", "windspeedKmph": "12", "winddirDegree": "250", "cloudcover": "55", "precipMM": "0.0",
                                   "weatherCode": "116", "observation_time": "08:45 AM", "localObsDateTime": "2026-10-19 02:15 PM"}], "weather": days}

def seven():
    ds = []
    for i in range(64):
        tp = 3 * (i + 1)
        hr_local = (12 + tp + 5.5) % 24
        ds.append({"timepoint": tp, "cloudcover": 5, "lifted_index": 2, "prec_type": "rain" if i % 8 in (3, 4) else "none", "prec_amount": 3 if i % 8 in (3, 4) else 0,
                   "temp2m": round(26 + 3 * math.sin((hr_local - 9) / 24 * 2 * math.pi)), "rh2m": "79%", "wind10m": {"direction": "NE", "speed": 3},
                   "weather": "lightrainday" if i % 8 in (3, 4) else "pcloudyday"})
    return {"product": "civil", "init": "2026101912", "dataseries": ds}

for name, fn in (("sample_metno.json", met), ("sample_wttr.json", wttr), ("sample_7timer.json", seven)):
    json.dump(fn(), open("tests/" + name, "w"))
    print("wrote", name)
