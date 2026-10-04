"""Turns a waypoint route into heading/altitude/speed commands for the policy.

The policy only learns to hold a Command; following a route is this small,
deterministic layer on top, shared by the JSBSim and DCS sides.
"""

from dataclasses import dataclass

import numpy as np

from dcsai.spec import Command

EARTH_RADIUS_FT = 20_902_231.0
NM_FT = 6076.12


@dataclass
class Waypoint:
    lat_deg: float
    lon_deg: float
    alt_ft: float
    kias: float


def bearing_and_distance(lat1, lon1, lat2, lon2):
    """Initial great-circle bearing (deg, 0 = north) and distance (ft)."""
    p1, p2 = np.radians(lat1), np.radians(lat2)
    dlon = np.radians(lon2 - lon1)
    y = np.sin(dlon) * np.cos(p2)
    x = np.cos(p1) * np.sin(p2) - np.sin(p1) * np.cos(p2) * np.cos(dlon)
    bearing = (np.degrees(np.arctan2(y, x)) + 360.0) % 360.0
    a = np.sin((p2 - p1) / 2) ** 2 + np.cos(p1) * np.cos(p2) * np.sin(dlon / 2) ** 2
    dist = 2 * EARTH_RADIUS_FT * np.arcsin(np.sqrt(a))
    return float(bearing), float(dist)


def offset_position(lat, lon, bearing_deg, dist_ft):
    """Point dist_ft away from (lat, lon) along bearing_deg."""
    d = dist_ft / EARTH_RADIUS_FT
    b = np.radians(bearing_deg)
    p1, l1 = np.radians(lat), np.radians(lon)
    p2 = np.arcsin(np.sin(p1) * np.cos(d) + np.cos(p1) * np.sin(d) * np.cos(b))
    l2 = l1 + np.arctan2(np.sin(b) * np.sin(d) * np.cos(p1), np.cos(d) - np.sin(p1) * np.sin(p2))
    return float(np.degrees(p2)), float(np.degrees(l2))


class RouteFollower:
    """Steers to each waypoint in turn, advancing when within capture radius."""

    def __init__(self, route, capture_nm=1.0):
        self.route = list(route)
        self.capture_ft = capture_nm * NM_FT
        self.index = 0

    @property
    def done(self):
        return self.index >= len(self.route)

    def update(self, lat_deg, lon_deg):
        """Return the Command for the current position, or None when finished."""
        while not self.done:
            wp = self.route[self.index]
            bearing, dist = bearing_and_distance(lat_deg, lon_deg, wp.lat_deg, wp.lon_deg)
            if dist > self.capture_ft:
                return Command(heading_deg=bearing, alt_ft=wp.alt_ft, kias=wp.kias)
            self.index += 1
        return None
