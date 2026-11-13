"""geo.py -- the earth's two sums the city cook needs, with nothing but numpy: latitude and longitude to UTM metres, and a mosaic of
one-metre elevation tiles read as one field.

utm(lat, lon, zone)      WGS84 / NAD83 to UTM easting and northing, Krueger's series (good to a millimetre at these latitudes).
                         NAD83 and WGS84 differ by about a metre in Seattle; OpenStreetMap is WGS84 and the USGS tiles NAD83. That metre is
                         left in: it's smaller than a road's width and smaller than the DEM's pixel.
Dem(paths)               the tiles, by their own GeoTIFF tie points. sample(e, n) is bilinear in metres, at pixel centres, and gives
                         NaN where no tile has data.
"""
import math

import numpy as np


def utm(lat, lon, zone=10):
    """lat, lon in degrees (floats or numpy arrays) -> easting, northing in metres, northern hemisphere"""
    a, f = 6378137.0, 1 / 298.257222101
    n = f / (2 - f)
    A = a / (1 + n) * (1 + n * n / 4 + n ** 4 / 64)
    al = (n / 2 - 2 * n * n / 3 + 5 * n ** 3 / 16, 13 * n * n / 48 - 3 * n ** 3 / 5, 61 * n ** 3 / 240)
    lon0 = -183.0 + 6.0 * zone
    p = np.radians(np.asarray(lat, dtype=np.float64))
    lam = np.radians(np.asarray(lon, dtype=np.float64) - lon0)
    e = math.sqrt(f * (2 - f))
    t = np.sinh(np.arctanh(np.sin(p)) - e * np.arctanh(e * np.sin(p)))
    xi = np.arctan2(t, np.cos(lam))
    eta = np.arctanh(np.sin(lam) / np.sqrt(1 + t * t))
    E = eta.copy() if isinstance(eta, np.ndarray) else eta
    N = xi.copy() if isinstance(xi, np.ndarray) else xi
    for j in range(3):
        k = 2 * (j + 1)
        E = E + al[j] * np.cos(k * xi) * np.sinh(k * eta)
        N = N + al[j] * np.sin(k * xi) * np.cosh(k * eta)
    return 500000.0 + 0.9996 * A * E, 0.9996 * A * N


class Dem:
    """the elevation tiles as one field. Only the window asked for is kept: each tile is read whole once (Pillow, LZW), cropped, and let go"""

    NODATA = -9000.0

    def __init__(self, paths):
        from PIL import Image
        Image.MAX_IMAGE_PIXELS = None
        self.paths = list(paths)
        self.tiles = []
        for p in self.paths:
            im = Image.open(p)
            tie = im.tag_v2[33922]
            scale = im.tag_v2[33550]
            assert abs(scale[0] - 1.0) < 1e-9 and abs(scale[1] - 1.0) < 1e-9, "%s: the cook reads one-metre tiles only (%r)" % (p, scale)
            self.tiles.append({"path": p, "e0": float(tie[3]), "n0": float(tie[4]), "w": im.size[0], "h": im.size[1]})
            im.close()

    def window(self, e0, n0, e1, n1, step=1.0):
        """the field on a grid: rows from n0 northwards, columns from e0 eastwards, every `step` metres, both ends included. Bilinear at pixel centres"""
        from PIL import Image
        es = e0 + step * np.arange(int(round((e1 - e0) / step)) + 1)
        ns = n0 + step * np.arange(int(round((n1 - n0) / step)) + 1)
        out = np.full((len(ns), len(es)), np.nan, dtype=np.float64)
        for t in self.tiles:
            # pixel (c, r) has its middle at e0 + c + 0.5, n0 - r - 0.5
            c = es - t["e0"] - 0.5
            r = t["n0"] - 0.5 - ns
            ci = np.nonzero((c >= 0) & (c <= t["w"] - 1))[0]
            ri = np.nonzero((r >= 0) & (r <= t["h"] - 1))[0]
            if not len(ci) or not len(ri):
                continue
            cmin, cmax = int(math.floor(c[ci].min())), min(t["w"] - 1, int(math.floor(c[ci].max())) + 1)
            rmin, rmax = int(math.floor(r[ri].min())), min(t["h"] - 1, int(math.floor(r[ri].max())) + 1)
            im = Image.open(t["path"])
            z = np.asarray(im, dtype=np.float32)[rmin:rmax + 1, cmin:cmax + 1].astype(np.float64)
            im.close()
            cc = c[ci] - cmin
            rr = r[ri] - rmin
            c0 = np.minimum(np.floor(cc).astype(int), z.shape[1] - 2) if z.shape[1] > 1 else np.zeros(len(cc), int)
            r0 = np.minimum(np.floor(rr).astype(int), z.shape[0] - 2) if z.shape[0] > 1 else np.zeros(len(rr), int)
            fc = (cc - c0)[None, :]
            fr = (rr - r0)[:, None]
            c1 = np.minimum(c0 + 1, z.shape[1] - 1)
            r1 = np.minimum(r0 + 1, z.shape[0] - 1)
            a = z[np.ix_(r0, c0)]
            b = z[np.ix_(r0, c1)]
            cq = z[np.ix_(r1, c0)]
            d = z[np.ix_(r1, c1)]
            v = (a * (1 - fc) + b * fc) * (1 - fr) + (cq * (1 - fc) + d * fc) * fr
            bad = (a < self.NODATA) | (b < self.NODATA) | (cq < self.NODATA) | (d < self.NODATA)
            v[bad] = np.nan
            blk = out[np.ix_(ri, ci)]
            take = np.isnan(blk) & ~np.isnan(v)
            blk[take] = v[take]
            out[np.ix_(ri, ci)] = blk
        return es, ns, out


def fill_holes(z):
    """NaN pixels (no tile there) take the nearest row's or column's value, so a strip the tiles miss is flat ground and not a hole. Says how many"""
    bad = np.isnan(z)
    n = int(bad.sum())
    if not n:
        return 0
    for axis in (1, 0):
        zz = np.moveaxis(z, axis, 0)
        for k in range(1, zz.shape[0]):
            m = np.isnan(zz[k])
            zz[k][m] = zz[k - 1][m]
        for k in range(zz.shape[0] - 2, -1, -1):
            m = np.isnan(zz[k])
            zz[k][m] = zz[k + 1][m]
    z[np.isnan(z)] = 0.0
    return n
