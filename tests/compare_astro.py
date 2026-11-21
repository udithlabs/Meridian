import subprocess, random, math, datetime as dt, sys
import numpy as np
import ephem
from astropy.time import Time
from astropy.coordinates import EarthLocation, AltAz, get_body, SkyCoord
import astropy.units as u
from astropy.utils import iers
iers.conf.auto_download = False
iers.conf.iers_degraded_accuracy = 'ignore'

LAT, LON = 6.9271, 79.8612
random.seed(7)
t0 = dt.datetime(2026,10,1,tzinfo=dt.timezone.utc).timestamp()
t1 = dt.datetime(2027,12,31,tzinfo=dt.timezone.utc).timestamp()
times = sorted(random.uniform(t0,t1) for _ in range(60))
out = subprocess.run(["./tests/astro_cli", str(LAT), str(LON)], input="\n".join(f"{t:.3f}" for t in times)+"\n", capture_output=True, text=True).stdout.splitlines()

loc = EarthLocation(lat=LAT*u.deg, lon=LON*u.deg, height=0*u.m)
blocks=[]; cur=None
for line in out:
    if line.startswith("T "):
        cur={"t":float(line.split()[1]),"lines":[]}; blocks.append(cur)
    elif cur is not None and not line.startswith("season"):
        cur["lines"].append(line.split())
seasons=[l.split() for l in out if l.startswith("season")]

def angdiff(a,b):
    d=(a-b+180)%360-180
    return d
errs={"sun":[], "moon":[], "Mercury":[], "Venus":[], "Mars":[], "Jupiter":[], "Saturn":[], "star":[]}
moon_ill=[]; moon_dist=[]; limb=[]
from astropy.coordinates import solar_system_ephemeris
solar_system_ephemeris.set('builtin')
stars_j2000 = {"Sirius":(6+45.1/60,-(16+43/60)),"Vega":(18+36.9/60,38+47/60),"Antares":(16+29.4/60,-(26+26/60)),"Canopus":(6+24.0/60,-(52+42/60)),"Polaris":(2+31.8/60,89+16/60),"Acrux":(12+26.6/60,-(63+6/60))}
for b in blocks:
    tm = Time(b["t"], format="unix")
    frame = AltAz(obstime=tm, location=loc)  # no pressure => no refraction
    def cmp(name, alt, az, body):
        c = get_body(body, tm, loc).transform_to(frame)
        da = alt - c.alt.deg; dz = angdiff(az, c.az.deg) * math.cos(math.radians(c.alt.deg))
        errs[name].append(math.hypot(da, dz))
    for l in b["lines"]:
        if l[0]=="sun": cmp("sun", float(l[1]), float(l[2]), "sun")
        elif l[0]=="moon":
            cmp("moon", float(l[1]), float(l[2]), "moon")
            moon_dist.append((float(l[5]), get_body("moon", tm, loc).distance.to(u.km).value))
            e=ephem.Moon(dt.datetime.utcfromtimestamp(b["t"])); moon_ill.append((float(l[6]), e.phase/100.0))
        elif l[0]=="planet": cmp(l[1], float(l[2]), float(l[3]), l[1].lower())
        elif l[0]=="star":
            ra,dec = stars_j2000[l[1]]
            c = SkyCoord(ra=ra*15*u.deg, dec=dec*u.deg, frame="icrs").transform_to(frame)
            da = float(l[2]) - c.alt.deg; dz = angdiff(float(l[3]), c.az.deg)*math.cos(math.radians(c.alt.deg))
            errs["star"].append(math.hypot(da,dz))
print("Position error vs astropy (degrees)  median / p95 / max")
for k,v in errs.items():
    v=np.array(v); print(f"  {k:8s} {np.median(v):7.4f} {np.percentile(v,95):7.4f} {v.max():7.4f}  (n={len(v)})")
md=np.array([abs(a-b) for a,b in moon_dist]); print(f"Moon distance error km: median {np.median(md):.1f} max {md.max():.1f}")
mi=np.array([abs(a-b) for a,b in moon_ill]); print(f"Moon illuminated fraction error vs ephem: max {mi.max():.4f}")

# phases vs ephem
print("Phase instants (seconds error vs ephem)")
errp=[]
for b in blocks[:20]:
    t=b["t"]; d=dt.datetime.utcfromtimestamp(t)
    nn=ephem.next_new_moon(d); nf=ephem.next_full_moon(d); pn=ephem.previous_new_moon(d); nq=ephem.next_first_quarter_moon(d)
    toU=lambda x: (x.datetime().replace(tzinfo=dt.timezone.utc)).timestamp()
    ln=[l for l in b["lines"] if l[0].startswith("nextnew")]
    
# rise/set and phases lines are in 'lines' with first token starting 'nextnew'
errn=[];errf=[];errpn=[];errq=[]; errsr=[]; errss=[]; errnoon=[]; errmr=[]; errms=[]; errciv=[]
for b in blocks:
    t=b["t"]; d=dt.datetime.utcfromtimestamp(t)
    toU=lambda x: (x.datetime().replace(tzinfo=dt.timezone.utc)).timestamp()
    for l in b["lines"]:
        if l[0]=="nextnew":
            errn.append(float(l[1])-toU(ephem.next_new_moon(d)))
            errf.append(float(l[3])-toU(ephem.next_full_moon(d)))
            errpn.append(float(l[5])-toU(ephem.previous_new_moon(d)))
            errq.append(float(l[7])-toU(ephem.next_first_quarter_moon(d)))
        if l[0]=="day":
            day0 = math.floor((t+19800)/86400)*86400-19800
            obs=ephem.Observer(); obs.lat=str(LAT); obs.lon=str(LON); obs.elevation=0; obs.pressure=0  # no refraction
            obs.horizon='-0.833'  # match threshold incl. refraction+semi-diameter treated as point-source
            obs.date=dt.datetime.utcfromtimestamp(day0)
            s=ephem.Sun()
            try:
                sr=toU(obs.next_rising(s, use_center=True)); ss=toU(obs.next_setting(s, use_center=True))
                errsr.append(float(l[1])-sr); errss.append(float(l[2])-ss)
            except Exception as ex: pass
            obs.horizon='-6'; 
            try:
                cr=toU(obs.next_rising(s, use_center=True)); errciv.append(float(l[5])-cr)
            except Exception: pass
        if l[0]=="moonrs":
            day0 = math.floor((t+19800)/86400)*86400-19800
            obs=ephem.Observer(); obs.lat=str(LAT); obs.lon=str(LON); obs.elevation=0; obs.pressure=0; obs.horizon='-0.8333'
            obs.date=dt.datetime.utcfromtimestamp(day0)
            m=ephem.Moon()
            try:
                if l[1]=="1": errmr.append(float(l[2])-toU(obs.next_rising(m)))
                if l[3]=="1": errms.append(float(l[4])-toU(obs.next_setting(m)))
            except Exception: pass
def rep(n,v):
    v=np.array(v); print(f"  {n:22s} median {np.median(np.abs(v)):7.1f}s  max {np.abs(v).max():7.1f}s  (n={len(v)})")
rep("next new moon", errn); rep("next full moon", errf); rep("prev new moon", errpn); rep("next first quarter", errq)
rep("sunrise (-0.833 centre)", errsr); rep("sunset", errss); rep("civil dawn (-6)", errciv)
rep("moonrise", errmr); rep("moonset", errms)
print("Equinox / solstice (seconds error vs ephem)")
for s in seasons:
    y=int(s[1]); w=int(s[2]); val=float(s[3])
    fn=[ephem.next_vernal_equinox, ephem.next_summer_solstice, ephem.next_autumn_equinox, ephem.next_winter_solstice][w]
    e=fn(str(y)+"/1/1"); ts=(e.datetime().replace(tzinfo=dt.timezone.utc)).timestamp()
    print(f"  {y} {['Mar eq','Jun sol','Sep eq','Dec sol'][w]}: {val-ts:7.1f}s  ({dt.datetime.fromtimestamp(val,dt.timezone.utc):%Y-%m-%d %H:%M} UTC)")
