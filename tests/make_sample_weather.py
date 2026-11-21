# Generates a SYNTHETIC forecast in Open-Meteo's response shape for offline visual tests only.
# Not real data and never shipped with the app.
import json, math, random, datetime as dt
random.seed(11)
tz = 19800
start = dt.date(2026,10,15)
days = 14
times=[]; temp=[]; feels=[]; hum=[]; pp=[]; pr=[]; wc=[]; cc=[]; ws=[]; uv=[]; isd=[]
daily={"time":[],"weather_code":[],"temperature_2m_max":[],"temperature_2m_min":[],"apparent_temperature_max":[],"apparent_temperature_min":[],"sunrise":[],"sunset":[],"uv_index_max":[],"precipitation_sum":[],"precipitation_probability_max":[],"wind_speed_10m_max":[],"wind_gusts_10m_max":[]}
for di in range(days):
    d = start + dt.timedelta(days=di)
    wet = 0.55 + 0.35*math.sin(di*1.7)           # day wetness
    tmax = 27.5 + 1.5*math.sin(di*0.9); tmin = 20.8 + 0.6*math.cos(di*1.3)
    dayp=[]; dayr=0.0
    for h in range(24):
        t = dt.datetime(d.year,d.month,d.day,h,0)
        times.append(t.strftime("%Y-%m-%dT%H:%M"))
        phase = (h-5)/24*2*math.pi
        base = tmin + (tmax-tmin)*(0.5-0.5*math.cos(phase - 0.0))
        # afternoon peak shifted: use skewed curve
        x = ((h-5)%24)/24.0
        base = tmin + (tmax-tmin)*math.sin(min(x*1.55,1)*math.pi/2)**1.6 if x<0.55 else tmin + (tmax-tmin)*(1-((x-0.55)/0.45))**1.2
        p = max(0.0, min(100.0, 100*wet*(0.15 + 0.85*math.exp(-((h-16.5)/3.2)**2)) + random.uniform(-8,8)))
        rain = (p/100.0)**2*2.8*random.uniform(0.4,1.2) if p>35 else 0.0
        cloud = max(5,min(100, 40 + 55*p/100 + random.uniform(-10,10)))
        sunlit = 6 <= h < 18
        code = 95 if (p>78 and 14<=h<=19) else 81 if p>62 else 80 if p>48 else 3 if cloud>78 else 2 if cloud>45 else 1
        if 48>=p>35 and rain>0: code = 51
        uvv = max(0.0, 11.2*math.sin(math.pi*(h-6)/12)*(1-0.6*cloud/100)) if sunlit else 0.0
        temp.append(round(base - 1.6*(p/100),1)); feels.append(round(base - 1.6*(p/100) + 1.8,1))
        hum.append(round(min(98, 62 + (28-base)*3 + 25*p/100))); pp.append(round(p)); pr.append(round(rain,1)); wc.append(code); cc.append(round(cloud))
        ws.append(round(6+6*abs(math.sin(h/24*math.pi*2+di))+random.uniform(0,3),1)); uv.append(round(uvv,1)); isd.append(1 if sunlit else 0)
        dayp.append(p); dayr+=rain
    daily["time"].append(d.strftime("%Y-%m-%d"))
    daily["weather_code"].append(95 if max(dayp)>78 else 81 if max(dayp)>60 else 80)
    daily["temperature_2m_max"].append(round(tmax,1)); daily["temperature_2m_min"].append(round(tmin,1))
    daily["apparent_temperature_max"].append(round(tmax+2.2,1)); daily["apparent_temperature_min"].append(round(tmin+1.5,1))
    daily["sunrise"].append(d.strftime("%Y-%m-%d")+"T05:51"); daily["sunset"].append(d.strftime("%Y-%m-%d")+"T17:48")
    daily["uv_index_max"].append(round(max(uv[-24:]),1)); daily["precipitation_sum"].append(round(dayr,1))
    daily["precipitation_probability_max"].append(round(max(dayp))); daily["wind_speed_10m_max"].append(round(max(ws[-24:]),1)); daily["wind_gusts_10m_max"].append(round(max(ws[-24:])*1.8,1))
# current block at 2026-10-18T17:30 (matches the hour index)
ci = (dt.date(2026,10,18)-start).days*24 + 17
cur = {"time":"2026-10-18T17:30","interval":900,"temperature_2m":temp[ci],"relative_humidity_2m":hum[ci],"apparent_temperature":feels[ci],"is_day":1,"precipitation":pr[ci],"weather_code":wc[ci],"cloud_cover":cc[ci],"pressure_msl":1009.4,"wind_speed_10m":ws[ci],"wind_direction_10m":238,"wind_gusts_10m":round(ws[ci]*1.8,1)}
out={"latitude":6.9271,"longitude":79.8612,"generationtime_ms":0.1,"utc_offset_seconds":tz,"timezone":"Asia/Colombo","timezone_abbreviation":"+0530","elevation":560.0,
 "current":cur,
 "hourly":{"time":times,"temperature_2m":temp,"apparent_temperature":feels,"relative_humidity_2m":hum,"precipitation_probability":pp,"precipitation":pr,"weather_code":wc,"cloud_cover":cc,"wind_speed_10m":ws,"uv_index":uv,"is_day":isd},
 "daily":daily}
json.dump(out, open("tests/sample_weather.json","w"))
air={"latitude":6.9271,"longitude":79.8612,"utc_offset_seconds":tz,"current":{"time":"2026-10-18T17:30","interval":3600,"us_aqi":42,"pm2_5":9.8,"pm10":14.1}}
json.dump(air, open("tests/sample_air.json","w"))
print("hours", len(times), "days", len(daily["time"]))
