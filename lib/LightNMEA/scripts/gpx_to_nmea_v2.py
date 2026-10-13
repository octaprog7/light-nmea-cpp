#!/usr/bin/env python3
"""Конвертация GPX файлов в NMEA 0183 (поддержка GPX 1.0 и 1.1)"""

import os
import sys
import math
from datetime import datetime
from xml.etree import ElementTree as ET

def decimal_to_nmea(degrees, is_lat=True):
    abs_deg = abs(degrees)
    dd = int(abs_deg)
    mm = (abs_deg - dd) * 60
    
    if is_lat:
        return f"{dd:02d}{mm:07.4f}", 'N' if degrees >= 0 else 'S'
    else:
        return f"{dd:03d}{mm:07.4f}", 'E' if degrees >= 0 else 'W'

def calculate_checksum(sentence):
    cs = 0
    for c in sentence:
        cs ^= ord(c)
    return f"{cs:02X}"

def make_rmc(lat, lon, speed_knots, course, dt):
    lat_str, lat_dir = decimal_to_nmea(lat, True)
    lon_str, lon_dir = decimal_to_nmea(lon, False)
    
    time_str = dt.strftime("%H%M%S.00")
    date_str = dt.strftime("%d%m%y")
    status = 'A'
    
    sentence = f"GPRMC,{time_str},{status},{lat_str},{lat_dir},{lon_str},{lon_dir},{speed_knots:05.1f},{course:05.1f},{date_str},,"
    cs = calculate_checksum(sentence)
    return f"${sentence}*{cs}"

def make_gga(lat, lon, alt, sats, hdop, dt):
    lat_str, lat_dir = decimal_to_nmea(lat, True)
    lon_str, lon_dir = decimal_to_nmea(lon, False)
    
    time_str = dt.strftime("%H%M%S.00")
    quality = 1
    
    sentence = f"GPGGA,{time_str},{lat_str},{lat_dir},{lon_str},{lon_dir},{quality},{sats:02d},{hdop:.1f},{alt:.1f},M,47.0,M,,"
    cs = calculate_checksum(sentence)
    return f"${sentence}*{cs}"

def gpx_to_nmea(gpx_file, nmea_file):
    """Конвертация GPX в NMEA с автоопределением версии"""
    
    try:
        tree = ET.parse(gpx_file)
        root = tree.getroot()
    except Exception as e:
        print(f"  Error parsing XML: {e}")
        return 0
    
    # Автоопределение namespace из корня XML
    ns = {}
    if root.tag.startswith('{'):
        ns_uri = root.tag.split('}')[0].strip('{')
        ns = {'gpx': ns_uri}
        print(f"[{ns_uri}]", end=' ')
    
    # Поиск точки трека с учётом namespace
    if ns:
        points = root.findall('.//gpx:trkpt', ns)
    else:
        points = root.findall('.//trkpt')
    
    if not points:
        print(f"  Warning: no track points found")
        return 0
    
    point_count = 0
    
    with open(nmea_file, 'w') as f:
        prev_lat, prev_lon = None, None
        prev_time = None
        
        for pt in points:
            try:
                lat = float(pt.get('lat'))
                lon = float(pt.get('lon'))
            except (TypeError, ValueError):
                continue
            
            # Время
            time_elem = pt.find('gpx:time', ns) if ns else pt.find('time')
            if time_elem is not None and time_elem.text:
                try:
                    dt = datetime.fromisoformat(time_elem.text.replace('Z', '+00:00'))
                except:
                    dt = datetime.now()
            else:
                dt = datetime.now()
            
            # Высота
            ele_elem = pt.find('gpx:ele', ns) if ns else pt.find('ele')
            alt = float(ele_elem.text) if ele_elem is not None and ele_elem.text else 0.0
            
            # Скорость и курс
            speed = 0.0
            course = 0.0
            if prev_lat is not None and prev_time is not None:
                dt_sec = (dt - prev_time).total_seconds()
                if dt_sec > 0:
                    dlat = math.radians(lat - prev_lat)
                    dlon = math.radians(lon - prev_lon)
                    a = math.sin(dlat/2)**2 + math.cos(math.radians(prev_lat)) * math.cos(math.radians(lat)) * math.sin(dlon/2)**2
                    c = 2 * math.asin(math.sqrt(a))
                    dist_m = 6371000 * c
                    speed_ms = dist_m / dt_sec
                    speed = speed_ms * 1.94384
                    
                    y = math.sin(dlon) * math.cos(math.radians(lat))
                    x = math.cos(math.radians(prev_lat)) * math.sin(math.radians(lat)) - \
                        math.sin(math.radians(prev_lat)) * math.cos(math.radians(lat)) * math.cos(dlon)
                    course = math.degrees(math.atan2(y, x)) % 360
            
            f.write(make_rmc(lat, lon, speed, course, dt) + "\r\n")
            f.write(make_gga(lat, lon, alt, 8, 1.2, dt) + "\r\n")
            
            prev_lat, prev_lon, prev_time = lat, lon, dt
            point_count += 1
    
    return point_count

def convert_directory(gpx_dir, nmea_dir):
    os.makedirs(nmea_dir, exist_ok=True)
    
    total_points = 0
    file_count = 0
    error_count = 0
    
    for filename in sorted(os.listdir(gpx_dir)):
        if filename.endswith('.gpx'):
            gpx_path = os.path.join(gpx_dir, filename)
            nmea_filename = filename.replace('.gpx', '.nmea')
            nmea_path = os.path.join(nmea_dir, nmea_filename)
            
            print(f"Converting {filename}...", end=' ')
            try:
                points = gpx_to_nmea(gpx_path, nmea_path)
                if points > 0:
                    size_kb = os.path.getsize(nmea_path) / 1024
                    print(f"{points} points, {size_kb:.1f} KB")
                    total_points += points
                    file_count += 1
                else:
                    print("skipped (no points)")
            except Exception as e:
                print(f"Error: {e}")
                error_count += 1
    
    print(f"\nConverted {file_count} files, {total_points} total points, {error_count} errors")
    print(f"Output: {nmea_dir}")

if __name__ == "__main__":
    gpx_dir = sys.argv[1] if len(sys.argv) > 1 else "data/raw"
    nmea_dir = sys.argv[2] if len(sys.argv) > 2 else "data/nmea"
    
    convert_directory(gpx_dir, nmea_dir)
