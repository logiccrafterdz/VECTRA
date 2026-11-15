"""HistData tick data downloader and converter for VECTRA research engine.

Fetches free historical tick data from HistData.com, parses Bid/Ask and timestamps,
and converts them into VECTRA's normalized CSV format:
symbol,bid,ask,source_ts_ns,sequence
"""

import sys
import os
import re
import io
import zipfile
import datetime
import urllib.request
import urllib.parse
import http.cookiejar

def fetch_histdata_ticks(pair="EURUSD", year="2023", month="1", cache_zip="examples/histdata_eurusd.zip"):
    if os.path.exists(cache_zip) and os.path.getsize(cache_zip) > 10000:
        print(f"Using cached zip archive: {cache_zip} ({os.path.getsize(cache_zip)/(1024*1024):.2f} MB)", flush=True)
        with open(cache_zip, "rb") as f:
            return f.read()

    url_month = f"{int(month)}"
    landing_url = f"https://www.histdata.com/download-free-forex-historical-data/?/ascii/tick-data-quotes/{pair.lower()}/{year}/{url_month}"
    
    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
    }
    
    cj = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
    
    print(f"Connecting to {landing_url} ...", flush=True)
    req = urllib.request.Request(landing_url, headers=headers)
    with opener.open(req, timeout=15) as resp:
        html = resp.read().decode('utf-8', errors='ignore')
        
    form_match = re.search(r'<form[^>]*id=["\']file_down["\'][^>]*>(.*?)</form>', html, re.DOTALL)
    if not form_match:
        raise RuntimeError("Could not find download form on landing page.")
        
    form_inputs = re.findall(r'<input[^>]*name=["\']([^"\']+)["\'][^>]*value=["\']([^"\']*)["\']', form_match.group(1))
    form_data = {k: v for k, v in form_inputs}
    
    tk_match = re.search(r'id=["\']tk["\']\s+value=["\']([^"\']+)["\']', html)
    if tk_match:
        form_data['tk'] = tk_match.group(1)
        
    print(f"Found download token: {form_data.get('tk')[:10]}...", flush=True)
    post_url = "https://www.histdata.com/get.php"
    post_headers = {
        'User-Agent': headers['User-Agent'],
        'Referer': landing_url,
        'Content-Type': 'application/x-www-form-urlencoded',
    }
    encoded = urllib.parse.urlencode(form_data).encode('utf-8')
    post_req = urllib.request.Request(post_url, data=encoded, headers=post_headers)
    
    print("Requesting ZIP archive from HistData...", flush=True)
    os.makedirs(os.path.dirname(cache_zip), exist_ok=True)
    with opener.open(post_req, timeout=30) as resp:
        content_length = resp.headers.get('Content-Length')
        total_len = int(content_length) if content_length else 0
        print(f"Content-Length: {total_len/(1024*1024):.2f} MB" if total_len else "Content-Length: chunked/unknown", flush=True)
        
        chunks = []
        downloaded = 0
        while True:
            chunk = resp.read(64 * 1024)
            if not chunk:
                break
            chunks.append(chunk)
            downloaded += len(chunk)
            if downloaded % (512 * 1024) == 0 or not chunk:
                print(f"Downloaded {downloaded/(1024*1024):.2f} MB ...", flush=True)
                
        zip_bytes = b"".join(chunks)
        print(f"Finished download: total {len(zip_bytes)/(1024*1024):.2f} MB.", flush=True)
        with open(cache_zip, "wb") as f:
            f.write(zip_bytes)
            
    return zip_bytes

def inspect_and_convert(zip_bytes, output_csv_path, symbol="EUR/USD", max_rows=None):
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
        file_list = zf.namelist()
        print(f"Files inside zip: {file_list}", flush=True)
        csv_name = [f for f in file_list if f.endswith('.csv')][0]
        
        with zf.open(csv_name) as f:
            lines_sample = []
            for _ in range(5):
                line = f.readline().decode('utf-8').strip()
                if line:
                    lines_sample.append(line)
            print("Sample raw rows from HistData:", flush=True)
            for l in lines_sample:
                print("  ", l, flush=True)
                
        print(f"Converting to {output_csv_path} (max_rows={max_rows}) ...", flush=True)
        count = 0
        skipped = 0
        sequence = 1
        
        with zf.open(csv_name) as in_f, open(output_csv_path, "w", encoding="utf-8") as out_f:
            out_f.write("symbol,bid,ask,source_ts_ns,sequence\n")
            
            for line_bytes in in_f:
                line = line_bytes.decode('utf-8').strip()
                if not line:
                    continue
                parts = [p.strip() for p in line.replace(';', ',').split(',')]
                if len(parts) < 3:
                    skipped += 1
                    continue
                    
                dt_str = parts[0]
                bid_str = parts[1]
                ask_str = parts[2]
                
                try:
                    dt_clean = dt_str.replace('-', '').replace(':', '').replace('.', '')
                    date_part, time_part = dt_clean.split(' ')
                    year = int(date_part[0:4])
                    month = int(date_part[4:6])
                    day = int(date_part[6:8])
                    hour = int(time_part[0:2])
                    minute = int(time_part[2:4])
                    second = int(time_part[4:6])
                    millis = int(time_part[6:9]) if len(time_part) >= 9 else 0
                    
                    dt = datetime.datetime(year, month, day, hour, minute, second, millis * 1000, tzinfo=datetime.timezone.utc)
                    ts_ns = int(dt.timestamp() * 1_000_000_000)
                    
                    out_f.write(f"{symbol},{bid_str},{ask_str},{ts_ns},{sequence}\n")
                    sequence += 1
                    count += 1
                    if max_rows and count >= max_rows:
                        break
                except Exception:
                    skipped += 1
                    continue
                    
        print(f"Successfully converted {count} ticks (skipped {skipped}) to {output_csv_path}", flush=True)

if __name__ == '__main__':
    max_ticks = 50000
    if len(sys.argv) > 1:
        max_ticks = int(sys.argv[1])
        
    out_path = os.path.join("examples", "real_eurusd_ticks.csv")
    zip_data = fetch_histdata_ticks(pair="EURUSD", year="2023", month="1")
    inspect_and_convert(zip_data, out_path, symbol="EUR/USD", max_rows=max_ticks)
