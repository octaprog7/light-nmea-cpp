#!/usr/bin/env python3
"""Скачивание GPS-треков с OpenStreetMap"""

import os
import requests
import time
import sys

def download_gps_traces(output_dir="data/raw", max_pages=10):
    """Скачивание публичных GPS-треков"""
    os.makedirs(output_dir, exist_ok=True)
    
    # Правильный API endpoint для GPS traces
    # Используем маленькие bbox для избежания лимита
    bboxes = [
        "37.5,55.7,37.6,55.75",  # Москва, центр
        "30.3,59.9,30.4,59.95",  # Санкт-Петербург
        "60.5,56.8,60.6,56.85",  # Екатеринбург
        "49.1,55.7,49.2,55.75",  # Казань
        "39.7,47.2,39.8,47.25",  # Ростов-на-Дону
    ]
    
    downloaded = 0
    
    for page in range(max_pages):
        bbox = bboxes[page % len(bboxes)]
        url = f"https://api.openstreetmap.org/api/0.6/trackpoints?bbox={bbox}&page={page}"
        
        print(f"[{page+1}/{max_pages}] Fetching bbox={bbox}, page={page}...")
        
        try:
            response = requests.get(url, timeout=30, stream=True)
            
            if response.status_code == 200:
                filename = os.path.join(output_dir, f"trace_{page:03d}.gpx.gz")
                with open(filename, 'wb') as f:
                    for chunk in response.iter_content(chunk_size=8192):
                        f.write(chunk)
                
                size_mb = os.path.getsize(filename) / (1024 * 1024)
                print(f"  Saved: {filename} ({size_mb:.2f} MB)")
                downloaded += 1
                
            elif response.status_code == 400:
                print(f"  Error 400: {response.text}")
                # Пробуем меньший bbox
                continue
                
            else:
                print(f"  Error {response.status_code}: {response.text}")
                
            time.sleep(2)  # Не перегружаем сервер
            
        except Exception as e:
            print(f"  Exception: {e}")
            continue
    
    print(f"\nDownloaded {downloaded} traces to {output_dir}/")
    return downloaded

if __name__ == "__main__":
    max_pages = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    download_gps_traces(max_pages=max_pages)
